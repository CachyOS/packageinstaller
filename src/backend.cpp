// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "iconprovider.hpp"
#include "memory.hpp"
#include "process.hpp"

#include <QFile>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {
Backend* s_instance = nullptr;
}  // namespace

Backend::Backend(IconProvider* icons, QObject* parent)
  : QObject(parent), m_aur(&m_packages), m_icons(icons) {
    Q_ASSERT(s_instance == nullptr);
    s_instance       = this;
    m_aurEnabled     = m_settings.value(u"aur/enabled"_s, false).toBool();
    m_flatpakEnabled = m_settings.value(u"flatpak/enabled"_s, false).toBool();
    m_paruAvailable  = !QStandardPaths::findExecutable(u"paru"_s).isEmpty();

    connect(&m_transaction, &Transaction::finished, this, [this](bool) {
        // a Flatpak operation leaves the pacman databases alone
        if (m_transaction.isFlatpak()) {
            m_flatpak.refresh();
        } else {
            reload();
        }
    });
    connect(&m_flatpak, &FlatpakClient::iconsLoaded, this, [this](const IconIndex& index) { m_icons->setIndex(index, true); });
    m_checkUpdates.setProcessEnvironment(plainEnvironment());
    connect(&m_checkUpdates, &QProcess::finished, this, &Backend::onUpdatesChecked);

    // A transaction touches the database many times; reload once things have settled,
    // and leave it to the transaction itself while one of ours is running.
    m_dbChanged.setSingleShot(true);
    m_dbChanged.setInterval(1500);
    connect(&m_dbWatcher, &QFileSystemWatcher::directoryChanged, &m_dbChanged, qOverload<>(&QTimer::start));
    connect(&m_dbChanged, &QTimer::timeout, this, [this] {
        // a transaction of ours reloads by itself when it ends
        if (m_transaction.state() != Transaction::Running && m_transaction.state() != Transaction::Planning) {
            reload();
        }
    });

    QFile pkglist(u":/qt/qml/CachyOS/PackageInstaller/pkglist.yaml"_s);
    if (pkglist.open(QIODevice::ReadOnly)) {
        m_curatedList = catalog::loadCurated(pkglist);
    } else {
        qWarning("pkglist.yaml is missing from the resources");
    }
    reload();
}

Backend::~Backend() {
    // paru keeps building after the app is gone, so leave its checkouts in place
    if (m_transaction.state() == Transaction::Running) {
        m_aur.keepCheckouts();
    }
}

Backend* Backend::create(QQmlEngine* /*qmlEngine*/, QJSEngine* /*jsEngine*/) {
    Q_ASSERT(s_instance != nullptr);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

bool Backend::store(bool& member, bool value, const QString& key, void (Backend::*changed)()) {
    if (member == value) {
        return false;
    }
    member = value;
    m_settings.setValue(key, value);
    emit(this->*changed)();
    return true;
}

void Backend::setAurEnabled(bool enabled) {
    if (!store(m_aurEnabled, enabled, u"aur/enabled"_s, &Backend::aurEnabledChanged)) {
        return;
    }
    if (enabled) {
        m_aur.checkUpdates();
    } else {
        m_aur.clearUpdates();
    }
}

void Backend::setFlatpakEnabled(bool enabled) {
    if (!store(m_flatpakEnabled, enabled, u"flatpak/enabled"_s, &Backend::flatpakEnabledChanged)) {
        return;
    }
    if (enabled) {
        m_flatpak.refresh();
    } else {
        m_flatpak.clear();
    }
}

void Backend::installFlatpak(const QString& id) {
    if (m_flatpakEnabled) {
        m_transaction.prepareFlatpak(Transaction::FlatpakInstall, {m_flatpak.title(id)}, m_flatpak.installArguments(id));
    }
}

void Backend::removeFlatpak(const QString& key) {
    if (m_flatpakEnabled) {
        m_transaction.prepareFlatpak(Transaction::FlatpakRemove, {m_flatpak.installedTitle(key)}, m_flatpak.removeArguments(key),
            m_flatpak.cleanupArguments(key));
    }
}

void Backend::updateFlatpaks() {
    if (m_flatpakEnabled) {
        m_transaction.prepareFlatpak(Transaction::FlatpakUpdate, {}, {u"update"_s, u"--noninteractive"_s});
    }
}

void Backend::addFlathub() {
    if (m_flatpakEnabled && m_flatpak.available()) {
        m_transaction.prepareFlatpak(Transaction::FlatpakSetup, {u"Flathub"_s},
            {u"remote-add"_s, u"--system"_s, u"--if-not-exists"_s, u"flathub"_s, u"https://dl.flathub.org/repo/flathub.flatpakrepo"_s});
    }
}

void Backend::reload() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    QtConcurrent::run(catalog::load).then(this, [this](std::expected<catalog::LoadResult, QString> result) {
        onLoaded(std::move(result));
    });
}

