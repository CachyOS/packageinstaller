// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gitoutput.hpp"
#include "pkgscan.hpp"
#include "process.hpp"

#include <QDateTime>
#include <QHash>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <expected>
#include <functional>
#include <memory>

class PackageModel;

/// One AUR package: the RPC record merged with its local state.
struct AurPackage {
    Q_GADGET
    QML_VALUE_TYPE(aurPackage)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString pkgbase MEMBER pkgbase FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QString description MEMBER description FINAL)
    Q_PROPERTY(QString url MEMBER url FINAL)
    Q_PROPERTY(QString maintainer MEMBER maintainer FINAL)
    Q_PROPERTY(QString submitter MEMBER submitter FINAL)
    Q_PROPERTY(qint64 firstSubmitted MEMBER firstSubmitted FINAL)
    Q_PROPERTY(int votes MEMBER votes FINAL)
    Q_PROPERTY(double popularity MEMBER popularity FINAL)
    Q_PROPERTY(bool outOfDate MEMBER outOfDate FINAL)
    Q_PROPERTY(qint64 lastModified MEMBER lastModified FINAL)
    Q_PROPERTY(QStringList licenses MEMBER licenses FINAL)
    Q_PROPERTY(QStringList depends MEMBER depends FINAL)
    Q_PROPERTY(QStringList makeDepends MEMBER makeDepends FINAL)
    Q_PROPERTY(QStringList checkDepends MEMBER checkDepends FINAL)
    Q_PROPERTY(bool installed MEMBER installed FINAL)
    Q_PROPERTY(QString installedVersion MEMBER installedVersion FINAL)
    Q_PROPERTY(bool upgradable MEMBER upgradable FINAL)
    Q_PROPERTY(bool heldBack MEMBER heldBack FINAL)
    Q_PROPERTY(bool inRepos MEMBER inRepos FINAL)
    /// AUR packages of a plan that need this one.
    Q_PROPERTY(QStringList neededBy MEMBER neededBy FINAL)
    /// The package's page on the AUR website.
    Q_PROPERTY(QString pageUrl READ pageUrl FINAL)

 public:
    QString name;
    QString pkgbase;
    QString version;
    QString description;
    QString url;
    QString maintainer;
    QString submitter;
    qint64 firstSubmitted = 0;
    int votes             = 0;
    double popularity     = 0;
    bool outOfDate        = false;
    qint64 lastModified   = 0;
    QStringList licenses;
    QStringList depends;
    QStringList makeDepends;
    QStringList checkDepends;
    bool installed = false;
    QString installedVersion{};
    bool upgradable = false;
    bool heldBack   = false;  ///< by IgnorePkg or IgnoreGroup in pacman.conf: not updated with the others
    bool inRepos    = false;  ///< a package of the same name in the repositories should be installed from there
    QStringList neededBy{};

    [[nodiscard]] QString pageUrl() const;
    /// An update that is counted and updated with the others, like `pacman -Syu` and paru do.
    [[nodiscard]] bool offered() const { return upgradable && !heldBack; }

    bool operator==(const AurPackage&) const = default;
};

struct FindingForeign {
    Q_GADGET
    QML_FOREIGN(pkgscan::Finding)
    QML_VALUE_TYPE(finding)
};

struct ScriptFileForeign {
    Q_GADGET
    QML_FOREIGN(pkgscan::ScriptFile)
    QML_VALUE_TYPE(scriptFile)
};

struct DiffRunForeign {
    Q_GADGET
    QML_FOREIGN(git::DiffRun)
    QML_VALUE_TYPE(diffRun)
};

