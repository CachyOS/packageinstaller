// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pkgscan.hpp"

#include <QHash>
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
    /// Commit of the checkout that is shown and that a build would use.
    Q_PROPERTY(QString revision READ revision NOTIFY pkgbuildChanged)
    Q_PROPERTY(bool loadingDetails READ loadingDetails NOTIFY loadingDetailsChanged)
    /// Scripts shipped next to the PKGBUILD (install scriptlet, hooks, units): [{name, text}].
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    /// What the automatic check wants a reviewer to look at: [{id, severity, file, line, title, detail, excerpt}].
    Q_PROPERTY(QVariantList findings READ findings NOTIFY findingsChanged)
    Q_PROPERTY(QStringList sourceHosts READ sourceHosts NOTIFY findingsChanged)
    /// AUR packages that the packages passed to resolve() need and that are neither installed
    /// nor in a repository. Same shape as `results`, plus `neededBy`.
    Q_PROPERTY(QVariantList plan READ plan NOTIFY planChanged)
    /// Dependencies that cannot be provided, as sentences.
    Q_PROPERTY(QStringList problems READ problems NOTIFY planChanged)
    Q_PROPERTY(bool resolving READ resolving NOTIFY planChanged)
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
    [[nodiscard]] QString revision() const { return m_revision; }
    [[nodiscard]] QVariantList plan() const { return m_plan; }
    [[nodiscard]] QStringList problems() const { return m_problems; }
    [[nodiscard]] bool resolving() const { return m_resolving; }
    [[nodiscard]] bool loadingDetails() const { return m_pending > 0; }
    [[nodiscard]] QVariantList files() const { return m_files; }
    [[nodiscard]] QVariantList findings() const { return m_findings; }
    [[nodiscard]] QStringList sourceHosts() const { return m_analysis.hosts; }
    [[nodiscard]] QVariantList updates() const { return m_updates; }
    [[nodiscard]] bool checkingUpdates() const { return m_updateRequests > 0; }
    [[nodiscard]] int installedCount() const { return m_installedCount; }

    Q_INVOKABLE void search(const QString& query);
    /// Fetches the package record and clones the package for review. What is shown is read
    /// from that checkout, and the same checkout is what gets built.
    Q_INVOKABLE void loadDetails(const QString& name, const QString& pkgbase);
    /// Works out which other AUR packages `names` need, see `plan`.
    Q_INVOKABLE void resolve(const QStringList& names);
    /// Records that the user reviewed the package that is shown, at the revision that is shown.
    Q_INVOKABLE void accept(const QString& name);
    /// Why `names` cannot be built as they are; empty when they can.
    Q_INVOKABLE [[nodiscard]] QString blockedReason(const QStringList& names) const;
    /// The reviewed checkouts of `names`; empty while blockedReason() has something to say.
    [[nodiscard]] QStringList buildDirectories(const QStringList& names) const;
    /// An empty directory to run the build from.
    [[nodiscard]] QString buildWorkingDirectory() const;
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
    void planChanged();
    void updatesChanged();
    void checkingUpdatesChanged();

 private:
    [[nodiscard]] QVariantMap toMap(const QJsonObject& object) const;
    void setError(const QString& error);
    void readCheckout(const QString& directory);
    void rescan();
    void resolveNext(const QStringList& wanted, const QHash<QString, QStringList>& wantedBy, int generation);
    void finishResolve();

    struct Checkout {
        QString directory;
        QString commit;
    };

    const PackageModel* m_packages;
    QNetworkAccessManager m_network;
    QVariantList m_results;
    QVariantMap m_details;
    QString m_pkgbuild;
    QString m_revision;
    QString m_cacheRoot;
    int m_cloneCount = 0;
    QHash<QString, Checkout> m_checkouts;  ///< by package base, the most recent clone
    QHash<QString, QString> m_pkgbaseOf;   ///< package name -> package base
    QHash<QString, QString> m_accepted;    ///< package name -> commit the user reviewed

    QVariantList m_plan;
    QStringList m_problems;
    QStringList m_planTargets;
    QHash<QString, QVariantMap> m_planInfo;     ///< every package the plan looked at
    QHash<QString, QStringList> m_needs;        ///< package -> AUR packages it needs
    QHash<QString, QStringList> m_unresolved;   ///< package -> dependencies nobody provides
    bool m_resolving     = false;
    int m_planGeneration = 0;
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
