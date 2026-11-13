// SPDX-License-Identifier: GPL-3.0-or-later
#include "flatpakclient.hpp"
#include "memory.hpp"
#include "process.hpp"

#include <AppStreamQt/bundle.h>
#include <AppStreamQt/component.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/metadata.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/release-list.h>
#include <AppStreamQt/release.h>

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QPromise>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype MAX_RESULTS = 150;

using Load = QPromise<FlatpakState>;

/// Runs `flatpak` and returns its output split into rows of tab separated columns.
/// Gives up when the load is cancelled or after `timeout` milliseconds.
QList<QStringList> rows(const Load& load, const QString& program, const QStringList& arguments, int timeout = 30000) {
    QList<QStringList> result;
    for (const auto& line : outputLines(program, arguments, timeout, [&] { return load.isCanceled(); })) {
        result << line.split(u'\t');
    }
    return result;
}

/// Flatpak keeps the catalogue of a system-wide remote in this directory once it was fetched.
QString catalogueOf(const QString& remote, const QString& arch) {
    return u"/var/lib/flatpak/appstream/%1/%2/active"_s.arg(remote, arch);
}

/// Which of two offers of the same app is shown: Flathub before other remotes, the stable
/// branch before others, then by name so the choice never depends on the order of loading.
bool preferred(const FlatpakApp& a, const FlatpakApp& b) {
    const auto rank = [](const FlatpakApp& app) {
        return std::tuple(app.remote != "flathub"_L1, app.branch != "stable"_L1, app.remote, app.branch);
    };
    return rank(a) < rank(b);
}

/// Reads the catalogue of one system-wide remote. Only that directory is loaded, so a
/// remote of the same name in the user's own installation cannot contribute anything.
void loadCatalogue(FlatpakState& state, QHash<QString, qsizetype>& positions, const QString& remote, const QString& arch) {
    AppStream::Pool pool;
    pool.setFlags(AppStream::Pool::FlagNone);
    pool.setLoadStdDataLocations(false);
    pool.addExtraDataLocation(catalogueOf(remote, arch), AppStream::Metadata::FormatStyleCatalog);
    if (!pool.load()) {
        qWarning("AppStream (Flatpak, %s): %s", qPrintable(remote), qPrintable(pool.lastError()));
        return;
    }
    for (const auto& component : pool.components()) {
        if (component.kind() != AppStream::Component::KindDesktopApp) {
            continue;
        }
        // "app/<id>/<arch>/<branch>": the reference is what gets installed
        const auto parts = component.bundle(AppStream::Bundle::KindFlatpak).id().split(u'/');
        if (parts.size() != 4 || parts.at(0) != "app"_L1) {
            continue;
        }
        FlatpakApp app;
        app.id          = parts.at(1);
        app.ref         = parts.sliced(1).join(u'/');
        app.branch      = parts.at(3);
        app.remote      = remote;
        app.title       = component.name();
        app.summary     = component.summary();
        app.description = component.description();
        app.developer   = component.developer().name();
        app.homepage    = component.url(AppStream::Component::UrlKindHomepage).toString();
        app.license     = component.projectLicense();
        app.screenshots = catalog::screenshotUrls(component);
        if (const auto releases = component.releasesPlain().entries(); !releases.isEmpty()) {
            app.version = releases.first().version();
        }
        app.haystack = (app.title + u'\n' + app.id + u'\n' + app.summary + u'\n' + component.keywords().join(u' ')).toLower();

        const auto known = positions.constFind(app.id);
        if (known != positions.cend() && !preferred(app, state.apps.at(*known))) {
            continue;
        }
        if (const auto icon = catalog::iconOf(component); !icon.path.isEmpty() || !icon.stock.isEmpty()) {
            state.icons.insert(app.id, icon);
        } else {
            state.icons.remove(app.id);
        }
        if (known != positions.cend()) {
            state.apps[*known] = app;
        } else {
            positions.insert(app.id, state.apps.size());
            state.apps << app;
        }
    }
}

