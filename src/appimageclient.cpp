// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimageclient.hpp"
#include "appimagedesktop.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr qint64 MAX_ICON_BYTES    = 8 * 1024 * 1024;
constexpr qint64 MAX_EXTRACT_BYTES = 1024LL * 1024 * 1024;  ///< what reading the metadata may unpack

using Cancel = std::shared_ptr<std::atomic_bool>;

QString installDirectory() {
    return QDir::homePath() + QStringLiteral("/Applications");
}

QString menuDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
}

QString iconDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/cachyos-pi/appimage-icons");
}

/// One writer at a time, across all windows of this app.
QString lockPath() {
    return installDirectory() + QStringLiteral("/.cachyos-pi.lock");
}

QString readText(const QString& path, qint64 limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/// A file this app put into the install folder: a plain file directly in it. An entry that
/// was edited by hand, or a link, never makes it delete or start anything else.
bool ownedFile(const QString& path) {
    return appimage::isManagedPath(path, installDirectory()) && !QFileInfo(path).isSymLink();
}

bool occupied(const QString& path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

/// Size of everything below `path`, counted up to `limit`.
qint64 directorySize(const QString& path, qint64 limit) {
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext() && total <= limit) {
        const auto info = it.nextFileInfo();
        if (!info.isSymLink()) {
            total += info.size();
        }
    }
    return total;
}

enum class Extraction { Done, Failed, TooLarge, Cancelled };

/// Runs the AppImage's own extractor for one path pattern (everything when empty). Stops
/// when it is cancelled, takes too long or unpacks more than MAX_EXTRACT_BYTES.
Extraction extract(const Cancel& cancel, const QString& appImage, const QString& workingDirectory, const QString& pattern, int timeout) {
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setStandardInputFile(QProcess::nullDevice());
    QStringList arguments{QStringLiteral("--appimage-extract")};
    if (!pattern.isEmpty()) {
        arguments << pattern;
    }
    process.start(appImage, arguments);
    const auto stop = [&process](Extraction result) {
        process.kill();
        process.waitForFinished(2000);
        return result;
    };
    for (int waited = 0; !process.waitForFinished(250); waited += 250) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        if (cancel->load()) {
            return stop(Extraction::Cancelled);
        }
        if (waited >= timeout) {
            return stop(Extraction::Failed);
        }
        if (waited % 1000 == 0 && directorySize(workingDirectory, MAX_EXTRACT_BYTES) > MAX_EXTRACT_BYTES) {
            return stop(Extraction::TooLarge);
        }
    }
    if (directorySize(workingDirectory, MAX_EXTRACT_BYTES) > MAX_EXTRACT_BYTES) {
        return Extraction::TooLarge;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? Extraction::Done : Extraction::Failed;
}

/// `path` with every link followed, or empty when that leaves `root` or is not a plain file.
/// An AppImage is free to ship links that point anywhere.
QString confined(const QString& root, const QString& path) {
    const auto resolved = QFileInfo(path).canonicalFilePath();
    const auto base     = QFileInfo(root).canonicalFilePath();
    if (resolved.isEmpty() || base.isEmpty() || !resolved.startsWith(base + QLatin1Char('/')) || !QFileInfo(resolved).isFile()) {
        return {};
    }
    return resolved;
}

struct DesktopFile {
    QString name;  ///< as the AppImage calls it; a link keeps its own name, not its target's
    QString path;  ///< where the text is read from
};

DesktopFile findDesktopFile(const QString& root) {
    const auto names = QDir(root).entryList({QStringLiteral("*.desktop")}, QDir::Files | QDir::System | QDir::Hidden, QDir::Name);
    for (const auto& name : names) {
        if (const auto path = confined(root, root + QLatin1Char('/') + name); !path.isEmpty()) {
            return {name, path};
        }
    }
    return {};
}

/// The best icon inside the extracted tree: a scalable one, else the largest bitmap.
QString findIcon(const QString& root, const QString& iconName) {
    QStringList candidates;
    const auto name = QFileInfo(iconName).fileName();  // "Icon=" may carry a path or an extension
    const auto stem = name.endsWith(QLatin1String(".png")) || name.endsWith(QLatin1String(".svg")) ? name.chopped(4) : name;
    if (!stem.isEmpty()) {
        for (const auto* size : {"scalable", "512x512", "256x256", "192x192", "128x128", "96x96", "64x64", "48x48"}) {
            for (const auto* extension : {".svg", ".png"}) {
                candidates << QStringLiteral("%1/usr/share/icons/hicolor/%2/apps/%3%4").arg(root, QLatin1String(size), stem, QLatin1String(extension));
            }
        }
        candidates << root + QLatin1Char('/') + stem + QStringLiteral(".svg") << root + QLatin1Char('/') + stem + QStringLiteral(".png")
                   << root + QStringLiteral("/usr/share/pixmaps/") + stem + QStringLiteral(".png");
    }
    candidates << root + QStringLiteral("/.DirIcon");
    for (const auto& candidate : std::as_const(candidates)) {
        const auto path = confined(root, candidate);
        if (path.isEmpty() || QFileInfo(path).size() > MAX_ICON_BYTES || QFileInfo(path).size() == 0) {
            continue;
        }
        // .DirIcon has no extension: look at the content
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const auto head = file.read(256);
        if (head.startsWith("\x89PNG") || head.contains("<svg") || head.startsWith("<?xml")) {
            return path;
        }
    }
    return {};
}

/// Our menu entries: desktop file name -> keys of its main group.
QHash<QString, QHash<QString, QString>> managedEntries() {
    QHash<QString, QHash<QString, QString>> entries;
    const QDir directory(menuDirectory());
    for (const auto& name : directory.entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
        const auto keys = appimage::mainGroup(readText(directory.filePath(name), appimage::MAX_ENTRY_BYTES));
        if (keys.contains(QLatin1String(appimage::PATH_KEY))) {
            entries.insert(name, keys);
        }
    }
    return entries;
}

/// True when another program already owns a menu entry of this name: a file of the user's
/// that is not ours, or one that a package installed.
bool nameTaken(const QString& desktopName) {
    const auto own = menuDirectory() + QLatin1Char('/') + desktopName;
    if (occupied(own)) {
        return !appimage::mainGroup(readText(own, appimage::MAX_ENTRY_BYTES)).contains(QLatin1String(appimage::PATH_KEY));
    }
    for (const auto& directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (directory != menuDirectory() && QFileInfo::exists(directory + QLatin1Char('/') + desktopName)) {
            return true;
        }
    }
    return false;
}

/// Where the new file goes. A name that is in use is only taken over from `replaceable`,
/// the earlier copy of the same app; anything else there belongs to someone else.
QString freeDestination(const QString& fileName, const QString& replaceable) {
    const auto first = installDirectory() + QLatin1Char('/') + fileName;
    if (!occupied(first) || first == replaceable) {
        return first;
    }
    const auto dot  = fileName.lastIndexOf(QLatin1Char('.'));
    const auto stem = fileName.left(dot).left(100);
    for (int n = 2; n < 1000; ++n) {
        const auto path = QStringLiteral("%1/%2-%3%4").arg(installDirectory(), stem).arg(n).arg(fileName.mid(dot));
        if (!occupied(path) || path == replaceable) {
            return path;
        }
    }
    return {};
}

void refreshMenu() {
    // only needed for file associations; menus watch the directory themselves
    const auto tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!tool.isEmpty()) {
        QProcess::startDetached(tool, {QStringLiteral("-q"), menuDirectory()});
    }
}