void Backend::onLoaded(std::expected<catalog::LoadResult, QString> result) {
    if (result) {
        m_error = result->packages.isEmpty() ? tr("The pacman databases are empty. Refresh them with a system update.") : QString();
        if (m_dbWatcher.directories().isEmpty()) {
            m_dbWatcher.addPaths({result->dbPath + u"/local"_s, result->dbPath + u"/sync"_s});
        }
        m_updateNotice.setDatabase(result->dbPath, result->repos);
        m_icons->setIndex(std::move(result->icons));
        m_packages.reset(std::move(result->packages));
        applyKnownUpdates();
    } else {
        m_error = result.error();
        m_packages.reset({});
    }
    // the databases as libalpm parsed them, and the previous package list
    releaseFreedMemory();

    QList<CuratedSection> curated;
    for (const auto& section : std::as_const(m_curatedList.sections)) {
        CuratedSection shown{.title = section.title, .names = {}};
        for (const auto& name : section.names) {
            if (const auto* pkg = m_packages.find(name); pkg != nullptr && !pkg->foreign()) {
                shown.names << name;
            }
        }
        if (!shown.names.isEmpty()) {
            curated << shown;
        }
    }
    if (curated != m_curated) {
        m_curated = std::move(curated);
        emit curatedChanged();
    }
    m_aur.refreshInstalled();

    m_loading = false;
    ++m_revision;
    emit loadingChanged();
    emit revisionChanged();

    if (m_reloadQueued) {
        m_reloadQueued = false;
        reload();
        return;
    }
    // let the first frame through before starting processes and network requests
    QTimer::singleShot(0, this, &Backend::refreshSources);
}

void Backend::applyKnownUpdates() {
    // The sync databases only know what the last refresh saw. Until checkupdates has
    // answered again, its previous answer still holds for all that was not updated since.
    m_newVersions.removeIf([this](const std::pair<const QString&, QString&>& update) {
        const auto* pkg = m_packages.find(update.first);
        return pkg == nullptr || !pkg->installed() || alpmdb::vercmp(pkg->installedVersion, update.second) >= 0;
    });
    if (!m_newVersions.isEmpty()) {
        m_packages.applyUpdates(m_newVersions);
    }
}

void Backend::refreshSources() {
    const bool first = !std::exchange(m_loadedOnce, true);
    // paru may have been installed or removed, by this app or outside of it
    if (const bool paru = !QStandardPaths::findExecutable(u"paru"_s).isEmpty(); paru != m_paruAvailable) {
        m_paruAvailable = paru;
        emit paruAvailableChanged();
    }
    // also when the flatpak package itself was just installed or removed
    const bool flatpakChanged = m_flatpak.available() == QStandardPaths::findExecutable(u"flatpak"_s).isEmpty();
    if (m_flatpakEnabled && !m_flatpak.loading() && (first || flatpakChanged)) {
        m_flatpak.refresh();
    }
    // only after the opt-in: the names of the installed packages are sent to the AUR
    if (m_aurEnabled) {
        m_aur.checkUpdates();
    }
    // every package operation ends in a reload; the notices change far less often
    m_updateNotice.check(std::chrono::minutes(10));
    checkUpdates();
}

void Backend::checkUpdates() {
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkOutdated = true;
        return;
    }
    const auto program = QStandardPaths::findExecutable(u"checkupdates"_s);
    if (program.isEmpty()) {
        return;  // pacman-contrib is optional; the local sync databases are used as they are
    }
    m_checkingUpdates = true;
    emit checkingUpdatesChanged();
    m_checkUpdates.start(program, {u"--nocolor"_s});
}

void Backend::onUpdatesChecked(int exitCode, QProcess::ExitStatus status) {
    m_checkingUpdates = false;
    emit checkingUpdatesChanged();
    if (std::exchange(m_checkOutdated, false)) {
        // started before the last reload: its answer may name packages that were updated since
        if (!m_loading) {
            checkUpdates();
        }
        return;
    }
    // checkupdates: 0 = updates listed, 2 = no updates, anything else = failure (keep the local view)
    if (status != QProcess::NormalExit || (exitCode != 0 && exitCode != 2) || m_loading) {
        return;
    }
    QHash<QString, QString> versions;
    for (const auto& line : QString::fromUtf8(m_checkUpdates.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts)) {
        // "name 1.0-1 -> 1.1-1"
        if (const auto fields = line.split(u' ', Qt::SkipEmptyParts); fields.size() == 4) {
            versions.insert(fields.at(0), fields.at(3));
        }
    }
    m_packages.applyUpdates(versions);
    m_newVersions = std::move(versions);
    ++m_revision;
    emit revisionChanged();
}

Package Backend::package(const QString& name) const {
    const auto* pkg = m_packages.find(name);
    return pkg == nullptr ? Package{} : *pkg;
}

void Backend::install(const QString& name) {
    QStringList targets{name};
    for (const auto& companion : m_curatedList.companions.value(name)) {
        if (const auto* pkg = m_packages.find(companion); pkg != nullptr && !pkg->foreign() && !pkg->installed()) {
            targets << companion;
        }
    }
    m_transaction.prepare(Transaction::Install, targets);
}

void Backend::remove(const QString& name) {
    m_transaction.prepare(Transaction::Remove, {name});
}

void Backend::upgradeAll() {
    // a notice may have been published since the last look; the dialog waits for the answer
    m_updateNotice.check();
    m_transaction.prepare(Transaction::Upgrade, {});
}

void Backend::installFromAur(const QString& name) {
    installManyFromAur({name});
}

void Backend::installManyFromAur(const QStringList& names) {
    if (m_aurEnabled && m_paruAvailable && !names.isEmpty()) {
        const auto directories = m_aur.buildDirectories(names);
        if (!directories) {
            emit notice(directories.error());
            return;
        }
        m_transaction.prepareAur(names, *directories, m_aur.buildWorkingDirectory());
    }
}
