// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimageclient.hpp"
#include "appimagedesktop.hpp"
#include "appimageimport.hpp"

#include "files.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QLockFile>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <expected>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 MAX_ICON_BYTES    = 8 * 1024 * 1024;
constexpr qint64 MAX_EXTRACT_BYTES = 1024LL * 1024 * 1024;  ///< what reading the metadata may unpack
constexpr qint64 MAX_RELEASE_BYTES = 8 * 1024 * 1024;       ///< the description of a release
constexpr qint64 CHECK_INTERVAL    = 6 * 60 * 60;           ///< seconds between two looks for newer releases
constexpr auto LAST_CHECK_KEY      = "appimage/lastUpdateCheck"_L1;
constexpr qint64 MAX_IMPORT_BYTES  = 4 * 1024 * 1024;  ///< the lists and configuration of other programs

using Cancel = std::shared_ptr<std::atomic_bool>;

QString installDirectory() {
    return QDir::homePath() + u"/Applications"_s;
}

QString menuDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
}

QString iconDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/cachyos-pi/appimage-icons"_s;
}

/// One writer at a time, across all windows of this app.
QString lockPath() {
    return installDirectory() + u"/.cachyos-pi.lock"_s;
}

QByteArray readBytes(const QString& path, qint64 limit) {
    return readCapped(path, limit, pi::file_utils::Links::Follow).value_or(QByteArray());
}

QString readText(const QString& path, qint64 limit) {
    return QString::fromUtf8(readBytes(path, limit));
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
    QStringList arguments{u"--appimage-extract"_s};
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
    if (resolved.isEmpty() || base.isEmpty() || !resolved.startsWith(base + u'/') || !QFileInfo(resolved).isFile()) {
        return {};
    }
    return resolved;
}

struct DesktopFile {
    QString name;  ///< as the AppImage calls it; a link keeps its own name, not its target's
    QString path;  ///< where the text is read from
};

DesktopFile findDesktopFile(const QString& root) {
    const auto names = QDir(root).entryList({u"*.desktop"_s}, QDir::Files | QDir::System | QDir::Hidden, QDir::Name);
    for (const auto& name : names) {
        if (const auto path = confined(root, root + u'/' + name); !path.isEmpty()) {
            return {name, path};
        }
    }
    return {};
}