/// Removes a file when it goes out of scope, unless it was handed over.
struct Discard {
    QString path;
    ~Discard() {
        if (!path.isEmpty()) {
            QFile::remove(path);
        }
    }
};

AppImageResult install(const QString& source, const Cancel& cancel) {
    const auto cancelled = AppImageResult{AppImageClient::tr("Adding the AppImage was stopped."), {}};
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isFile() || !appimage::isAppImage(source)) {
        return {AppImageClient::tr("This file is not an AppImage."), {}};
    }
    auto fileName = appimage::safeName(sourceInfo.fileName()).left(100);
    if (!fileName.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive)) {
        fileName += QStringLiteral(".AppImage");
    }
    const auto cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(installDirectory()) || !QDir().mkpath(menuDirectory()) || !QDir().mkpath(iconDirectory()) || !QDir().mkpath(cacheDirectory)) {
        return {AppImageClient::tr("Could not create the folders for AppImages in your home directory."), {}};
    }
    QLockFile lock(lockPath());
    if (!lock.tryLock(0)) {
        return {AppImageClient::tr("Another window of this app is adding or removing an AppImage. Try again when it has finished."), {}};
    }

    // Work on a private copy inside the install folder: its name is new and ours alone, and
    // the last step is a rename on one file system.
    // (Not a QTemporaryFile: that keeps the file open, and a file open for writing cannot be run.)
    const auto stagingPath = QStringLiteral("%1/.cachyos-pi-%2.part").arg(installDirectory()).arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
    QFile staging(stagingPath);
    QFile input(source);
    if (!staging.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {  // fails if the name exists
        return {AppImageClient::tr("Could not copy the file to %1.").arg(installDirectory()), {}};
    }
    Discard stagedFile{stagingPath};
    if (!input.open(QIODevice::ReadOnly)) {
        return {AppImageClient::tr("Could not copy the file to %1.").arg(installDirectory()), {}};
    }
    while (!input.atEnd()) {
        const auto chunk = input.read(4 * 1024 * 1024);
        if (cancel->load()) {
            return cancelled;
        }
        if (input.error() != QFile::NoError || staging.write(chunk) != chunk.size()) {
            return {AppImageClient::tr("Could not copy the file to %1. Is there enough free space?").arg(installDirectory()), {}};
        }
    }
    if (!staging.flush()) {
        return {AppImageClient::tr("Could not copy the file to %1. Is there enough free space?").arg(installDirectory()), {}};
    }
    staging.close();
    QFile::setPermissions(stagingPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::ExeOther);

    // The menu entry and the icon are inside the file. Ask its extractor for those parts
    // only; a runtime that does not understand the patterns gets one run that unpacks
    // everything, up to the size limit. The scratch folder is on disk, not in /tmp.
    QTemporaryDir scratch(cacheDirectory + QStringLiteral("/appimage-XXXXXX"));
    if (!scratch.isValid()) {
        return {AppImageClient::tr("Could not create a temporary folder."), {}};
    }
    const auto tooLarge = AppImageResult{AppImageClient::tr("The AppImage unpacks to more than 1 GiB before its name and icon can be read, so it was not added."), {}};
    const auto root     = scratch.path() + QStringLiteral("/squashfs-root");
    for (const auto* pattern : {"*.desktop", "*.png", "*.svg", ".DirIcon", "usr/share/applications/*", "usr/share/icons/*", "usr/share/pixmaps/*"}) {
        const auto result = extract(cancel, stagingPath, scratch.path(), QLatin1String(pattern), 30000);
        if (result == Extraction::Cancelled) {
            return cancelled;
        }
        if (result == Extraction::TooLarge) {
            return tooLarge;
        }
    }
    auto desktopFile = findDesktopFile(root);
    if (desktopFile.path.isEmpty()) {
        QDir(root).removeRecursively();
        const auto result = extract(cancel, stagingPath, scratch.path(), {}, 300000);
        if (result == Extraction::Cancelled) {
            return cancelled;
        }
        if (result == Extraction::TooLarge) {
            return tooLarge;
        }
        if (result != Extraction::Done) {
            return {AppImageClient::tr("The AppImage could not be read. It may be damaged, or built for another kind of computer."), {}};
        }
        desktopFile = findDesktopFile(root);
    }
    const auto original = readText(desktopFile.path, appimage::MAX_ENTRY_BYTES);
    const auto keys     = appimage::mainGroup(original);
    if (desktopFile.path.isEmpty() || keys.value(QStringLiteral("Name")).isEmpty()) {
        return {AppImageClient::tr("The AppImage does not describe an application (no menu entry inside)."), {}};
    }

    // "org.example.App.desktop" keeps its name so windows are matched to the entry, unless
    // that name belongs to something else already.
    auto desktopName = appimage::safeDesktopName(desktopFile.name);
    if (nameTaken(desktopName)) {
        desktopName.prepend(QStringLiteral("appimage-"));
    }
    if (nameTaken(desktopName)) {
        return {AppImageClient::tr("Another application already uses the menu entry “%1”.").arg(desktopName), {}};
    }
    const auto id = desktopName.chopped(8);  // without ".desktop"

    // An entry of that name which is ours is the same app, added before: its file is replaced.
    auto previous = managedEntries().value(desktopName).value(QLatin1String(appimage::PATH_KEY));
    if (!ownedFile(previous) || !QFileInfo::exists(previous)) {
        previous.clear();
    }
    const auto destination = freeDestination(fileName, previous);
    if (destination.isEmpty()) {
        return {AppImageClient::tr("Could not find a free file name in %1.").arg(installDirectory()), {}};
    }

    // The new icon waits next to its place until the entry that names it is written.
    QString iconPath;
    Discard stagedIcon;
    if (const auto icon = findIcon(root, keys.value(QStringLiteral("Icon"))); !icon.isEmpty()) {
        QFile file(icon);
        const bool svg = file.open(QIODevice::ReadOnly) && !file.read(8).startsWith("\x89PNG");
        file.close();
        const auto target = iconDirectory() + QLatin1Char('/') + id + (svg ? QStringLiteral(".svg") : QStringLiteral(".png"));
        const auto staged = target + QStringLiteral(".new");
        QFile::remove(staged);
        if (QFile::copy(icon, staged)) {
            QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
            iconPath        = target;
            stagedIcon.path = staged;
        }
    }

    const auto entry = appimage::integrate(original, destination, iconPath);
    if (entry.isEmpty()) {
        return {AppImageClient::tr("The menu entry inside the AppImage is not usable."), {}};
    }
    if (cancel->load()) {
        return cancelled;
    }

    // From here on things the user already has are touched. The earlier copy is moved aside,
    // not deleted, until the new entry is written, and is put back if anything fails.
    QString backup;
    const auto restore = [&] {
        QFile::remove(destination);
        if (!backup.isEmpty()) {
            QFile::rename(backup, destination);
        }
    };
    if (occupied(destination)) {  // only ever `previous`, see freeDestination()
        backup = QStringLiteral("%1/.cachyos-pi-%2.bak").arg(installDirectory()).arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
        if (!QFile::rename(destination, backup)) {
            return {AppImageClient::tr("Could not replace the earlier copy in %1.").arg(installDirectory()), {}};
        }
    }
    if (!QFile::rename(stagingPath, destination)) {
        restore();
        return {AppImageClient::tr("Could not move the AppImage into %1.").arg(installDirectory()), {}};
    }
    stagedFile.path.clear();
    QSaveFile menuEntry(menuDirectory() + QLatin1Char('/') + desktopName);
    if (!menuEntry.open(QIODevice::WriteOnly) || menuEntry.write(entry.toUtf8()) < 0 || !menuEntry.commit()) {
        restore();
        return {AppImageClient::tr("Could not write the menu entry. Nothing was changed."), {}};
    }

    // Committed: the rest is tidying up.
    for (const auto* extension : {".svg", ".png"}) {
        QFile::remove(iconDirectory() + QLatin1Char('/') + id + QLatin1String(extension));
    }
    if (!stagedIcon.path.isEmpty() && QFile::rename(stagedIcon.path, iconPath)) {
        stagedIcon.path.clear();
    }
    if (!backup.isEmpty()) {
        QFile::remove(backup);
    }
    if (!previous.isEmpty() && previous != destination) {
        QFile::remove(previous);
    }
    refreshMenu();
    return {{}, keys.value(QStringLiteral("Name"))};
}

}  // namespace