FlatpakState load(const Load& load) {
    FlatpakState state;
    const auto& program = state.program = QStandardPaths::findExecutable(u"flatpak"_s);
    if (program.isEmpty()) {
        return state;
    }
    // QSysInfo says "arm64" where Flatpak says "aarch64"
    static const auto arch = [&] {
        const auto out = rows(load, program, {u"--default-arch"_s}, 5000);
        return out.isEmpty() ? QSysInfo::currentCpuArchitecture().replace("arm64"_L1, "aarch64"_L1) : out.first().first().trimmed();
    }();

    for (const auto& row : rows(load, program, {u"remotes"_s, u"--system"_s, u"--columns=name"_s})) {
        if (const auto remote = row.first().trimmed(); !remote.isEmpty()) {
            state.remotes << remote;
        }
    }

    // Fetch the catalogue when a remote has none yet or it is older than a day. Flatpak's
    // policy allows this without authentication. The first download takes minutes on a slow line.
    const auto stale = QDateTime::currentDateTime().addDays(-1);
    QHash<QString, qsizetype> positions;
    for (const auto& remote : std::as_const(state.remotes)) {
        const QFileInfo catalogue(catalogueOf(remote, arch) + u"/appstream.xml.gz"_s);
        if (!catalogue.exists() || catalogue.lastModified() < stale) {
            rows(load, program, {u"update"_s, u"--appstream"_s, u"--system"_s, u"--noninteractive"_s, remote}, 600000);
        }
        if (load.isCanceled()) {
            return state;
        }
        if (QFileInfo::exists(catalogueOf(remote, arch))) {
            loadCatalogue(state, positions, remote, arch);
        }
    }
    std::ranges::sort(state.apps, [](const FlatpakApp& a, const FlatpakApp& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    // --all: locale and debug extensions are updated too, so they count as installed
    QSet<QString> apps;
    for (const auto& row : rows(load, program, {u"list"_s, u"--app"_s, u"--columns=ref,installation"_s})) {
        if (row.size() >= 2) {
            apps.insert(row.at(1) + u':' + row.at(0));
        }
    }
    for (const auto& row : rows(load, program, {u"list"_s, u"--all"_s, u"--columns=ref,name,version,origin,installation,size"_s})) {
        if (row.size() < 6) {
            continue;
        }
        FlatpakInstalled entry{row.at(0), row.at(0).section(u'/', 0, 0), row.at(1), row.at(2), row.at(3), row.at(4), row.at(5), false};
        entry.isApp = apps.contains(entry.key());
        state.installed << entry;
    }
    return state;
}

/// The option that makes flatpak work on one installation.
QString installationOption(const QString& installation) {
    if (installation == "user"_L1 || installation == "system"_L1) {
        return u"--"_s + installation;
    }
    return u"--installation="_s + installation;
}

}  // namespace

FlatpakClient::FlatpakClient(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<FlatpakState>::finished, this, &FlatpakClient::onLoaded);
    m_checkUpdates.setProcessEnvironment(plainEnvironment());
    connect(&m_checkUpdates, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_checkGeneration != m_generation) {
            // started before the installed apps were read again or cleared: its answer may name
            // what was updated or removed since
            if (!m_loading) {
                checkUpdates();
            }
            emit updatesChanged();
            return;
        }
        m_updates.clear();
        const auto lines = status == QProcess::NormalExit && exitCode == 0
            ? QString::fromUtf8(m_checkUpdates.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts)
            : QStringList();
        // the listing does not say which installation; an app counts if any copy of it is one
        QHash<std::pair<QString, QString>, std::pair<bool, QString>> copies;  // ref and remote: isApp and version
        for (const auto& entry : std::as_const(m_state.installed)) {
            auto& [isApp, version] = copies[{entry.ref, entry.remote}];
            isApp                  = isApp || entry.isApp;
            version                = entry.version;
        }
        for (const auto& line : lines) {
            const auto row = line.split(u'\t');
            if (row.size() < 4) {
                continue;
            }
            const auto id              = row.at(0).section(u'/', 0, 0);
            const auto [isApp, before] = copies.value({row.at(0), row.at(3)});
            const auto* app = isApp ? find(id) : nullptr;
            FlatpakItem update;
            update.id               = id;
            update.ref              = row.at(0);
            update.title            = app != nullptr ? app->title : row.at(1);
            update.version          = row.at(2);
            update.installedVersion = before;
            update.remote           = row.at(3);
            update.isApp            = isApp;
            m_updates << update;
        }
        emit updatesChanged();
        rebuild();
    });
    connect(&m_checkUpdates, &QProcess::errorOccurred, this, [this] { emit updatesChanged(); });
}