/// The best icon inside the extracted tree: a scalable one, else the largest bitmap.
QString findIcon(const QString& root, const QString& iconName) {
    QStringList candidates;
    const auto name = QFileInfo(iconName).fileName();  // "Icon=" may carry a path or an extension
    const auto stem = name.endsWith(".png"_L1) || name.endsWith(".svg"_L1) ? name.chopped(4) : name;
    if (!stem.isEmpty()) {
        for (const auto* size : {"scalable", "512x512", "256x256", "192x192", "128x128", "96x96", "64x64", "48x48"}) {
            for (const auto* extension : {".svg", ".png"}) {
                candidates << u"%1/usr/share/icons/hicolor/%2/apps/%3%4"_s.arg(root, QLatin1StringView(size), stem, QLatin1StringView(extension));
            }
        }
        candidates << root + u'/' + stem + u".svg"_s << root + u'/' + stem + u".png"_s
                   << root + u"/usr/share/pixmaps/"_s + stem + u".png"_s;
    }
    candidates << root + u"/.DirIcon"_s;
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

using Entries = QHash<QString, QHash<QString, QString>>;

/// The user's menu entries: desktop file name -> keys of its main group. With `mark`,
/// only those whose text has it: most entries are of no interest, and are told apart
/// without parsing them.
Entries menuEntries(QLatin1StringView mark = {}) {
    Entries entries;
    const QDir directory(menuDirectory());
    for (const auto& name : directory.entryList({u"*.desktop"_s}, QDir::Files)) {
        const auto text = readText(directory.filePath(name), appimage::MAX_ENTRY_BYTES);
        if (mark.isEmpty() || text.contains(mark)) {
            entries.insert(name, appimage::mainGroup(text));
        }
    }
    return entries;
}

/// The menu entries this app wrote.
Entries managedEntries() {
    auto entries = menuEntries(appimage::PATH_KEY);
    entries.removeIf([](const auto& entry) { return !entry.value().contains(appimage::PATH_KEY); });
    return entries;
}

/// A menu entry another program wrote for the AppImage `file`: it starts that file and
/// is not one of ours.
bool startsForeign(const QHash<QString, QString>& keys, const QString& file) {
    // TryExec as well: an Exec line can quote the path the way a shell does, which desktop entries do not
    return !file.isEmpty() && !keys.contains(appimage::PATH_KEY)
        && (keys.value(u"TryExec"_s) == file || appimage::commandLine(keys.value(u"Exec"_s)).contains(file));
}

/// True when another program already owns a menu entry of this name: a file of the user's
/// that is not ours, or one that a package installed. The entry the earlier manager of
/// `takenOver` wrote for that file does not count: it is replaced along with it.
bool nameTaken(const QString& desktopName, const QString& takenOver = {}) {
    const auto own = menuDirectory() + u'/' + desktopName;
    if (occupied(own)) {
        const auto keys = appimage::mainGroup(readText(own, appimage::MAX_ENTRY_BYTES));
        return !keys.contains(appimage::PATH_KEY) && !startsForeign(keys, takenOver);
    }
    for (const auto& directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (directory != menuDirectory() && QFileInfo::exists(directory + u'/' + desktopName)) {
            return true;
        }
    }
    return false;
}

/// A file this app downloaded: it lies in the install folder under a name of ours already
/// and is the working copy. `name` is what it is called where it came from.
struct Fetched {
    QString name;
    appimage::Tracking tracking;
    QString replaces;        ///< the menu entry this is an update of; empty for a new app
    bool takesOver = false;  ///< the file is one another program manages: its menu entry may be replaced
};

/// True when `existing`, an entry of ours, is an earlier copy of the app that is being
/// added: both came from the same repository, or they are called the same.
bool sameApp(const QHash<QString, QString>& existing, const QHash<QString, QString>& added, const appimage::Tracking& tracking) {
    const auto source = appimage::tracking(existing).source;
    if (!source.isEmpty() && !tracking.source.isEmpty()) {
        return source == tracking.source;
    }
    return existing.value(u"Name"_s) == added.value(u"Name"_s);
}

/// The name of the menu entry for the app `added` describes. `wanted`, as the AppImage
/// calls it, is kept so windows are matched to the entry, unless it belongs to something
/// else: another program, or another app of ours that calls its entry alike. Empty when
/// no name is free.
QString entryName(const QString& wanted, const Entries& ours, const QHash<QString, QString>& added, const Fetched& fetched, const QString& foreign) {
    const auto usable = [&](const QString& name) {
        if (const auto existing = ours.constFind(name); existing != ours.cend()) {
            return name == fetched.replaces || sameApp(*existing, added, fetched.tracking);
        }
        return !nameTaken(name, foreign);
    };
    if (usable(wanted)) {
        return wanted;
    }
    // an update stays the entry it is, whatever the new release calls it
    if (!fetched.replaces.isEmpty()) {
        return fetched.replaces;
    }
    const auto stem = u"appimage-"_s + wanted.chopped(8);  // without ".desktop"
    for (int n = 1; n < 100; ++n) {
        const auto name = n == 1 ? stem + u".desktop"_s : u"%1-%2.desktop"_s.arg(stem).arg(n);
        if (usable(name)) {
            return name;
        }
    }
    return {};
}

/// Where the new file goes. A name that is in use is only taken over from `replaceable`,
/// the earlier copy of the same app; anything else there belongs to someone else.
QString freeDestination(const QString& fileName, const QString& replaceable) {
    const auto first = installDirectory() + u'/' + fileName;
    if (!occupied(first) || first == replaceable) {
        return first;
    }
    const auto dot  = fileName.lastIndexOf(u'.');
    const auto stem = fileName.left(dot).left(100);
    for (int n = 2; n < 1000; ++n) {
        const auto path = u"%1/%2-%3%4"_s.arg(installDirectory(), stem).arg(n).arg(fileName.mid(dot));
        if (!occupied(path) || path == replaceable) {
            return path;
        }
    }
    return {};
}

void refreshMenu() {
    // only needed for file associations; menus watch the directory themselves
    const auto tool = QStandardPaths::findExecutable(u"update-desktop-database"_s);
    if (!tool.isEmpty()) {
        QProcess::startDetached(tool, {u"-q"_s, menuDirectory()});
    }
}

/// A name in the install folder that is new and ours alone.
QString privatePath(QLatin1StringView extension) {
    return u"%1/.cachyos-pi-%2.%3"_s.arg(installDirectory()).arg(QRandomGenerator::global()->generate64(), 16, 16, u'0').arg(extension);
}

void removeIcons(const QString& id) {
    for (const auto* extension : {".svg", ".png"}) {
        QFile::remove(iconDirectory() + u'/' + id + QLatin1StringView(extension));
    }
}

/// Deletes a menu entry of ours and its icons. The AppImage it starts is the caller's business.
void removeMenuEntry(const QString& desktopName) {
    QFile::remove(menuDirectory() + u'/' + desktopName);
    removeIcons(desktopName.chopped(8));
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

/// Copies `source` to `target`, which must not exist. Returns what went wrong, if anything.
QString copyTo(const QString& source, const QString& target, const Cancel& cancel) {
    QFile output(target);
    QFile input(source);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !input.open(QIODevice::ReadOnly)) {
        return AppImageClient::tr("Could not copy the file to %1.").arg(installDirectory());
    }
    while (!input.atEnd()) {
        const auto chunk = input.read(4 * 1024 * 1024);
        if (cancel->load()) {
            return AppImageClient::tr("Adding the AppImage was stopped.");
        }
        if (input.error() != QFile::NoError || output.write(chunk) != chunk.size()) {
            return AppImageClient::tr("Could not copy the file to %1. Is there enough free space?").arg(installDirectory());
        }
    }
    if (!output.flush()) {
        return AppImageClient::tr("Could not copy the file to %1. Is there enough free space?").arg(installDirectory());
    }
    return {};
}

/// Adds `source`, a file of the user's, or the download `fetched` describes.
AppImageResult install(const QString& source, const Cancel& cancel, const Fetched& fetched) {
    const auto cancelled = AppImageResult{AppImageClient::tr("Adding the AppImage was stopped."), {}};
    const bool downloaded = !fetched.name.isEmpty();
    Discard working{downloaded ? source : QString()};
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isFile() || !appimage::isAppImage(source)) {
        return {AppImageClient::tr("This file is not an AppImage."), {}};
    }
    auto fileName = appimage::safeName(downloaded ? fetched.name : sourceInfo.fileName()).left(100);
    if (!fileName.endsWith(".AppImage"_L1, Qt::CaseInsensitive)) {
        fileName += u".AppImage"_s;
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
    // the last step is a rename on one file system. A download is such a copy already.
    // (Not a QTemporaryFile: that keeps the file open, and a file open for writing cannot be run.)
    const auto stagingPath = downloaded ? source : privatePath("part"_L1);
    if (!downloaded) {
        working.path = stagingPath;
        if (const auto failed = copyTo(source, stagingPath, cancel); !failed.isEmpty()) {
            return {failed, {}};
        }
    }
    QFile::setPermissions(stagingPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::ExeOther);

    // The menu entry and the icon are inside the file. Ask its extractor for those parts
    // only; a runtime that does not understand the patterns gets one run that unpacks
    // everything, up to the size limit. The scratch folder is on disk, not in /tmp.
    QTemporaryDir scratch(cacheDirectory + u"/appimage-XXXXXX"_s);
    if (!scratch.isValid()) {
        return {AppImageClient::tr("Could not create a temporary folder."), {}};
    }
    const auto tooLarge = AppImageResult{AppImageClient::tr("The AppImage unpacks to more than 1 GiB before its name and icon can be read, so it was not added."), {}};
    const auto root     = scratch.path() + u"/squashfs-root"_s;
    for (const auto* pattern : {"*.desktop", "*.png", "*.svg", ".DirIcon", "usr/share/applications/*", "usr/share/icons/*", "usr/share/pixmaps/*"}) {
        const auto result = extract(cancel, stagingPath, scratch.path(), QLatin1StringView(pattern), 30000);
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
    if (desktopFile.path.isEmpty() || keys.value(u"Name"_s).isEmpty()) {
        return {AppImageClient::tr("The AppImage does not describe an application (no menu entry inside)."), {}};
    }

    const auto entries     = managedEntries();
    const auto wanted      = appimage::safeDesktopName(desktopFile.name);
    const auto desktopName = entryName(wanted, entries, keys, fetched, fetched.takesOver ? source : QString());
    if (desktopName.isEmpty()) {
        return {AppImageClient::tr("Another application already uses the menu entry “%1”.").arg(wanted), {}};
    }
    // what another program hands over never replaces an app of ours, whatever its entry is
    // called: ours is newer, or what the user chose to keep
    if (fetched.takesOver && std::ranges::any_of(entries, [&](const auto& existing) { return sameApp(existing, keys, fetched.tracking); })) {
        return {AppImageClient::tr("%1 is one of your AppImages already.").arg(keys.value(u"Name"_s)), {}};
    }
    const auto id = desktopName.chopped(8);  // without ".desktop"

    // An update belongs to one entry. That entry has to be what it was when the update was
    // offered; anything else happened behind our back while the file came in.
    QString renamedFrom;
    if (!fetched.replaces.isEmpty()) {
        if (appimage::tracking(entries.value(fetched.replaces)).source != fetched.tracking.source) {
            return {AppImageClient::tr("The list of AppImages changed in the meantime. Nothing was updated."), {}};
        }
        if (fetched.replaces != desktopName) {
            renamedFrom = fetched.replaces;  // the new release calls its menu entry differently
        }
    }

    // An entry of that name is an earlier copy of this app, see entryName(): its file is replaced.
    auto previous = entries.value(desktopName).value(appimage::PATH_KEY);
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
    if (const auto icon = findIcon(root, keys.value(u"Icon"_s)); !icon.isEmpty()) {
        QFile file(icon);
        const bool svg = file.open(QIODevice::ReadOnly) && !file.read(8).startsWith("\x89PNG");
        file.close();
        const auto target = iconDirectory() + u'/' + id + (svg ? u".svg"_s : u".png"_s);
        const auto staged = target + u".new"_s;
        QFile::remove(staged);
        if (QFile::copy(icon, staged)) {
            QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
            iconPath        = target;
            stagedIcon.path = staged;
        }
    }

    const auto entry = appimage::integrate(original, destination, iconPath, fetched.tracking);
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
        backup = privatePath("bak"_L1);
        if (!QFile::rename(destination, backup)) {
            return {AppImageClient::tr("Could not replace the earlier copy in %1.").arg(installDirectory()), {}};
        }
    }
    if (!QFile::rename(stagingPath, destination)) {
        restore();
        return {AppImageClient::tr("Could not move the AppImage into %1.").arg(installDirectory()), {}};
    }
    working.path.clear();
    QSaveFile menuEntry(menuDirectory() + u'/' + desktopName);
    if (!menuEntry.open(QIODevice::WriteOnly) || menuEntry.write(entry.toUtf8()) < 0 || !menuEntry.commit()) {
        restore();
        return {AppImageClient::tr("Could not write the menu entry. Nothing was changed."), {}};
    }

    // Committed: the rest is tidying up.
    removeIcons(id);
    if (!stagedIcon.path.isEmpty() && QFile::rename(stagedIcon.path, iconPath)) {
        stagedIcon.path.clear();
    }
    if (!backup.isEmpty()) {
        QFile::remove(backup);
    }
    if (!previous.isEmpty() && previous != destination) {
        QFile::remove(previous);
    }
    if (!renamedFrom.isEmpty()) {
        // the entry that was updated goes, along with its file: the new one took its place
        const auto old = entries.value(renamedFrom).value(appimage::PATH_KEY);
        if (ownedFile(old) && old != destination) {
            QFile::remove(old);
        }
        removeMenuEntry(renamedFrom);
    }
    refreshMenu();
    const auto name = keys.value(u"Name"_s);
    return {{}, name, AppImageClient::tr("%1 was added to your menu.").arg(name)};
}

/// An AppImage another program manages, as this app would take it over.
struct Import {
    QString key;    ///< what that program's own records call it, if it keeps any
    QString title;  ///< the name of the app, for display
    QString path;   ///< the AppImage file
    appimage::Tracking tracking;
    QStringList leftovers;  ///< what the program made for it besides a menu entry, removed once it is moved
};

/// Where a file another program updated from GitHub keeps being updated from.
appimage::Tracking takenOver(const std::optional<appimage::Source>& source, const QString& variant, const QString& release) {
    return {.source = source ? appimage::sourceValue(*source) : QString(), .asset = 0, .variant = variant, .release = release};
}

/// A program whose AppImages can be moved over to this app.
struct Importer {
    QLatin1StringView id;    ///< for the setting that says not to ask again
    QLatin1StringView name;  ///< as the user knows it; a name, so not translated
    /// What it manages, unchecked.
    QList<Import> (*records)();
    /// Takes the moved apps (their keys) off the program's own records; says what went
    /// wrong. None for a program that keeps no records besides the menu entries.
    QString (*forget)(const QStringList& keys);
};

QString configFile(QLatin1StringView name) {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + name;
}

QList<Import> shellyRecords() {
    const auto database = readBytes(configFile(appimage::SHELLY_DATABASE), MAX_IMPORT_BYTES);
    if (database.isEmpty()) {
        return {};
    }
    auto folder = appimage::shellyInstallDirectory(readBytes(configFile(appimage::SHELLY_CONFIG), MAX_IMPORT_BYTES));
    if (folder.isEmpty()) {
        folder = qEnvironmentVariable("XDG_BIN_HOME", QDir::homePath() + u"/.local/bin"_s);
    }
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QList<Import> imports;
    for (const auto& app : appimage::parseShellyDatabase(database, folder)) {
        Import import{.key = app.name,
            .title         = app.title,
            .path          = app.path,
            .tracking      = takenOver(app.source, app.variant, app.release),
            .leftovers     = {}};
        for (const auto& icon : appimage::shellyIconFiles(app)) {
            import.leftovers << data + u'/' + icon;
        }
        imports << import;
    }
    return imports;
}

QString forgetShelly(const QStringList& keys) {
    // read again: Shelly may have written to its list in the meantime
    const auto path = configFile(appimage::SHELLY_DATABASE);
    const auto rest = appimage::withoutShellyApps(readBytes(path, MAX_IMPORT_BYTES), keys);
    QSaveFile database(path);
    if (rest.isEmpty() || !database.open(QIODevice::WriteOnly) || database.write(rest) < 0 || !database.commit()) {
        return AppImageClient::tr("Shelly's own list could not be updated; it still names the apps that were moved.");
    }
    return {};
}

/// Gear Lever's configuration remembers every file it once looked at, so an app counts as
/// its own only while Gear Lever's menu entry for the file is there, see gearLeverIcon().
/// Newest first: Gear Lever may keep earlier versions of an app next to the newest, and
/// of those only the first is moved.
QList<Import> gearLeverRecords() {
    QList<appimage::GearLeverApp> apps;
    for (const auto& config : {QDir::homePath() + u'/' + appimage::GEARLEVER_FLATPAK_CONFIG, configFile(appimage::GEARLEVER_CONFIG)}) {
        apps << appimage::parseGearLeverConfig(readBytes(config, MAX_IMPORT_BYTES));
    }
    // The files that were moved or deleted stay in the configuration. A file both
    // installations know counts once; when they disagree on where it is updated from, as
    // when one follows pre-releases, it is updated from nowhere.
    QHash<QString, appimage::GearLeverApp> byPath;
    for (auto& app : apps) {
        if (!QFileInfo(app.path).isFile()) {
            continue;
        }
        const auto known = byPath.find(app.path);
        if (known == byPath.end()) {
            byPath.insert(app.path, std::move(app));
        } else if (known->source.has_value() != app.source.has_value() || known->variant != app.variant
            || (app.source && appimage::sourceValue(*known->source) != appimage::sourceValue(*app.source))) {
            known->source.reset();
            known->variant.clear();
        }
    }
    apps = byPath.values();
    if (apps.isEmpty()) {
        return {};
    }
    QList<std::pair<QDateTime, Import>> found;
    const auto menu = menuEntries(appimage::GEARLEVER_NAME_KEY);
    for (const auto& app : std::as_const(apps)) {
        for (const auto& keys : menu) {
            if (const auto icon = appimage::gearLeverIcon(keys, app.path); !icon.isEmpty()) {
                found.emplace_back(QFileInfo(app.path).lastModified(),
                    Import{.key = {},
                        // the name inside the file; Gear Lever's own may carry a version: "App (1.2)"
                        .title     = keys.value(appimage::GEARLEVER_NAME_KEY),
                        .path      = app.path,
                        .tracking  = takenOver(app.source, app.variant, keys.value(u"X-AppImage-Version"_s)),
                        .leftovers = {icon}});
                break;
            }
        }
    }
    std::ranges::stable_sort(found, std::ranges::greater(), &std::pair<QDateTime, Import>::first);
    QList<Import> imports;
    for (auto& [modified, import] : found) {
        imports << std::move(import);
    }
    return imports;
}

/// Changes whenever the AppImages to move over may have: the other programs' records,
/// and the menu folder, which changes when an entry is added or removed by anyone.
/// Cheaper than reading them all again on every reload.
/// The folders of the files offered so far count too: one of them deleted by hand is no
/// longer offered.
QList<QDateTime> importStamp(const QList<AppImageImport>& offered) {
    QStringList paths{configFile(appimage::SHELLY_DATABASE), configFile(appimage::SHELLY_CONFIG),
        QDir::homePath() + u'/' + appimage::GEARLEVER_FLATPAK_CONFIG, configFile(appimage::GEARLEVER_CONFIG), menuDirectory()};
    for (const auto& app : offered) {
        paths << QFileInfo(app.path).absolutePath();
    }
    paths.removeDuplicates();
    QList<QDateTime> stamp;
    for (const auto& path : std::as_const(paths)) {
        stamp << QFileInfo(path).lastModified();
    }
    return stamp;
}

constexpr std::array IMPORTERS{
    Importer{.id = "shelly"_L1, .name = "Shelly"_L1, .records = shellyRecords, .forget = forgetShelly},
    Importer{.id = "gearlever"_L1, .name = "Gear Lever"_L1, .records = gearLeverRecords, .forget = nullptr},
};

QString dismissedKey(const Importer& importer) {
    return u"appimage/%1Dismissed"_s.arg(importer.id);
}

using Offered = QList<std::pair<const Importer*, QList<Import>>>;

/// True for a path in the install folder, links followed.
bool insideInstallDirectory(const QString& canonicalPath) {
    const auto folder = QFileInfo(installDirectory()).canonicalFilePath();
    return canonicalPath.isEmpty() || (!folder.isEmpty() && canonicalPath.startsWith(folder + u'/'));
}

/// What can be moved over, by program, from those the user did not say no to: AppImage
/// files that are there, that none of our menu entries (`ours`) starts already and that
/// are no app of ours by sameApp(). Such a file would take that app's place, and is mostly
/// an older copy of it, like one Gear Lever keeps. The same app in two programs, or twice
/// in one, is moved once.
Offered importable(const Entries& ours) {
    QList<QHash<QString, QString>> taken(ours.cbegin(), ours.cend());
    Offered offered;
    for (const auto& importer : IMPORTERS) {
        if (QSettings().value(dismissedKey(importer), false).toBool()) {
            continue;
        }
        QList<Import> apps;
        for (auto& app : importer.records()) {
            const QHash<QString, QString> keys{{u"Name"_s, app.title}, {appimage::SOURCE_KEY, app.tracking.source}};
            if (std::ranges::any_of(taken, [&](const auto& existing) {
                    return existing.value(appimage::PATH_KEY) == app.path || sameApp(existing, keys, app.tracking);
                })) {
                continue;
            }
            // by a link or ".." on the way, a path can lead to a file of ours: never one to delete
            const QFileInfo info(app.path);
            if (info.isSymLink() || !info.isFile() || !appimage::isAppImage(app.path) || insideInstallDirectory(info.canonicalFilePath())) {
                continue;
            }
            // the path too: the same file in two programs is moved once
            taken << keys;
            taken.last().insert(appimage::PATH_KEY, app.path);
            apps << std::move(app);
        }
        if (!apps.isEmpty()) {
            offered.emplace_back(&importer, std::move(apps));
        }
    }
    return offered;
}

/// What the program that managed `app` left behind for it. `menu` is the menu as it was
/// before anything was moved.
void removeTraces(const Import& app, const Entries& menu) {
    for (const auto& [name, keys] : menu.asKeyValueRange()) {
        // looked at again: the entry of that name may be the one that was just written
        const auto path = menuDirectory() + u'/' + name;
        if (startsForeign(keys, app.path) && startsForeign(appimage::mainGroup(readText(path, appimage::MAX_ENTRY_BYTES)), app.path)) {
            QFile::remove(path);
        }
    }
    for (const auto& leftover : app.leftovers) {
        // a linked folder could lead anywhere, our icons included
        if (const auto folder = QFileInfo(leftover).absolutePath(); QFileInfo(folder).canonicalFilePath() == folder) {
            QFile::remove(leftover);
        }
    }
}

/// Adds every AppImage the other programs manage and takes it off their hands. One that
/// cannot be added stays where it is, untouched.
AppImageResult migrate(const Cancel& cancel, const QStringList& shown) {
    QStringList failed;
    QStringList report;
    const auto menu = menuEntries();
    for (const auto& [importer, apps] : importable(managedEntries())) {
        QStringList moved;
        QStringList done;
        for (const auto& app : apps) {
            if (cancel->load()) {
                break;
            }
            // only what the user agreed to: a file that turned up since waits for the next time
            if (!shown.contains(app.path)) {
                continue;
            }
            const QFileInfo before(app.path);
            const auto size     = before.size();
            const auto modified = before.lastModified();
            const auto result   = install(app.path, cancel, Fetched{.name = {}, .tracking = app.tracking, .replaces = {}, .takesOver = true});
            if (!result.error.isEmpty()) {
                failed << AppImageClient::tr("%1 was not moved: %2").arg(app.title, result.error);
                continue;
            }
            // the other program may have put a newer file there in the meantime: that one stays,
            // along with what it needs
            // and the program keeps its record of it
            moved << result.name;
            if (const QFileInfo after(app.path); after.isSymLink() || after.size() != size || after.lastModified() != modified) {
                failed << AppImageClient::tr("%1 changed while it was being moved, so it was left in %2.").arg(after.fileName(), after.absolutePath());
            } else if (removeTraces(app, menu); !QFile::remove(app.path)) {
                failed << AppImageClient::tr("%1 was copied, but could not be removed from %2.").arg(before.fileName(), before.absolutePath());
            } else {
                done << app.key;
            }
        }
        if (moved.isEmpty()) {
            continue;
        }
        if (const auto error = importer->forget ? importer->forget(done) : QString(); !error.isEmpty()) {
            failed << error;
        }
        report << AppImageClient::tr("Moved from %1: %2.").arg(importer->name, QLocale().createSeparatedList(moved));
    }
    if (!report.isEmpty()) {
        refreshMenu();
    }
    return {failed.join(u'\n'), {}, report.join(u' ')};
}

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"cachyos-pi/" PI_VERSION ""_s);
    req.setTransferTimeout(20000);
    return req;
}

