// SPDX-License-Identifier: GPL-3.0-or-later
#include "flatpakclient.hpp"

#include <AppStreamQt/bundle.h>
#include <AppStreamQt/component.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/metadata.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/release-list.h>
#include <AppStreamQt/release.h>

#include <QDateTime>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int MAX_RESULTS = 150;

using Cancel = std::shared_ptr<std::atomic_bool>;

QProcessEnvironment plainEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    return env;
}

/// Runs `flatpak` and returns its output split into rows of tab separated columns.
/// Gives up when `cancel` is set or after `timeout` milliseconds.
QList<QStringList> rows(const Cancel& cancel, const QString& program, const QStringList& arguments, int timeout = 30000) {
    QProcess process;
    process.setProcessEnvironment(plainEnvironment());
    process.start(program, arguments);
    for (int waited = 0; !process.waitForFinished(200); waited += 200) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        if (cancel->load() || waited >= timeout) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
    }
    QList<QStringList> result;
    const auto lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        result << line.split(QLatin1Char('\t'));
    }
    return result;
}

/// Flatpak keeps the catalogue of a system-wide remote in this directory once it was fetched.
QString catalogueOf(const QString& remote) {
    return QStringLiteral("/var/lib/flatpak/appstream/%1/%2/active").arg(remote, QSysInfo::currentCpuArchitecture());
}

/// Which of two offers of the same app is shown: Flathub before other remotes, the stable
/// branch before others, then by name so the choice never depends on the order of loading.
bool preferred(const FlatpakApp& a, const FlatpakApp& b) {
    const auto rank = [](const FlatpakApp& app) {
        return std::tuple(app.remote != QLatin1String("flathub"), app.branch != QLatin1String("stable"), app.remote, app.branch);
    };
    return rank(a) < rank(b);
}

