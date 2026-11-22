// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapclient.hpp"
#include "process.hpp"

#include <QPromise>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

const QStringList TABLE_OPTIONS{u"--color=never"_s, u"--unicode=never"_s};

struct Output {
    bool ok = false;
    QStringList lines;
};

/// Runs `snap` as the user with untranslated output, keeping the exit status so a stopped
/// daemon and a failed search can be told apart from an empty table.
Output read(const QString& program, const QStringList& arguments, std::chrono::seconds timeout = 30s) {
    const auto argv   = qt_utils::to_std_strings(QStringList{program} + arguments);
    const auto result = pi::utils::run(argv, {.timeout = timeout});
    return {.ok = result.ok(), .lines = qt_utils::to_qstring_list(pi::utils::make_split_view(result.output))};
}

SnapLoad load() {
    SnapLoad state;
    const auto program = QStandardPaths::findExecutable(u"snap"_s);
    if (program.isEmpty()) {
        return state;
    }
    state.program = program;

    const auto listed   = read(program, QStringList{u"list"_s} + TABLE_OPTIONS);
    state.daemonRunning = listed.ok;
    state.installed     = snapcli::parseList(listed.lines);

    // A non-zero `refresh --list` means "no updates/unavailable", not an app error.
    const auto updates = read(program, QStringList{u"refresh"_s, u"--list"_s} + TABLE_OPTIONS);
    if (updates.ok) {
        state.updates = snapcli::parseUpdates(updates.lines);
    }
    return state;
}

SnapFind find(const QString& program, const QString& query, int generation) {
    SnapFind result;
    result.generation = generation;
    if (program.isEmpty() || query.size() < 2) {
        return result;
    }
    QStringList arguments{u"find"_s};
    arguments += TABLE_OPTIONS;
    arguments += query;
    const auto out = read(program, arguments);
    result.ok      = out.ok;
    if (out.ok) {
        result.apps = snapcli::parseFind(out.lines);
    }
    return result;
}

}  // namespace

SnapClient::SnapClient(QObject* parent) : QObject(parent) {
    connect(&m_loadWatcher, &QFutureWatcher<SnapLoad>::finished, this, &SnapClient::onLoaded);
    connect(&m_findWatcher, &QFutureWatcher<SnapFind>::finished, this, &SnapClient::onFound);
}

SnapClient::~SnapClient() {
    // Stop both before the members are destroyed: a destroyed watcher reports "finished".
    m_loadWatcher.disconnect(this);
    m_loadWatcher.cancel();
    m_loadWatcher.waitForFinished();
    m_findWatcher.disconnect(this);
    m_findWatcher.cancel();
    m_findWatcher.waitForFinished();
}

void SnapClient::refresh() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    using Promise = QPromise<SnapLoad>;
    m_loadWatcher.setFuture(QtConcurrent::run([](Promise& promise) { promise.addResult(load()); }));
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

    if (m_program.isEmpty() || m_query.size() < 2) {
        m_results.clear();
        emit resultsChanged();
        return;
    }
    const auto program = m_program;
    const auto text    = m_query;
    using Promise      = QPromise<SnapFind>;
    m_findWatcher.setFuture(QtConcurrent::run([program, text, generation](Promise& promise) {
        promise.addResult(find(program, text, generation));
    }));
}

void SnapClient::onFound() {
    if (m_findWatcher.isCanceled()) {
        return;
    }
    const auto result = m_findWatcher.result();
    if (result.generation != m_searchGeneration) {
        return;  // a newer query has taken over
    }

    m_results.clear();
    m_searchError.clear();
    if (!result.ok) {
        m_searchError = tr("Could not search the Snap Store. Check the snapd connection and try again.");
    } else {
        for (const auto& app : result.apps) {
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