AppImageClient::AppImageClient(QObject* parent) : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)) {
    connect(&m_watcher, &QFutureWatcher<AppImageResult>::finished, this, [this] {
        const auto result = m_watcher.result();
        m_busy            = false;
        emit busyChanged();
        setMessage(result.error, result.error.isEmpty() ? result.name : QString());
        refresh();
    });
}

AppImageClient::~AppImageClient() {
    m_watcher.disconnect(this);
    m_cancel->store(true);  // stops a copy or an extraction that is running
    m_watcher.waitForFinished();
}

QString AppImageClient::directory() const {
    return installDirectory();
}

void AppImageClient::setMessage(const QString& error, const QString& added) {
    m_error = error;
    m_added = added;
    emit messageChanged();
}

void AppImageClient::clearMessage() {
    setMessage({}, {});
}

QVariantMap AppImageClient::inspect(const QUrl& file) const {
    const auto path = file.isLocalFile() ? file.toLocalFile() : QString();
    const QFileInfo info(path);
    QVariantMap map{{QStringLiteral("ok"), false}, {QStringLiteral("name"), info.fileName()}};
    if (path.isEmpty() || !info.isFile()) {
        map.insert(QStringLiteral("reason"), tr("This is not a file on this computer."));
    } else if (!appimage::isAppImage(path)) {
        map.insert(QStringLiteral("reason"), tr("“%1” is not an AppImage.").arg(info.fileName()));
    } else {
        map.insert(QStringLiteral("ok"), true);
        map.insert(QStringLiteral("size"), QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeIecFormat));
    }
    return map;
}