FlatpakClient::~FlatpakClient() {
    // Stop both while every member is still alive: a process that is destroyed while running
    // reports "finished" from its destructor, and the worker thread would be waited for at exit.
    m_checkUpdates.disconnect(this);
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkUpdates.kill();
        m_checkUpdates.waitForFinished(2000);
    }
    m_watcher.disconnect(this);
    m_watcher.cancel();
    m_watcher.waitForFinished();
}

void FlatpakClient::refresh() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    ++m_generation;
    m_loading = true;
    emit loadingChanged();
    m_watcher.setFuture(QtConcurrent::run([](Load& promise) { promise.addResult(load(promise)); }));
}

void FlatpakClient::clear() {
    ++m_generation;
    m_watcher.cancel();  // also stops a catalogue download
    m_reloadQueued = false;
    m_checkUpdates.kill();
    m_state = {};
    m_index.clear();
    m_results.clear();
    m_installed.clear();
    m_updates.clear();
    ++m_revision;
    emit changed();
    emit resultsChanged();
    emit updatesChanged();
}

void FlatpakClient::onLoaded() {
    m_loading = false;
    releaseFreedMemory();  // the catalogue as AppStream parsed it
    if (m_watcher.isCanceled()) {  // cleared while the catalogue was being read: the result is partial
        emit loadingChanged();
        if (std::exchange(m_reloadQueued, false)) {
            refresh();
        }
        return;
    }
    m_state = m_watcher.result();
    m_index.clear();
    for (qsizetype i = 0; i < m_state.apps.size(); ++i) {
        m_index.insert(m_state.apps.at(i).id, i);
    }
    emit iconsLoaded(m_state.icons);
    ++m_revision;
    emit loadingChanged();
    rebuild();

    // the flatpak package may have been installed or removed while this was running
    const bool present = !QStandardPaths::findExecutable(u"flatpak"_s).isEmpty();
    if (std::exchange(m_reloadQueued, false) || present != available()) {
        refresh();
        return;
    }
    checkUpdates();
}

void FlatpakClient::checkUpdates() {
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        return;  // stale by now, so it asks again once it has finished
    }
    if (!available() || m_state.installed.isEmpty()) {
        if (!m_updates.isEmpty()) {
            m_updates.clear();
            emit updatesChanged();
            rebuild();
        }
        return;
    }
    m_checkGeneration = m_generation;
    m_checkUpdates.start(m_state.program, {u"remote-ls"_s, u"--updates"_s, u"--all"_s, u"--columns=ref,name,version,origin"_s});
    emit updatesChanged();
}

void FlatpakClient::rebuild() {
    m_installed.clear();
    for (const auto& entry : std::as_const(m_state.installed)) {
        if (entry.isApp) {
            m_installed << toItem(find(entry.id), &entry);
        }
    }
    emit changed();
    search(m_query);
}

int FlatpakClient::runtimeUpdates() const {
    return static_cast<int>(std::ranges::count(m_updates, false, &FlatpakItem::isApp));
}

