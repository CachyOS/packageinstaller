// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapclient.hpp"
#include "process.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

const QStringList TABLE_OPTIONS{u"--color=never"_s, u"--unicode=never"_s};
constexpr auto STORE_URL = "https://api.snapcraft.io/v2/snaps/find"_L1;

struct Output {
    bool ok = false;
    QStringList lines;
};

/// Runs `snap` as the user with untranslated output, keeping the exit status so a stopped
/// daemon and a failed search can be told apart from an empty table. Gives up on cancellation.
Output read(const QString& program, const QStringList& arguments, std::function<bool()> cancel_requested,
    std::chrono::seconds timeout = 30s) {
    const auto argv   = qt_utils::to_std_strings(QStringList{program} + arguments);
    const auto result = pi::utils::run(argv, {.timeout = timeout, .cancel_requested = std::move(cancel_requested)});
    return {.ok = result.ok(), .lines = qt_utils::to_qstring_list(pi::utils::make_split_view(result.output))};
}

SnapLoad load(const QPromise<SnapLoad>& promise) {
    SnapLoad state;
    const auto program = QStandardPaths::findExecutable(u"snap"_s);
    if (program.isEmpty()) {
        return state;
    }
    state.program = program;

    const auto cancel   = [&] { return promise.isCanceled(); };
    const auto listed   = read(program, QStringList{u"list"_s} + TABLE_OPTIONS, cancel);
    state.daemonRunning = listed.ok;
    state.installed     = snapcli::parseList(listed.lines);

    // A non-zero `refresh --list` means "no updates/unavailable", not an app error.
    const auto updates = read(program, QStringList{u"refresh"_s, u"--list"_s} + TABLE_OPTIONS, cancel);
    if (updates.ok) {
        state.updates = snapcli::parseUpdates(updates.lines);
    }
    return state;
}

/// The V2 store needs device headers; a different device id per process is enough here.
QNetworkRequest storeRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, u"cachyos-pi/" PI_VERSION ""_s);
    request.setRawHeader("Snap-Device-Series", "16");
    request.setRawHeader("Snap-Device-Architecture", QSysInfo::currentCpuArchitecture().replace(u"arm64"_s, u"aarch64"_s).toUtf8());
    static const auto deviceId = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    request.setRawHeader("X-Device-Id", deviceId);
    request.setTransferTimeout(20000);
    return request;
}

/// Only the fields shown by the page are read; a missing or wrongly typed field stays empty.
QList<snapcli::SnapApp> parseStore(const QByteArray& data) {
    const auto document = QJsonDocument::fromJson(data);
    QList<snapcli::SnapApp> apps;
    for (const auto& value : document.object().value(u"results"_s).toArray()) {
        const auto object    = value.toObject();
        const auto snap      = object.value(u"snap"_s).toObject();
        const auto publisher = snap.value(u"publisher"_s).toObject();
        const auto topPub    = object.value(u"publisher"_s).toObject();
        const auto pubName   = publisher.value(u"display-name"_s).toString();
        const auto summary   = snap.value(u"summary"_s).toString();
        apps << snapcli::SnapApp{
            .name      = object.value(u"name"_s).toString(),
            .version   = object.value(u"revision"_s).toObject().value(u"version"_s).toString(),
            .publisher = pubName.isEmpty() ? topPub.value(u"display-name"_s).toString() : pubName,
            .notes     = (publisher.value(u"validation"_s).toString() == u"verified"_s
                             || topPub.value(u"validation"_s).toString() == u"verified"_s)
                ? u"verified"_s
                : QString(),
            .summary   = summary.isEmpty() ? object.value(u"summary"_s).toString() : summary,
        };
    }
    return apps;
}

}  // namespace

SnapClient::SnapClient(QObject* parent) : QObject(parent) {
    connect(&m_loadWatcher, &QFutureWatcher<SnapLoad>::finished, this, &SnapClient::onLoaded);
    const auto overrideUrl = QString::fromLocal8Bit(qgetenv("CACHYOS_PI_SNAP_STORE_URL"));
    m_storeUrl             = overrideUrl.isEmpty() ? QUrl(STORE_URL) : QUrl(overrideUrl);
}