void AppImageClient::add(const QUrl& file) {
    if (m_busy || !file.isLocalFile()) {
        return;
    }
    m_busy = true;
    emit busyChanged();
    clearMessage();
    m_watcher.setFuture(QtConcurrent::run(install, file.toLocalFile(), m_cancel));
}

void AppImageClient::remove(const QString& id, const QString& path) {
    if (m_busy) {
        setMessage(tr("An AppImage is being added. Remove this one when that has finished."), {});
        return;
    }
    QDir().mkpath(installDirectory());  // the lock lives there
    QLockFile lock(lockPath());
    if (!lock.tryLock(0)) {
        setMessage(tr("Another window of this app is adding or removing an AppImage. Try again when it has finished."), {});
        return;
    }
    const auto desktopName = id + QStringLiteral(".desktop");
    const auto current     = managedEntries().value(desktopName).value(QLatin1String(appimage::PATH_KEY));
    if (current.isEmpty() || current != path) {
        // the entry is not what the confirmation showed any more
        setMessage(tr("The list of AppImages changed in the meantime. Nothing was removed."), {});
        refresh();
        return;
    }
    // An entry that points anywhere else (edited by hand) loses only the entry itself.
    if (ownedFile(path) && QFileInfo::exists(path) && !QFile::remove(path)) {
        setMessage(tr("Could not delete %1.").arg(path), {});
        return;
    }
    QFile::remove(menuDirectory() + QLatin1Char('/') + desktopName);
    for (const auto* extension : {".svg", ".png"}) {
        QFile::remove(iconDirectory() + QLatin1Char('/') + id + QLatin1String(extension));
    }
    refreshMenu();
    clearMessage();
    refresh();
}