void FlatpakClient::search(const QString& query) {
    m_query = query.trimmed().toLower();
    m_results.clear();
    if (m_query.size() >= 2) {
        const auto words = m_query.split(u' ', Qt::SkipEmptyParts);
        QList<FlatpakItem> rest;
        for (const auto& app : std::as_const(m_state.apps)) {
            const bool matches = std::ranges::all_of(words, [&app](const QString& word) { return app.haystack.contains(word); });
            if (!matches) {
                continue;
            }
            // apps whose name starts with the query come first
            (app.title.startsWith(m_query, Qt::CaseInsensitive) ? m_results : rest) << toItem(&app, bestCopy(app.id));
            if (m_results.size() + rest.size() >= MAX_RESULTS) {
                break;
            }
        }
        m_results += rest;
    }
    emit resultsChanged();
}

const FlatpakApp* FlatpakClient::find(const QString& id) const {
    const auto found = m_index.constFind(id);
    return found == m_index.cend() ? nullptr : &m_state.apps.at(*found);
}

const FlatpakInstalled* FlatpakClient::findCopy(const QString& key) const {
    for (const auto& entry : m_state.installed) {
        if (entry.isApp && entry.key() == key) {
            return &entry;
        }
    }
    return nullptr;
}

const FlatpakInstalled* FlatpakClient::bestCopy(const QString& id) const {
    const auto* app = find(id);
    const FlatpakInstalled* best = nullptr;
    int bestScore                = -1;
    for (const auto& entry : m_state.installed) {
        if (!entry.isApp || entry.id != id) {
            continue;
        }
        // the copy the catalogue entry stands for: same branch, system-wide
        const int score = (app != nullptr && entry.ref == app->ref ? 2 : 0) + (entry.installation == "system"_L1 ? 1 : 0);
        if (score > bestScore) {
            best      = &entry;
            bestScore = score;
        }
    }
    return best;
}

FlatpakItem FlatpakClient::toItem(const FlatpakApp* app, const FlatpakInstalled* copy) const {
    FlatpakItem item;
    if (app != nullptr) {
        static_cast<FlatpakApp&>(item) = *app;
    }
    item.available = app != nullptr;
    item.installed = copy != nullptr;
    if (copy != nullptr) {
        item.id  = copy->id;
        item.key = copy->key();
        item.ref = copy->ref;
        if (app == nullptr) {  // installed from a remote without a catalogue, or from a file
            item.title  = copy->name.isEmpty() ? copy->id : copy->name;
            item.remote = copy->remote;
        }
        item.installedVersion = copy->version;
        item.installedFrom    = copy->remote;
        item.installation     = copy->installation;
        item.branch           = copy->ref.section(u'/', 2, 2);
        item.size             = copy->size;
        if (const auto update = std::ranges::find(m_updates, copy->ref, &FlatpakItem::ref); update != m_updates.cend()) {
            item.upgradable = true;
            item.newVersion = update->version;
        }
    }
    return item;
}

FlatpakItem FlatpakClient::app(const QString& id, const QString& key) const {
    const auto* app  = find(id);
    const auto* copy = key.isEmpty() ? nullptr : findCopy(key);
    if (copy == nullptr) {  // none asked for, or it has just been removed
        copy = bestCopy(id);
    }
    return app == nullptr && copy == nullptr ? FlatpakItem{} : toItem(app, copy);
}

QString FlatpakClient::title(const QString& id) const {
    const auto* found = find(id);
    return found != nullptr ? found->title : id;
}

QString FlatpakClient::installedTitle(const QString& key) const {
    const auto* copy = findCopy(key);
    if (copy == nullptr) {
        return {};
    }
    const auto* found = find(copy->id);
    return found != nullptr ? found->title : copy->name.isEmpty() ? copy->id : copy->name;
}

QStringList FlatpakClient::installArguments(const QString& id) const {
    const auto* app = find(id);
    if (app == nullptr) {
        return {};
    }
    return {u"install"_s, u"--system"_s, u"--noninteractive"_s, app->remote, u"app/"_s + app->ref};
}

QStringList FlatpakClient::removeArguments(const QString& key) const {
    const auto* copy = findCopy(key);
    if (copy == nullptr) {
        return {};
    }
    return {u"uninstall"_s, u"--noninteractive"_s, installationOption(copy->installation), u"app/"_s + copy->ref};
}