/// Asks GitHub about the newest release of `source`. `load` says whether an answer that
/// was kept from an earlier question will do.
QNetworkReply* askRelease(QNetworkAccessManager& network, const appimage::Source& source, QNetworkRequest::CacheLoadControl load) {
    auto req = request(appimage::latestReleaseUrl(source));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, load);
    auto* reply = network.get(req);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > MAX_RELEASE_BYTES) {
            reply->abort();
        }
    });
    return reply;
}

/// The release a finished question came to, or why there is none.
std::expected<appimage::Release, QString> readRelease(QNetworkReply* reply, const appimage::Source& source) {
    if (reply->error() != QNetworkReply::NoError) {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            return std::unexpected(AppImageClient::tr("There is no project %1 on GitHub, or it has not published a release.").arg(source.slug()));
        }
        if (status == 403 || status == 429) {
            return std::unexpected(AppImageClient::tr("GitHub answers only a limited number of questions per hour, and that limit is reached. Try again later."));
        }
        return std::unexpected(AppImageClient::tr("GitHub could not be reached."));
    }
    // an answer from the cache arrives in one piece, past the check while it comes in
    if (reply->bytesAvailable() <= MAX_RELEASE_BYTES) {
        if (auto release = appimage::parseRelease(reply->readAll())) {
            return *std::move(release);
        }
    }
    return std::unexpected(AppImageClient::tr("GitHub gave an answer about %1 that could not be read.").arg(source.slug()));
}

