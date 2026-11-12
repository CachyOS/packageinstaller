// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pkgscan.hpp"

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class PackageModel;

/// Talks to the AUR RPC (v5). Only used after the user opted in to the AUR.
class AurClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged)
    Q_PROPERTY(QString pkgbuild READ pkgbuild NOTIFY pkgbuildChanged)
    Q_PROPERTY(bool loadingDetails READ loadingDetails NOTIFY loadingDetailsChanged)
    /// Scripts shipped next to the PKGBUILD (install scriptlet, hooks, units): [{name, text}].
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    /// What the automatic check wants a reviewer to look at: [{id, severity, file, line, title, detail, excerpt}].
    Q_PROPERTY(QVariantList findings READ findings NOTIFY findingsChanged)
    Q_PROPERTY(QStringList sourceHosts READ sourceHosts NOTIFY findingsChanged)
    /// Installed AUR packages with a newer version in the AUR, same shape as `results`.
    Q_PROPERTY(QVariantList updates READ updates NOTIFY updatesChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY checkingUpdatesChanged)
    /// How many installed packages the AUR knows; -1 until the first check has finished.
    Q_PROPERTY(int installedCount READ installedCount NOTIFY updatesChanged)

 public:
    explicit AurClient(const PackageModel* packages, QObject* parent = nullptr);

    [[nodiscard]] QVariantList results() const { return m_results; }
    [[nodiscard]] bool searching() const { return m_searching; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QVariantMap details() const { return m_details; }
    [[nodiscard]] QString pkgbuild() const { return m_pkgbuild; }
    [[nodiscard]] bool loadingDetails() const { return m_pending > 0; }
    [[nodiscard]] QVariantList files() const { return m_files; }
    [[nodiscard]] QVariantList findings() const { return m_findings; }
    [[nodiscard]] QStringList sourceHosts() const { return m_analysis.hosts; }
    [[nodiscard]] QVariantList updates() const { return m_updates; }
    [[nodiscard]] bool checkingUpdates() const { return m_updateRequests > 0; }
    [[nodiscard]] int installedCount() const { return m_installedCount; }

    Q_INVOKABLE void search(const QString& query);
    /// Fetches dependency information and the PKGBUILD for review.
    Q_INVOKABLE void loadDetails(const QString& name, const QString& pkgbase);
    /// Re-evaluates the installed state after a transaction.
    void refreshInstalled();
    /// Asks the AUR for the current versions of the installed packages that are in no repository.
    void checkUpdates();
    void clearUpdates();

 signals:
    void resultsChanged();
    void searchingChanged();
    void errorChanged();
    void detailsChanged();
    void pkgbuildChanged();
    void loadingDetailsChanged();
    void filesChanged();
    void findingsChanged();
    void updatesChanged();
    void checkingUpdatesChanged();

 private:
    [[nodiscard]] QVariantMap toMap(const QJsonObject& object) const;
    void setError(const QString& error);
    void loadFile(const QString& name, const QString& pkgbase, int generation);
    void rescan();

    const PackageModel* m_packages;
    QNetworkAccessManager m_network;
    QVariantList m_results;
    QVariantMap m_details;
    QString m_pkgbuild;
    QVariantList m_files;
    QVariantList m_findings;
    pkgscan::Analysis m_analysis;
    QStringList m_unreadable;
    QString m_lookAlike;
    QString m_error;
    bool m_searching = false;
    int m_pending    = 0;
    int m_generation = 0;
    int m_detailsGeneration = 0;

    QVariantList m_updates;
    QList<QVariantMap> m_updatesFound;  ///< collected while a check runs
    int m_updatesKnown      = 0;
    bool m_updatesFailed    = false;
    int m_installedCount    = -1;
    int m_updateRequests    = 0;  ///< replies still expected, including those of a superseded check
    int m_updatesPending    = 0;  ///< replies still expected by the current check
    int m_updatesGeneration = 0;
};