SnapClient::~SnapClient() {
    if (m_searchReply != nullptr) {
        m_searchReply->disconnect(this);
        m_searchReply->abort();
        m_searchReply->deleteLater();
    }
    // Stop the read before the watcher is destroyed: a destroyed watcher reports "finished".
    m_loadWatcher.disconnect(this);
    m_loadWatcher.cancel();
    m_loadWatcher.waitForFinished();
}

void SnapClient::refresh() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    using Promise = QPromise<SnapLoad>;
    m_loadWatcher.setFuture(QtConcurrent::run([](Promise& promise) { promise.addResult(load(promise)); }));
}

void SnapClient::onLoaded() {
    m_loading = false;
    if (m_loadWatcher.isCanceled()) {
        emit loadingChanged();
        if (std::exchange(m_reloadQueued, false)) {
            refresh();
        }
        return;
    }

    const auto state = m_loadWatcher.result();
    m_program        = state.program;
    m_daemonRunning  = state.daemonRunning;
    m_updates        = state.updates;
    m_installed.clear();
    for (const auto& entry : state.installed) {
        SnapItem item;
        item.name       = entry.name;
        item.version    = entry.version;
        item.revision   = entry.revision;
        item.channel    = entry.channel;
        item.publisher  = entry.publisher;
        item.notes      = entry.notes;
        item.installed  = true;
        item.upgradable = std::ranges::any_of(m_updates, [&entry](const auto& update) { return update.name == entry.name; });
        m_installed << item;
    }
    ++m_revision;
    emit loadingChanged();
    emit changed();
    search(m_query);

    if (std::exchange(m_reloadQueued, false)) {
        refresh();
    }
}

void SnapClient::search(const QString& query) {
    m_query              = query.trimmed();
    const int generation = ++m_searchGeneration;
    m_searchError.clear();

    if (m_searchReply != nullptr) {
        m_searchReply->disconnect(this);
        m_searchReply->abort();
        m_searchReply->deleteLater();
        m_searchReply = nullptr;
    }

    if (m_query.size() < 2) {
        m_results.clear();
        m_searching = false;
        emit resultsChanged();
        emit loadingChanged();
        return;
    }

    QUrl url = m_storeUrl;
    QUrlQuery parameters;
    parameters.addQueryItem(u"q"_s, m_query);
    parameters.addQueryItem(u"fields"_s, u"version,summary,publisher,title,media"_s);
    url.setQuery(parameters);

    m_searching = true;
    emit loadingChanged();
    auto* reply   = m_store.get(storeRequest(url));
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (reply != m_searchReply) {
            reply->deleteLater();
            return;
        }
        m_searchReply = nullptr;
        reply->deleteLater();

        m_searching = false;
        emit loadingChanged();
        if (generation != m_searchGeneration) {
            return;  // a newer query has taken over
        }

        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool failed = reply->error() != QNetworkReply::NoError || (status != 0 && status != 200);
        m_results.clear();
        m_searchError.clear();
        if (failed) {
            m_searchError = tr("Could not search the Snap Store. Check your connection and try again.");
        } else {
            for (const auto& app : parseStore(reply->readAll())) {
                SnapItem item;
                item.name      = app.name;
                item.version   = app.version;
                item.publisher = app.publisher;
                item.notes     = app.notes;
                item.summary   = app.summary;
                for (const auto& installed : m_installed) {
                    if (installed.name == app.name) {
                        item.revision   = installed.revision;
                        item.channel    = installed.channel;
                        item.installed  = true;
                        item.upgradable = installed.upgradable;
                        break;
                    }
                }
                m_results << item;
            }
        }
        emit resultsChanged();
    });
}

SnapItem SnapClient::app(const QString& name) const {
    for (const auto& item : m_results) {
        if (item.name == name) {
            return item;
        }
    }
    for (const auto& item : m_installed) {
        if (item.name == name) {
            return item;
        }
    }
    return {};
}