/// Talks to the AUR RPC (v5). Only used after the user opted in to the AUR.
class AurClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    Q_PROPERTY(QList<AurPackage> results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    Q_PROPERTY(AurPackage details READ details NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QString pkgbuild READ pkgbuild NOTIFY pkgbuildChanged FINAL)
    /// Commit of the checkout that is shown and that a build would use.
    Q_PROPERTY(QString revision READ revision NOTIFY pkgbuildChanged FINAL)
    Q_PROPERTY(bool loadingDetails READ loadingDetails NOTIFY loadingDetailsChanged FINAL)
    /// Scripts shipped next to the PKGBUILD (install scriptlet, hooks, units).
    Q_PROPERTY(QList<pkgscan::ScriptFile> files READ files NOTIFY filesChanged FINAL)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged FINAL)
    /// What the automatic check wants a reviewer to look at.
    Q_PROPERTY(QList<pkgscan::Finding> findings READ findings NOTIFY findingsChanged FINAL)
    Q_PROPERTY(int findingCount READ findingCount NOTIFY findingsChanged FINAL)
    Q_PROPERTY(bool dangerous READ dangerous NOTIFY findingsChanged FINAL)
    Q_PROPERTY(QStringList sourceHosts READ sourceHosts NOTIFY findingsChanged FINAL)
    /// AUR packages that the packages passed to resolve() need and that are neither installed
    /// nor in a repository, with `neededBy` set.
    Q_PROPERTY(QList<AurPackage> plan READ plan NOTIFY planChanged FINAL)
    Q_PROPERTY(int planCount READ planCount NOTIFY planChanged FINAL)
    /// Dependencies that cannot be provided, as sentences.
    Q_PROPERTY(QStringList problems READ problems NOTIFY planChanged FINAL)
    Q_PROPERTY(bool resolving READ resolving NOTIFY planChanged FINAL)
    /// The installed AUR packages the AUR knows, those with a newer version first.
    Q_PROPERTY(QList<AurPackage> installed READ installed NOTIFY updatesChanged FINAL)
    /// Installed AUR packages with a newer version in the AUR.
    Q_PROPERTY(QList<AurPackage> updates READ updates NOTIFY updatesChanged FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged FINAL)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY checkingUpdatesChanged FINAL)
    /// For an update: what changed in its recipe since the installed version, in the form of
    /// git::readableDiff(). Empty when nothing did or it is not known.
    Q_PROPERTY(QString changes READ changes NOTIFY changesChanged FINAL)
    /// What kind of line each line of `changes` is.
    Q_PROPERTY(QList<git::DiffRun> changeRuns READ changeRuns NOTIFY changesChanged FINAL)
    /// Said instead of the changes of an update: why they cannot be shown, or that there are none.
    Q_PROPERTY(QString changesNote READ changesNote NOTIFY changesChanged FINAL)

 public:
    explicit AurClient(const PackageModel* packages, QObject* parent = nullptr);
    ~AurClient() override;

    QList<AurPackage> results() const { return m_results; }
    bool searching() const { return m_searching; }
    QString error() const { return m_error; }
    AurPackage details() const { return m_details; }
    QString pkgbuild() const { return m_pkgbuild; }
    QString revision() const { return m_revision; }
    QList<AurPackage> plan() const { return m_plan; }
    int planCount() const { return static_cast<int>(m_plan.size()); }
    QStringList problems() const { return m_problems; }
    bool resolving() const { return m_resolving; }
    bool loadingDetails() const { return m_pending > 0; }
    QList<pkgscan::ScriptFile> files() const { return m_files; }
    QList<pkgscan::Finding> findings() const { return m_findings; }
    int fileCount() const { return static_cast<int>(m_files.size()); }
    int findingCount() const { return static_cast<int>(m_findings.size()); }
    bool dangerous() const {
        return std::ranges::any_of(m_findings, [](const pkgscan::Finding& f) { return f.severity == pkgscan::Severity::Danger; });
    }
    QStringList sourceHosts() const { return m_analysis.hosts; }
    QList<AurPackage> installed() const { return m_installed; }
    QList<AurPackage> updates() const;
    int updateCount() const { return static_cast<int>(std::ranges::count_if(m_installed, &AurPackage::offered)); }
    bool checkingUpdates() const { return m_updateChecks > 0; }
    QString changes() const { return m_changes.text; }
    QList<git::DiffRun> changeRuns() const { return m_changes.runs; }
    QString changesNote() const { return m_changesNote; }

    Q_INVOKABLE void search(const QString& query);
    /// Fetches the package record and clones the package for review. What is shown is read
    /// from that checkout, and the same checkout is what gets built. For an update the whole
    /// history is cloned, to show what changed since the installed version.
    Q_INVOKABLE void loadDetails(const QString& name, const QString& pkgbase, bool upgradable = false);
    /// Works out which other AUR packages `names` need, see `plan`.
    Q_INVOKABLE void resolve(const QStringList& names);
    /// Records that the user reviewed the package that is shown, at the revision that is shown.
    Q_INVOKABLE void accept(const QString& name);
    /// Why `names` cannot be built as they are, or an empty string. `verifyContents` hashes
    /// every file instead of comparing sizes and modification times.
    Q_INVOKABLE [[nodiscard]] QString blockedReason(const QStringList& names, bool verifyContents = false) const;
    /// The reviewed checkouts of `names`, verified file by file, or why they cannot be built.
    [[nodiscard]] std::expected<QStringList, QString> buildDirectories(const QStringList& names) const;
    /// An empty directory to run the build from.
    [[nodiscard]] QString buildWorkingDirectory() const;
    /// Re-evaluates the installed state after a transaction.
    void refreshInstalled();
    /// Asks the AUR for the current versions of the installed packages that are in no repository.
    void checkUpdates();
    void clearUpdates();
    /// Keeps the build directories while a build may still use them. The next start removes them.
    void keepCheckouts() { m_keepCheckouts = true; }

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
    void changesChanged();

 private:
    [[nodiscard]] AurPackage fromJson(const QJsonObject& object) const;
    /// Sets installed, installedVersion, upgradable, heldBack and inRepos from the local database.
    void applyLocal(AurPackage& package) const;
    /// Takes the answer of an update check, or the same list after a reload: brings it up
    /// to date with the local database, sorts it and derives the updates.
    void setInstalled(QList<AurPackage> installed);
    /// Asks the AUR about `names` in batches and passes every record to `done` once all replies are in.
    void fetchInfo(const QStringList& names, std::function<void(QList<AurPackage> found, bool failed)> done);
    /// Runs git in isolation from the user's and the system's configuration.
    void git(const QStringList& arguments, std::function<void(bool ok)> done);
    using GitOutput = std::expected<QByteArray, OutputFailure>;
    /// Like git(), passing on what it printed, unless it failed or printed more than `maxSize` bytes.
    void gitRead(const QStringList& arguments, qsizetype maxSize, std::function<void(GitOutput)> done);
    /// The installed version of `name` if it was built from the AUR, else an empty string.
    [[nodiscard]] QString installedAurVersion(const QString& name) const;
    /// Sets `changes` to what changed in the checkout in `directory` since `installed`.
    /// Nothing to do for an empty `installed`.
    void loadChanges(const QString& directory, const QString& pkgbase, const QString& installed, int generation);
    /// The installed version that the changes of `name` start from, or an empty string when
    /// they are not shown: it is no update, or one whose history cannot hold that version.
    [[nodiscard]] QString changesBase(const QString& name, bool upgradable) const;
    /// The commit that `version` was built from, or why it is not known: the one accepted
    /// after review if it was that version, else searchCommit().
    void findCommit(const QString& directory, const QString& pkgbase, const QString& version,
        std::function<void(std::expected<QString, QString>)> done);
    /// The oldest commit whose .SRCINFO names `version`, or why there is none.
    void searchCommit(const QString& directory, const QString& version, std::function<void(std::expected<QString, QString>)> done);
    /// The changes of the checkout since `commit`, or why they cannot be shown.
    void diffSince(const QString& directory, const QString& commit, std::function<void(std::expected<git::Diff, QString>)> done);
    void setChanges(git::Diff changes, const QString& note);
    void setError(const QString& error);
    void readCheckout(const QString& directory);
    void rescan();
    void resolveNext(const QStringList& wanted, const QHash<QString, QStringList>& wantedBy, int generation);
    void finishResolve();

    struct Checkout {
        QString directory;
        QString commit;
        QByteArray digest;  ///< of the files as they were shown for review
        QByteArray stamp;   ///< names, sizes and times of those files: a cheap check for edits
        QDateTime cloned;
        QString version;       ///< named by its .SRCINFO
        bool history = false;  ///< cloned with every commit, not only the newest
    };
    [[nodiscard]] static bool intact(const Checkout& checkout, bool verifyContents = true);

    const PackageModel* m_packages;
    QNetworkAccessManager m_network;
    QList<AurPackage> m_results;
    AurPackage m_details;
    QString m_pkgbuild;
    QString m_revision;
    QString m_cacheRoot;                     ///< checkouts of this window only
    std::unique_ptr<QLockFile> m_cacheLock;  ///< held while the window lives
    bool m_keepCheckouts = false;
    int m_cloneCount = 0;
    QHash<QString, Checkout> m_checkouts;  ///< by package base, the most recent clone
    QHash<QString, QString> m_pkgbaseOf;   ///< package name -> package base
    QHash<QString, QString> m_accepted;    ///< package name -> commit the user reviewed

    QList<AurPackage> m_plan;
    QStringList m_problems;
    QStringList m_planTargets;
    QHash<QString, AurPackage> m_planInfo;      ///< every package the plan looked at
    QHash<QString, QStringList> m_needs;        ///< package -> AUR packages it needs
    QHash<QString, QStringList> m_unresolved;   ///< package -> dependencies nobody provides
    bool m_resolving     = false;
    int m_planGeneration = 0;
    QList<pkgscan::ScriptFile> m_files;
    git::Diff m_changes;
    QString m_changesNote;
    QList<pkgscan::Finding> m_findings;
    pkgscan::Analysis m_analysis;
    QStringList m_unreadable;
    QString m_lookAlike;
    QStringList m_repoNames;  ///< packages in the repositories, for the look-alike check
    QString m_error;
    bool m_searching = false;
    int m_pending    = 0;
    int m_generation = 0;
    int m_detailsGeneration = 0;

    QList<AurPackage> m_installed;
    int m_updateChecks      = 0;  ///< checks still running, including superseded ones
    int m_updatesGeneration = 0;
};