/// Reads the catalogue of one system-wide remote. Only that directory is loaded, so a
/// remote of the same name in the user's own installation cannot contribute anything.
void loadCatalogue(FlatpakState& state, QHash<QString, qsizetype>& positions, const QString& remote) {
    AppStream::Pool pool;
    pool.setFlags(AppStream::Pool::FlagNone);
    pool.setLoadStdDataLocations(false);
    pool.addExtraDataLocation(catalogueOf(remote), AppStream::Metadata::FormatStyleCatalog);
    if (!pool.load()) {
        qWarning("AppStream (Flatpak, %s): %s", qPrintable(remote), qPrintable(pool.lastError()));
        return;
    }
    for (const auto& component : pool.components()) {
        if (component.kind() != AppStream::Component::KindDesktopApp) {
            continue;
        }
        // "app/<id>/<arch>/<branch>": the reference is what gets installed
        const auto parts = component.bundle(AppStream::Bundle::KindFlatpak).id().split(QLatin1Char('/'));
        if (parts.size() != 4 || parts.at(0) != QLatin1String("app")) {
            continue;
        }
        FlatpakApp app;
        app.id          = parts.at(1);
        app.ref         = parts.mid(1).join(QLatin1Char('/'));
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
        app.haystack = (app.title + QLatin1Char('\n') + app.id + QLatin1Char('\n') + app.summary + QLatin1Char('\n')
            + component.keywords().join(QLatin1Char(' '))).toLower();

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

FlatpakState load(const Cancel& cancel) {
    FlatpakState state;
    const auto program = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if (program.isEmpty()) {
        return state;
    }
    state.available = true;

    for (const auto& row : rows(cancel, program, {QStringLiteral("remotes"), QStringLiteral("--system"), QStringLiteral("--columns=name")})) {
        if (!row.at(0).trimmed().isEmpty()) {
            state.remotes << row.at(0).trimmed();
        }
    }

    // Fetch the catalogue when a remote has none yet or it is older than a day. Flatpak's
    // policy allows this without authentication. The first download takes minutes on a slow line.
    const auto stale = QDateTime::currentDateTime().addDays(-1);
    QHash<QString, qsizetype> positions;
    for (const auto& remote : std::as_const(state.remotes)) {
        const QFileInfo catalogue(catalogueOf(remote) + QStringLiteral("/appstream.xml.gz"));
        if (!catalogue.exists() || catalogue.lastModified() < stale) {
            rows(cancel, program, {QStringLiteral("update"), QStringLiteral("--appstream"), QStringLiteral("--system"),
                                      QStringLiteral("--noninteractive"), remote},
                600000);
        }
        if (cancel->load()) {
            return state;
        }
        if (QFileInfo::exists(catalogueOf(remote))) {
            loadCatalogue(state, positions, remote);
        }
    }
    std::ranges::sort(state.apps, [](const FlatpakApp& a, const FlatpakApp& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    // --all: locale and debug extensions are updated too, so they count as installed
    QSet<QString> apps;
    for (const auto& row : rows(cancel, program, {QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=ref,installation")})) {
        if (row.size() >= 2) {
            apps.insert(row.at(1) + QLatin1Char(':') + row.at(0));
        }
    }
    for (const auto& row : rows(cancel, program, {QStringLiteral("list"), QStringLiteral("--all"),
             QStringLiteral("--columns=ref,name,version,origin,installation,size")})) {
        if (row.size() < 6) {
            continue;
        }
        FlatpakInstalled entry{row.at(0), row.at(0).section(QLatin1Char('/'), 0, 0), row.at(1), row.at(2), row.at(3), row.at(4), row.at(5), false};
        entry.isApp = apps.contains(entry.key());
        state.installed << entry;
    }
    return state;
}

/// The option that makes flatpak work on one installation.
QString installationOption(const QString& installation) {
    if (installation == QLatin1String("user")) {
        return QStringLiteral("--user");
    }
    if (installation == QLatin1String("system")) {
        return QStringLiteral("--system");
    }
    return QStringLiteral("--installation=") + installation;
}

}  // namespace

FlatpakClient::FlatpakClient(QObject* parent) : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)) {
    connect(&m_watcher, &QFutureWatcher<FlatpakState>::finished, this, &FlatpakClient::onLoaded);
    m_checkUpdates.setProcessEnvironment(plainEnvironment());
    connect(&m_checkUpdates, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_checkOutdated) {
            // started before the installed apps were read again: its answer may name what was
            // updated or removed since
            m_checkOutdated = false;
            if (!m_loading) {
                checkUpdates();
            }
            emit updatesChanged();
            return;
        }
        m_updates.clear();
        if (status == QProcess::NormalExit && exitCode == 0 && !m_cleared) {
            const auto lines = QString::fromUtf8(m_checkUpdates.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const auto& line : lines) {
                const auto row = line.split(QLatin1Char('\t'));
                if (row.size() < 4) {
                    continue;
                }
                // the listing does not say which installation; an app counts if any copy of it is one
                const auto id  = row.at(0).section(QLatin1Char('/'), 0, 0);
                bool isApp     = false;
                QString before;
                for (const auto& entry : std::as_const(m_state.installed)) {
                    if (entry.ref == row.at(0) && entry.remote == row.at(3)) {
                        isApp  = isApp || entry.isApp;
                        before = entry.version;
                    }
                }
                const auto* app = isApp ? find(id) : nullptr;
                m_updates << QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("ref"), row.at(0)},
                    {QStringLiteral("title"), app != nullptr ? app->title : row.at(1)},
                    {QStringLiteral("version"), row.at(2)},
                    {QStringLiteral("installedVersion"), before},
                    {QStringLiteral("remote"), row.at(3)},
                    {QStringLiteral("isApp"), isApp},
                };
            }
        }
        emit updatesChanged();
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
    cancelLoad();
    m_watcher.waitForFinished();
}

void FlatpakClient::cancelLoad() {
    m_cancel->store(true);
}

void FlatpakClient::refresh() {
    m_cleared = false;
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkOutdated = true;
    }
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_watcher.setFuture(QtConcurrent::run(load, m_cancel));
}

void FlatpakClient::clear() {
    m_cleared = true;
    cancelLoad();  // also stops a catalogue download
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkOutdated = true;
        m_checkUpdates.kill();
    }
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
    if (m_cleared || m_cancel->load()) {  // turned off while the catalogue was being read: the result is partial
        const bool again = !m_cleared && m_reloadQueued;
        m_reloadQueued   = false;
        emit loadingChanged();
        if (again) {
            refresh();
        }
        return;
    }
    m_state = m_watcher.result();
    m_index.clear();
    for (qsizetype i = 0; i < m_state.apps.size(); ++i) {
        m_index.insert(m_state.apps.at(i).id, i);
    }
    m_installed.clear();
    for (const auto& entry : std::as_const(m_state.installed)) {
        if (entry.isApp) {
            m_installed << toMap(find(entry.id), &entry);
        }
    }
    emit iconsLoaded(m_state.icons);
    ++m_revision;
    emit loadingChanged();
    emit changed();
    search(m_query);

    // the flatpak package may have been installed or removed while this was running
    const bool present = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
    if (m_reloadQueued || present != m_state.available) {
        m_reloadQueued = false;
        refresh();
        return;
    }
    checkUpdates();
}