/// The file of the newest known release that takes the place of what `tracked` names;
/// null when that one is installed or nothing is known. `tag` gets the name of the release.
const appimage::Asset* newerAsset(const QHash<QString, appimage::Release>& releases, const appimage::Tracking& tracked, QString* tag = nullptr) {
    const auto release = releases.constFind(tracked.source);
    if (release == releases.cend()) {
        return nullptr;
    }
    // a file this app never downloaded has the kind another program's records suggest
    const auto file = appimage::choose(release->assets, tracked.variant, tracked.asset == 0);
    if (file < 0) {
        return nullptr;
    }
    const auto& asset = release->assets.at(file);
    // Versions say which is newer: a file uploaded again, or an older release that became
    // the latest, is no update. A release without one ("nightly") goes by the upload, and
    // by its name when another program downloaded the file and the upload is not known.
    const auto order = appimage::compareReleases(tracked.release, release->tag);
    const bool newer = order ? std::is_lt(*order)
                             : asset.id != tracked.asset && (tracked.asset != 0 || !appimage::sameRelease(tracked.release, release->tag));
    if (!newer) {
        return nullptr;
    }
    if (tag != nullptr) {
        *tag = release->tag;
    }
    return &asset;
}

}  // namespace

struct AppImageClient::Download {
    appimage::Asset asset;
    Fetched fetched;  ///< what install() is told about it
    QNetworkReply* reply = nullptr;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 received = 0;
    bool broken     = false;  ///< more than announced arrived, or it could not be written
    bool cancelled  = false;
};

