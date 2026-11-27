// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpakclient.hpp"
#include "memory.hpp"
#include "process.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

constexpr qsizetype MAX_RESULTS = 150;
/// The catalog carries every icon; a few MB is normal.
constexpr qsizetype MAX_OUTPUT = 64 * 1024 * 1024;

using Load = QPromise<CpakState>;

/// Runs `cpak`, leaving out what it says on standard error (update notices). Empty when it
/// failed, took longer than `timeout` or the load was cancelled.
QByteArray run(const Load& load, const QString& program, const QStringList& arguments, std::chrono::seconds timeout) {
    QProcess process;
    process.setProcessEnvironment(plainEnvironment());
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, arguments);
    QByteArray output;
    QElapsedTimer spent;
    spent.start();
    while (!process.waitForFinished(200)) {
        output += process.readAllStandardOutput();
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        if (load.isCanceled() || spent.elapsed() > std::chrono::milliseconds(timeout).count() || output.size() > MAX_OUTPUT) {
            process.kill();
            process.waitForFinished(2000);
            return {};
        }
    }
    output += process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && output.size() <= MAX_OUTPUT ? output : QByteArray();
}

/// Writes an icon of an app and returns where, or nothing.
QString writeIcon(const QDir& directory, const QString& origin, const QByteArray& data, const QString& extension) {
    if (data.isEmpty()) {
        return {};
    }
    const auto hash = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha1).toHex();
    const auto path = directory.filePath(QString::fromLatin1(hash) + u'.' + extension);
    if (QFile file(path); file.open(QIODevice::ReadOnly) && file.readAll() == data) {
        return path;  // unchanged, and the icon cache stays valid
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return {};
    }
    return path;
}

CpakState loadAll(const Load& load) {
    CpakState state;
    state.program = QStandardPaths::findExecutable(u"cpak"_s);
    if (state.program.isEmpty()) {
        return state;
    }
    const auto output = run(load, state.program, {u"discover"_s, u"list"_s}, 120s);
    if (output.isEmpty()) {
        return state;
    }
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/cpak-icons"_s;
    QDir().mkpath(cache);
    state.catalog = cpak::parseCatalog(output, cache);
    // Not fatal when it fails: the catalog alone still says what it installed.
    cpak::mergeInstalled(state.catalog, cpak::parseInstalled(run(load, state.program, {u"list"_s, u"--json"_s}, 60s)));
    return state;
}

}  // namespace

namespace cpak {

Catalog parseCatalog(const QByteArray& json, const QString& iconDirectory) {
    Catalog catalog;
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return catalog;
    }
    const QDir directory(iconDirectory);
    for (const auto& entry : document.object().value("packages"_L1).toArray()) {
        const auto object = entry.toObject();
        CpakItem item;
        item.origin = object.value("origin"_L1).toString();
        if (item.origin.isEmpty()) {
            continue;
        }
        item.title            = object.value("name"_L1).toString(item.origin.section(u'/', -1));
        item.summary          = object.value("description"_L1).toString();
        item.version          = object.value("available_version"_L1).toString();
        item.installedVersion = object.value("installed_version"_L1).toString();
        item.available        = true;
        item.installed        = object.value("installed"_L1).toBool();
        item.installable      = object.value("installable"_L1).toBool();
        for (const auto& permission : object.value("permissions"_L1).toArray()) {
            const auto fields = permission.toObject();
            const auto name   = fields.value("name"_L1).toString();
            const auto detail = fields.value("detail"_L1).toString();
            if (!name.isEmpty()) {
                item.permissions << (detail.isEmpty() ? name : name + u": "_s + detail);
            }
        }
        item.haystack = (item.title + u'\n' + item.origin + u'\n' + item.summary).toLower();

        // an icon that is there but cannot be stored still leaves the generated tile
        QString icon;
        if (!iconDirectory.isEmpty()) {
            icon = writeIcon(directory, item.origin, object.value("icon_svg"_L1).toString().toUtf8(), u"svg"_s);
            if (icon.isEmpty()) {
                icon = writeIcon(directory, item.origin, QByteArray::fromBase64(object.value("icon_png"_L1).toString().toLatin1()), u"png"_s);
            }
        }
        if (!icon.isEmpty()) {
            catalog.icons.insert(item.origin, IconRef{icon, {}});
        }
        catalog.items << item;
    }
    return catalog;
}