void FlatpakClient::checkUpdates() {
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkOutdated = true;  // asked again once it has finished
        return;
    }
    if (!m_state.available || m_state.installed.isEmpty()) {
        if (!m_updates.isEmpty()) {
            m_updates.clear();
            emit updatesChanged();
        }
        return;
    }
    m_checkUpdates.start(QStringLiteral("flatpak"), {QStringLiteral("remote-ls"), QStringLiteral("--updates"), QStringLiteral("--all"),
                                                        QStringLiteral("--columns=ref,name,version,origin")});
    emit updatesChanged();
}

void FlatpakClient::search(const QString& query) {
    m_query = query.trimmed().toLower();
    m_results.clear();
    if (m_query.size() >= 2) {
        const auto words = m_query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QVariantList rest;
        for (const auto& app : std::as_const(m_state.apps)) {
            const bool matches = std::ranges::all_of(words, [&app](const QString& word) { return app.haystack.contains(word); });
            if (!matches) {
                continue;
            }
            // apps whose name starts with the query come first
            (app.title.startsWith(m_query, Qt::CaseInsensitive) ? m_results : rest) << toMap(&app, bestCopy(app.id));
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
        const int score = (app != nullptr && entry.ref == app->ref ? 2 : 0) + (entry.installation == QLatin1String("system") ? 1 : 0);
        if (score > bestScore) {
            best      = &entry;
            bestScore = score;
        }
    }
    return best;
}

QVariantMap FlatpakClient::toMap(const FlatpakApp* app, const FlatpakInstalled* copy) const {
    QVariantMap map{
        {QStringLiteral("key"), QString()},
        {QStringLiteral("summary"), QString()},
        {QStringLiteral("available"), app != nullptr},
        {QStringLiteral("installed"), copy != nullptr},
    };
    if (app != nullptr) {
        map.insert(QStringLiteral("id"), app->id);
        map.insert(QStringLiteral("ref"), app->ref);
        map.insert(QStringLiteral("title"), app->title);
        map.insert(QStringLiteral("summary"), app->summary);
        map.insert(QStringLiteral("description"), app->description);
        map.insert(QStringLiteral("developer"), app->developer);
        map.insert(QStringLiteral("homepage"), app->homepage);
        map.insert(QStringLiteral("license"), app->license);
        map.insert(QStringLiteral("version"), app->version);
        map.insert(QStringLiteral("remote"), app->remote);
        map.insert(QStringLiteral("screenshots"), app->screenshots);
    }
    if (copy != nullptr) {
        map.insert(QStringLiteral("id"), copy->id);
        map.insert(QStringLiteral("key"), copy->key());
        map.insert(QStringLiteral("ref"), copy->ref);
        if (app == nullptr) {  // installed from a remote without a catalogue, or from a file
            map.insert(QStringLiteral("title"), copy->name.isEmpty() ? copy->id : copy->name);
            map.insert(QStringLiteral("remote"), copy->remote);
        }
        map.insert(QStringLiteral("installedVersion"), copy->version);
        map.insert(QStringLiteral("installedFrom"), copy->remote);
        map.insert(QStringLiteral("installation"), copy->installation);
        map.insert(QStringLiteral("branch"), copy->ref.section(QLatin1Char('/'), 2, 2));
        map.insert(QStringLiteral("size"), copy->size);
    }
    return map;
}

QVariantMap FlatpakClient::app(const QString& id, const QString& key) const {
    const auto* app  = find(id);
    const auto* copy = key.isEmpty() ? nullptr : findCopy(key);
    if (copy == nullptr) {  // none asked for, or it has just been removed
        copy = bestCopy(id);
    }
    return app == nullptr && copy == nullptr ? QVariantMap{} : toMap(app, copy);
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
    return {QStringLiteral("install"), QStringLiteral("--system"), QStringLiteral("--noninteractive"), app->remote,
        QStringLiteral("app/") + app->ref};
}

QStringList FlatpakClient::removeArguments(const QString& key) const {
    const auto* copy = findCopy(key);
    if (copy == nullptr) {
        return {};
    }
    return {QStringLiteral("uninstall"), QStringLiteral("--noninteractive"), installationOption(copy->installation),
        QStringLiteral("app/") + copy->ref};
}