void AppImageClient::launch(const QString& id) {
    const auto keys = managedEntries().value(id + QStringLiteral(".desktop"));
    const auto path = keys.value(QLatin1String(appimage::PATH_KEY));
    // started the way the menu entry does it: same arguments, nothing passed through a shell
    const auto command = appimage::commandLine(keys.value(QStringLiteral("Exec")));
    if (!ownedFile(path) || command.isEmpty() || command.first() != path || !QProcess::startDetached(path, command.mid(1), QDir::homePath())) {
        setMessage(tr("Could not start the application."), {});
    }
}

void AppImageClient::refresh() {
    m_fuseAvailable = QFileInfo::exists(QStringLiteral("/usr/lib/libfuse.so.2"));

    QList<QVariantMap> apps;
    IconIndex icons;
    const auto entries = managedEntries();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const auto id   = it.key().chopped(8);
        const auto path = it->value(QLatin1String(appimage::PATH_KEY));
        const QFileInfo file(path);
        apps << QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), it->value(QStringLiteral("Name"))},
            {QStringLiteral("summary"), it->value(QStringLiteral("Comment"))},
            {QStringLiteral("version"), it->value(QStringLiteral("X-AppImage-Version"))},
            {QStringLiteral("path"), path},
            {QStringLiteral("size"), file.exists() ? QLocale().formattedDataSize(file.size(), 1, QLocale::DataSizeIecFormat) : QString()},
            {QStringLiteral("missing"), !file.exists()},
            // false for an entry that was edited to point somewhere else: its file is left alone
            {QStringLiteral("owned"), ownedFile(path)},
            // a command line tool: there is nothing to open without a terminal
            {QStringLiteral("terminal"), it->value(QStringLiteral("Terminal")) == QLatin1String("true")},
        };
        const auto icon = it->value(QStringLiteral("Icon"));
        if (icon.startsWith(QLatin1Char('/'))) {
            icons.insert(id, IconRef{icon, {}});
        } else if (!icon.isEmpty()) {
            icons.insert(id, IconRef{{}, icon});
        }
    }
    std::ranges::sort(apps, [](const QVariantMap& a, const QVariantMap& b) {
        return a.value(QStringLiteral("name")).toString().compare(b.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    m_apps.clear();
    for (const auto& app : std::as_const(apps)) {
        m_apps << app;
    }
    emit iconsLoaded(icons);
    emit appsChanged();
}