AppImageClient::AppImageClient(QObject* parent) : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)) {
    // GitHub's answers are kept: asking again for one that did not change is cheap
    auto* cache = new QNetworkDiskCache(&m_network);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/github"_s);
    cache->setMaximumCacheSize(32 * 1024 * 1024);
    m_network.setCache(cache);

    connect(&m_watcher, &QFutureWatcher<AppImageResult>::finished, this, [this] {
        const auto result = m_watcher.result();
        setBusy(false);
        setMessage(result.error, result.done);
        m_icons.clear();  // an icon may have been replaced under the name it had
        refresh();
    });
}

AppImageClient::~AppImageClient() {
    if (m_download) {
        m_download->reply->disconnect(this);
        m_download->reply->abort();
        m_download->file.remove();
    }
    m_watcher.disconnect(this);
    m_cancel->store(true);  // stops a copy or an extraction that is running
    m_watcher.waitForFinished();
}

bool AppImageClient::tracking() const {
    return std::ranges::any_of(m_apps, [](const AppImageItem& app) { return !app.source.isEmpty(); });
}

int AppImageClient::updateCount() const {
    return static_cast<int>(std::ranges::count_if(m_apps, [](const AppImageItem& app) { return !app.update.isEmpty(); }));
}

QString AppImageClient::directory() const {
    return installDirectory();
}

