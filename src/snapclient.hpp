// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snapcli.hpp"

#include <QFutureWatcher>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QNetworkReply;

/// One snap as QML sees it: a store result, an installed copy, or both.
struct SnapItem {
    Q_GADGET
    QML_VALUE_TYPE(snapItem)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QString publisher MEMBER publisher FINAL)
    Q_PROPERTY(QString notes MEMBER notes FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
    Q_PROPERTY(QString revision MEMBER revision FINAL)
    Q_PROPERTY(QString channel MEMBER channel FINAL)
    Q_PROPERTY(bool installed MEMBER installed FINAL)
    Q_PROPERTY(bool upgradable MEMBER upgradable FINAL)

 public:
    QString name;
    QString version;
    QString publisher;
    QString notes;
    QString summary;
    QString revision;
    QString channel;
    bool installed  = false;
    bool upgradable = false;
};

/// What one background read of the installed snaps and the available updates found.
struct SnapLoad {
    QString program;  ///< the snap command, empty when it is not installed
    bool daemonRunning = false;
    QList<snapcli::SnapInstalled> installed;
    QList<snapcli::SnapUpdate> updates;
};

/// Snaps: store search goes straight to the Snap Store over HTTP; the installed snaps and their
/// updates still come from the unprivileged `snap` command.
class SnapClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// The snap command is installed.
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    /// `snap list` could talk to snapd.
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int appCount READ appCount NOTIFY changed FINAL)
    /// Bumped whenever the installed snaps and the updates were read again.
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    /// Apps matching the last search.
    Q_PROPERTY(QList<SnapItem> results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QList<SnapItem> installed READ installed NOTIFY changed FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY changed FINAL)
    /// Why the last store search failed, empty when it succeeded.
    Q_PROPERTY(QString searchError READ searchError NOTIFY resultsChanged FINAL)

 public:
    explicit SnapClient(QObject* parent = nullptr);
    ~SnapClient() override;

    bool available() const { return !m_program.isEmpty(); }
    bool daemonRunning() const { return m_daemonRunning; }
    bool loading() const { return m_loading || m_searching; }
    int appCount() const { return static_cast<int>(m_installed.size()); }
    int revision() const { return m_revision; }
    QList<SnapItem> results() const { return m_results; }
    QList<SnapItem> installed() const { return m_installed; }
    int updateCount() const { return static_cast<int>(m_updates.size()); }
    QString searchError() const { return m_searchError; }

    /// Looks up `query` in the store; an older search cannot overwrite a newer one.
    Q_INVOKABLE void search(const QString& query);
    /// Reads the installed snaps and the updates again, then re-runs the current query.
    Q_INVOKABLE void refresh();
    /// Everything known about one snap; empty when neither the store nor the system knows it.
    Q_INVOKABLE [[nodiscard]] SnapItem app(const QString& name) const;

 signals:
    void changed();
    void loadingChanged();
    void resultsChanged();

 private:
    void onLoaded();

    QString m_program;
    QString m_query;
    QString m_searchError;
    QList<SnapItem> m_results;
    QList<SnapItem> m_installed;
    QList<snapcli::SnapUpdate> m_updates;
    QFutureWatcher<SnapLoad> m_loadWatcher;
    QNetworkAccessManager m_store;
    QNetworkReply* m_searchReply = nullptr;
    QUrl m_storeUrl;
    int m_searchGeneration = 0;
    int m_revision         = 0;
    bool m_daemonRunning   = false;
    bool m_loading         = false;
    bool m_searching       = false;
    bool m_reloadQueued    = false;
};