QList<CpakItem> parseInstalled(const QByteArray& json) {
    QList<CpakItem> found;
    for (const auto& entry : QJsonDocument::fromJson(json).array()) {
        const auto object = entry.toObject();
        // runtimes and extensions come with an app and go with it
        if (object.value("pulled_in"_L1).toBool()) {
            continue;
        }
        CpakItem item;
        item.origin = object.value("origin"_L1).toString();
        if (item.origin.isEmpty()) {
            continue;
        }
        item.title            = object.value("name"_L1).toString(item.origin.section(u'/', -1));
        item.installedVersion = object.value("version"_L1).toString();
        item.installed        = true;
        // "flatpak/flathub/pm.mirko.atoms": the name is the id as the remote spells it
        if (item.origin.startsWith("flatpak/"_L1)) {
            item.flatpakId = item.title;
            // "pm.mirko.Atoms": until the Flatpak catalog gives the real name, the last part has to do
            item.title = item.flatpakId.section(u'.', -1);
        }
        item.haystack = (item.title + u'\n' + item.origin).toLower();
        found << item;
    }
    return found;
}

void mergeInstalled(Catalog& catalog, const QList<CpakItem>& installed) {
    QSet<QString> listed;
    for (const auto& item : std::as_const(catalog.items)) {
        listed.insert(item.origin);
    }
    for (const auto& item : installed) {
        if (!listed.contains(item.origin)) {
            catalog.items << item;
        }
    }
}

QHash<QString, QString> parseUpdates(const QByteArray& json) {
    QHash<QString, QString> updates;
    for (const auto& entry : QJsonDocument::fromJson(json).array()) {
        const auto object = entry.toObject();
        // "available" is an update that can be applied; "pinned" or a failure is not
        if (object.value("status"_L1).toString() == "available"_L1) {
            updates.insert(object.value("origin"_L1).toString(), object.value("new_version"_L1).toString());
        }
    }
    return updates;
}

}  // namespace cpak

CpakClient::CpakClient(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<CpakState>::finished, this, &CpakClient::onLoaded);
    m_checkUpdates.setProcessEnvironment(plainEnvironment());
    m_checkUpdates.setStandardErrorFile(QProcess::nullDevice());
    connect(&m_checkUpdates, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_checkGeneration != m_generation) {
            // started before the catalog was read again or cleared
            if (!m_loading) {
                checkUpdates();
            }
            emit updatesChanged();
            return;
        }
        m_updates.clear();
        if (status == QProcess::NormalExit && exitCode == 0) {
            m_updates = cpak::parseUpdates(m_checkUpdates.readAllStandardOutput());
        }
        emit updatesChanged();
        rebuild();
    });
    connect(&m_checkUpdates, &QProcess::errorOccurred, this, [this] { emit updatesChanged(); });
}

CpakClient::~CpakClient() {
    m_checkUpdates.disconnect(this);
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkUpdates.kill();
        m_checkUpdates.waitForFinished(2000);
    }
    m_watcher.disconnect(this);
    m_watcher.cancel();
    m_watcher.waitForFinished();
}

int CpakClient::updateCount() const {
    return static_cast<int>(std::ranges::count_if(m_installed, &CpakItem::upgradable));
}

void CpakClient::refresh() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    ++m_generation;
    m_loading = true;
    emit loadingChanged();
    m_watcher.setFuture(QtConcurrent::run([](Load& promise) { promise.addResult(loadAll(promise)); }));
}

void CpakClient::clear() {
    ++m_generation;
    m_watcher.cancel();
    m_reloadQueued = false;
    m_checkUpdates.kill();
    m_program.clear();
    m_items.clear();
    m_index.clear();
    m_updates.clear();
    m_results.clear();
    m_installed.clear();
    m_failed = false;
    ++m_revision;
    emit changed();
    emit resultsChanged();
    emit updatesChanged();
}

void CpakClient::onLoaded() {
    m_loading = false;
    releaseFreedMemory();  // the catalog as JSON
    if (m_watcher.isCanceled()) {
        emit loadingChanged();
        if (std::exchange(m_reloadQueued, false)) {
            refresh();
        }
        return;
    }
    auto state    = m_watcher.result();
    auto& catalog = state.catalog;
    m_program     = state.program;
    m_failed      = !m_program.isEmpty() && catalog.items.isEmpty();
    m_items       = std::move(catalog.items);
    m_index.clear();
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        m_index.insert(m_items.at(i).origin, i);
    }
    emit iconsLoaded(catalog.icons);
    ++m_revision;
    emit loadingChanged();
    rebuild();

    // the cpak package may have been installed or removed while this was running
    const bool present = !QStandardPaths::findExecutable(u"cpak"_s).isEmpty();
    if (std::exchange(m_reloadQueued, false) || present != available()) {
        refresh();
        return;
    }
    checkUpdates();
}

void CpakClient::checkUpdates() {
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        return;  // stale by now, so it asks again once it has finished
    }
    const bool any = std::ranges::any_of(m_items, &CpakItem::installed);
    if (!available() || !any) {
        if (!m_updates.isEmpty()) {
            m_updates.clear();
            emit updatesChanged();
            rebuild();
        }
        return;
    }
    m_checkGeneration = m_generation;
    m_checkUpdates.start(m_program, {u"update"_s, u"--check"_s, u"--json"_s});
    emit updatesChanged();
}