void AppImageClient::setBusy(bool busy, const QString& subject) {
    m_busy    = busy;
    m_subject = subject;
    emit busyChanged();
}

void AppImageClient::setMessage(const QString& error, const QString& added) {
    m_error = error;
    m_added = added;
    emit messageChanged();
}

void AppImageClient::clearMessage() {
    setMessage({}, {});
}

AppImageCandidate AppImageClient::inspect(const QUrl& file) const {
    const auto path = file.isLocalFile() ? file.toLocalFile() : QString();
    const QFileInfo info(path);
    AppImageCandidate candidate{.url = file, .name = info.fileName(), .size = {}, .reason = {}, .ok = false};
    if (path.isEmpty() || !info.isFile()) {
        candidate.reason = tr("This is not a file on this computer.");
    } else if (!appimage::isAppImage(path)) {
        candidate.reason = tr("“%1” is not an AppImage.").arg(info.fileName());
    } else {
        candidate.ok   = true;
        candidate.size = QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeIecFormat);
    }
    return candidate;
}

void AppImageClient::add(const QUrl& file) {
    if (m_busy || !file.isLocalFile()) {
        return;
    }
    clearMessage();
    setBusy(true, file.fileName());
    m_watcher.setFuture(QtConcurrent::run(install, file.toLocalFile(), m_cancel, Fetched{.name = {}, .tracking = {}, .replaces = {}}));
}

bool AppImageClient::takeImportPrompt() {
    if (m_importPrompted || m_imports.isEmpty()) {
        return false;
    }
    m_importPrompted = true;
    return true;
}

QString AppImageClient::importTools() const {
    QStringList tools;
    for (const auto& app : m_imports) {
        tools << app.tool;
    }
    tools.removeDuplicates();
    return QLocale().createSeparatedList(tools);
}

void AppImageClient::migrateImports() {
    if (m_busy || m_imports.isEmpty()) {
        return;
    }
    clearMessage();
    setBusy(true, tr("%n AppImage(s) from %1", nullptr, static_cast<int>(m_imports.size())).arg(importTools()));
    QStringList shown;
    for (const auto& app : std::as_const(m_imports)) {
        shown << app.path;
    }
    m_watcher.setFuture(QtConcurrent::run(migrate, m_cancel, shown));
}

void AppImageClient::dismissImports() {
    // the programs the user was asked about, not one that has turned up since
    for (const auto& importer : IMPORTERS) {
        if (std::ranges::contains(m_imports, QString(importer.name), &AppImageImport::tool)) {
            QSettings().setValue(dismissedKey(importer), true);
        }
    }
    m_importStamp.clear();
    refresh();
}

void AppImageClient::lookUp(const QString& link) {
    clearOffer();
    const auto source = appimage::parseSource(link);
    if (!source) {
        m_offer.error = tr("This is not a link to a project on GitHub. One looks like https://github.com/owner/project.");
        emit offerChanged();
        return;
    }
    m_offeredSource = *source;
    m_lookup        = askRelease(m_network, *source, QNetworkRequest::PreferNetwork);
    connect(m_lookup, &QNetworkReply::finished, this, [this] {
        auto* reply = std::exchange(m_lookup, nullptr);
        reply->deleteLater();
        m_offer.source     = m_offeredSource.slug();
        const auto release = readRelease(reply, m_offeredSource);
        if (!release) {
            m_offer.error = release.error();
        } else {
            m_releases.insert(appimage::sourceValue(m_offeredSource), *release);
            m_offer.release = release->tag;
            m_offeredAssets = release->assets;
            for (const auto& asset : std::as_const(m_offeredAssets)) {
                m_offer.files << AppImageChoice{.name = asset.name,
                    .size                             = QLocale().formattedDataSize(asset.size, 1, QLocale::DataSizeIecFormat)};
            }
            // the file the link named, as far as the newest release still has one of that kind
            m_offer.preferred = static_cast<int>(std::max<qsizetype>(0, appimage::choose(m_offeredAssets, appimage::variant(m_offeredSource.file))));
            if (m_offeredAssets.isEmpty()) {
                m_offer.error = tr("The newest release of %1 (%2) has no AppImage file for this computer.").arg(m_offer.source, release->tag);
            }
        }
        emit offerChanged();
    });
    emit offerChanged();
}

void AppImageClient::clearOffer() {
    if (auto* reply = std::exchange(m_lookup, nullptr); reply != nullptr) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_offer = {};
    m_offeredAssets.clear();
    emit offerChanged();
}

void AppImageClient::addOffered(int file) {
    if (file >= 0 && file < m_offeredAssets.size()) {
        download(m_offeredSource, m_offeredAssets.at(file));
    }
}

void AppImageClient::update(const QString& id) {
    const auto desktopName = id + u".desktop"_s;
    const auto tracked     = appimage::tracking(appimage::mainGroup(readText(menuDirectory() + u'/' + desktopName, appimage::MAX_ENTRY_BYTES)));
    const auto source      = appimage::sourceFromValue(tracked.source);
    const auto* asset      = newerAsset(m_releases, tracked);
    if (!source || asset == nullptr) {
        refresh();  // what the list showed is out of date
        return;
    }
    download(*source, *asset, desktopName);
}

void AppImageClient::download(const appimage::Source& source, const appimage::Asset& asset, const QString& replaces) {
    if (m_busy) {
        return;
    }
    clearMessage();
    // what a crash left behind; the working copy of another window is younger than that
    const QDir folder(installDirectory());
    for (const auto& stale : folder.entryInfoList({u".cachyos-pi-*.part"_s}, QDir::Files | QDir::Hidden)) {
        if (stale.lastModified().daysTo(QDateTime::currentDateTime()) >= 1) {
            QFile::remove(stale.filePath());
        }
    }
    auto download     = std::make_unique<Download>();
    download->asset   = asset;
    download->fetched = {.name = asset.name,
        .tracking = {.source = appimage::sourceValue(source), .asset = asset.id, .variant = appimage::variant(asset.name),
            .release = m_releases.value(appimage::sourceValue(source)).tag},
        .replaces = replaces};
    // next to where it will end up: install() takes it over as its working copy
    download->file.setFileName(privatePath("part"_L1));
    if (!QDir().mkpath(installDirectory()) || !download->file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setMessage(tr("Could not write to %1.").arg(installDirectory()), {});
        return;
    }
    download->file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto req = request(asset.url);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    download->reply = m_network.get(req);
    m_download      = std::move(download);
    m_progress      = 0;
    emit progressChanged();
    connect(m_download->reply, &QNetworkReply::readyRead, this, [this] {
        auto& current    = *m_download;
        const auto chunk = current.reply->readAll();
        current.received += chunk.size();
        if (current.received > current.asset.size || current.file.write(chunk) != chunk.size()) {
            current.broken = true;
            current.reply->abort();  // ends in onDownloaded(), `current` is gone after it
            return;
        }
        if (!current.asset.sha256.isEmpty()) {
            current.hash.addData(chunk);
        }
        if (const auto percent = static_cast<int>(current.received * 100 / current.asset.size); percent != m_progress) {
            m_progress = percent;
            emit progressChanged();
        }
    });
    connect(m_download->reply, &QNetworkReply::finished, this, &AppImageClient::onDownloaded);
    setBusy(true, asset.name);
}

void AppImageClient::cancelDownload() {
    if (m_download) {
        m_download->cancelled = true;
        m_download->reply->abort();
    }
}

void AppImageClient::onDownloaded() {
    const auto download = std::move(m_download);
    download->reply->deleteLater();
    const auto path     = download->file.fileName();
    const bool complete = download->reply->error() == QNetworkReply::NoError && !download->broken && download->file.flush();
    download->file.close();
    QString error;
    if (download->cancelled) {
        // the user knows
    } else if (!complete) {
        error = tr("%1 could not be downloaded. Check the connection and the free space in %2.").arg(download->asset.name, installDirectory());
    } else if (download->received != download->asset.size
        || (!download->asset.sha256.isEmpty() && download->hash.result() != download->asset.sha256)) {
        error = tr("The file that arrived is not the one GitHub lists as %1, so it was not added.").arg(download->asset.name);
    }
    if (download->cancelled || !error.isEmpty()) {
        QFile::remove(path);
        setBusy(false);
        setMessage(error, {});
        return;
    }
    emit busyChanged();  // not downloading any more
    m_watcher.setFuture(QtConcurrent::run(install, path, m_cancel, download->fetched));
}