void CpakClient::rebuild() {
    for (auto& item : m_items) {
        const auto update = m_updates.constFind(item.origin);
        item.upgradable   = item.installed && update != m_updates.cend();
        item.newVersion   = item.upgradable ? *update : QString();
    }
    m_installed.clear();
    for (const auto& item : std::as_const(m_items)) {
        if (item.installed) {
            m_installed << item;
        }
    }
    std::ranges::stable_sort(m_installed, [](const CpakItem& a, const CpakItem& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    emit changed();
    emit updatesChanged();
    search(m_query);
}

QString CpakClient::normalizeQuery(const QString& query) {
    auto text  = query.trimmed();
    auto words = text.split(u' ', Qt::SkipEmptyParts);
    // a command line copied from a store page: the repository is what it is about
    static const QStringList commands{u"install"_s, u"run"_s, u"remove"_s, u"update"_s, u"rollback"_s};
    if (words.size() >= 3 && words.at(0).compare("cpak"_L1, Qt::CaseInsensitive) == 0 && commands.contains(words.at(1).toLower())) {
        words = words.mid(2);
        words.removeIf([](const QString& word) { return word.startsWith(u'-'); });
        const auto repository = std::ranges::find_if(words, [](const QString& word) { return word.contains(u'/'); });
        if (repository != words.cend()) {
            text = *repository;
        } else if (!words.isEmpty()) {
            text = words.first();
        }
    }
    // a single address: "https://github.com/a/b/", "git@github.com:a/b.git"
    if (text.contains(u' ') || !text.contains(u'/')) {
        return text;
    }
    for (const auto prefix : {"https://"_L1, "http://"_L1, "ssh://"_L1, "git://"_L1}) {
        if (text.startsWith(prefix, Qt::CaseInsensitive)) {
            text.remove(0, prefix.size());
            break;
        }
    }
    if (text.startsWith("git@"_L1)) {
        text.remove(0, 4);
        if (const auto colon = text.indexOf(u':'); colon > 0 && colon < text.indexOf(u'/')) {
            text[colon] = u'/';
        }
    }
    if (text.startsWith("www."_L1, Qt::CaseInsensitive)) {
        text.remove(0, 4);
    }
    while (text.endsWith(u'/')) {
        text.chop(1);
    }
    if (text.endsWith(".git"_L1)) {
        text.chop(4);
    }
    return text;
}

void CpakClient::search(const QString& query) {
    m_query = normalizeQuery(query).toLower();
    m_results.clear();
    if (m_query.size() >= 2) {
        const auto words = m_query.split(u' ', Qt::SkipEmptyParts);
        QList<CpakItem> rest;
        for (const auto& item : std::as_const(m_items)) {
            const bool matches = std::ranges::all_of(words, [&item](const QString& word) { return item.haystack.contains(word); });
            if (!matches) {
                continue;
            }
            (item.title.startsWith(m_query, Qt::CaseInsensitive) ? m_results : rest) << item;
            if (m_results.size() + rest.size() >= MAX_RESULTS) {
                break;
            }
        }
        m_results += rest;
    }
    emit resultsChanged();
}

const CpakItem* CpakClient::find(const QString& origin) const {
    const auto found = m_index.constFind(origin);
    return found == m_index.cend() ? nullptr : &m_items.at(*found);
}

QString CpakClient::shortVersion(const QString& version) {
    static const QRegularExpression commit(u"^[0-9a-f]{20,}$"_s);
    return commit.match(version).hasMatch() ? version.left(8) : version;
}

CpakItem CpakClient::flatpakCopy(const QString& appId) const {
    for (const auto& item : m_items) {
        if (item.installed && !item.flatpakId.isEmpty() && item.flatpakId.compare(appId, Qt::CaseInsensitive) == 0) {
            return item;
        }
    }
    return {};
}

CpakItem CpakClient::app(const QString& origin) const {
    const auto* found = find(origin);
    return found == nullptr ? CpakItem{} : *found;
}

QString CpakClient::title(const QString& origin) const {
    const auto* found = find(origin);
    return found != nullptr ? found->title : origin;
}

QStringList CpakClient::installArguments(const QString& origin) const {
    const auto* found = find(origin);
    if (found == nullptr || !found->installable) {
        return {};
    }
    return {u"discover"_s, u"install"_s, origin};
}

QStringList CpakClient::removeArguments(const QString& origin) const {
    const auto* found = find(origin);
    if (found == nullptr || !found->installed) {
        return {};
    }
    // an app that the signed catalog does not list has to be removed the plain way
    return found->available ? QStringList{u"discover"_s, u"remove"_s, origin} : QStringList{u"remove"_s, origin};
}

QStringList CpakClient::installFlatpakArguments(const QString& appId) {
    // --yes: the confirmation dialog of this app is the prompt
    return {u"install"_s, u"--flatpak"_s, u"--yes"_s, appId};
}