void AppImageClient::checkUpdates(bool now) {
    if (m_checks > 0) {
        return;
    }
    QHash<QString, appimage::Source> sources;
    const auto entries = managedEntries();
    for (const auto& keys : entries) {
        const auto value = appimage::tracking(keys).source;
        if (const auto source = appimage::sourceFromValue(value)) {
            sources.insert(value, *source);
        }
    }
    if (sources.isEmpty()) {
        return;
    }
    // Between two real looks the answers GitHub gave last time are read back from disk.
    const auto last  = QSettings().value(LAST_CHECK_KEY).toDateTime();
    const auto age   = last.secsTo(QDateTime::currentDateTimeUtc());
    const bool fresh = now || !last.isValid() || age < 0 || age > CHECK_INTERVAL;
    if (!fresh && std::ranges::all_of(sources.keys(), [this](const QString& value) { return m_releases.contains(value); })) {
        return;  // nothing the last answers do not tell already
    }
    m_checkError.clear();
    for (const auto& [value, source] : sources.asKeyValueRange()) {
        auto* reply = askRelease(m_network, source, fresh ? QNetworkRequest::PreferNetwork : QNetworkRequest::AlwaysCache);
        ++m_checks;
        connect(reply, &QNetworkReply::finished, this, [this, reply, value, source, fresh, now] {
            reply->deleteLater();
            if (const auto release = readRelease(reply, source)) {
                m_releases.insert(value, *release);
            } else if (fresh) {
                m_checkError = release.error();
            }
            if (--m_checks > 0) {
                return;
            }
            if (fresh) {
                // also after a failure: a project that is gone is not asked about on every start
                QSettings().setValue(LAST_CHECK_KEY, QDateTime::currentDateTimeUtc());
            }
            if (now && !m_busy) {
                setMessage(m_checkError, {});
            }
            refresh();
            emit checkingChanged();
        });
    }
    emit checkingChanged();
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
    const auto desktopName = id + u".desktop"_s;
    const auto current     = managedEntries().value(desktopName).value(appimage::PATH_KEY);
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
    removeMenuEntry(desktopName);
    refreshMenu();
    clearMessage();
    refresh();
}

void AppImageClient::launch(const QString& id) {
    const auto keys = managedEntries().value(id + u".desktop"_s);
    const auto path = keys.value(appimage::PATH_KEY);
    // started the way the menu entry does it: same arguments, nothing passed through a shell
    const auto command = appimage::commandLine(keys.value(u"Exec"_s));
    if (!ownedFile(path) || command.isEmpty() || command.first() != path || !QProcess::startDetached(path, command.mid(1), QDir::homePath())) {
        setMessage(tr("Could not start the application."), {});
    }
}

void AppImageClient::refresh() {
    const bool fuseAvailable = QFileInfo::exists(u"/usr/lib/libfuse.so.2"_s);

    QList<AppImageItem> apps;
    IconIndex icons;
    const auto entries = managedEntries();
    for (const auto& [entry, keys] : entries.asKeyValueRange()) {
        const auto id   = entry.chopped(8);  // without ".desktop"
        const auto path = keys.value(appimage::PATH_KEY);
        const QFileInfo file(path);
        const auto tracked = appimage::tracking(keys);
        const auto source  = appimage::sourceFromValue(tracked.source);
        QString update;
        if (source) {
            newerAsset(m_releases, tracked, &update);
        }
        apps << AppImageItem{
            .id      = id,
            .name    = keys.value(u"Name"_s),
            .summary = keys.value(u"Comment"_s),
            .version = keys.value(u"X-AppImage-Version"_s),
            .path    = path,
            .size    = file.exists() ? QLocale().formattedDataSize(file.size(), 1, QLocale::DataSizeIecFormat) : QString(),
            .missing = !file.exists(),
            // false for an entry that was edited to point somewhere else: its file is left alone
            .owned = ownedFile(path),
            // a command line tool: there is nothing to open without a terminal
            .terminal = keys.value(u"Terminal"_s) == "true"_L1,
            .source   = source ? source->slug() : QString(),
            .update   = update,
        };
        const auto icon = keys.value(u"Icon"_s);
        if (icon.startsWith(u'/')) {
            icons.insert(id, IconRef{.path = icon, .stock = {}});
        } else if (!icon.isEmpty()) {
            icons.insert(id, IconRef{.path = {}, .stock = icon});
        }
    }
    std::ranges::sort(apps, [](const AppImageItem& a, const AppImageItem& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    // every reload of the package list comes through here: nothing new, nothing to redraw
    if (icons != m_icons) {
        m_icons = std::move(icons);
        emit iconsLoaded(m_icons);
    }
    if (apps != m_apps || fuseAvailable != m_fuseAvailable) {
        m_apps          = std::move(apps);
        m_fuseAvailable = fuseAvailable;
        emit appsChanged();
    }

    // nothing is offered while files are being added; afterwards the list is made anew
    auto stamp = m_busy ? QList<QDateTime>{} : importStamp(m_imports);
    if (!stamp.isEmpty() && stamp == m_importStamp) {
        return;
    }
    m_importStamp = std::move(stamp);
    QList<AppImageImport> imports;
    if (!m_busy) {
        const auto offered = importable(entries);
        for (const auto& [importer, records] : offered) {
            for (const auto& app : records) {
                const auto source = appimage::sourceFromValue(app.tracking.source);
                QStringList detail{app.path, QLocale().formattedDataSize(QFileInfo(app.path).size(), 1, QLocale::DataSizeIecFormat)};
                if (offered.size() > 1) {
                    detail.prepend(importer->name);
                }
                if (source) {
                    detail << u"github.com/"_s + source->slug();
                }
                imports << AppImageImport{.name = app.title, .tool = importer->name, .detail = detail.join(u" · "_s), .path = app.path};
            }
        }
    }
    if (imports != m_imports) {
        m_imports = std::move(imports);
        emit importsChanged();
    }
}
