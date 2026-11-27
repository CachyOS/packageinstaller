// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QtQml/qqmlregistration.h>

/// One application of cpak's signed catalog, as `cpak discover list` reports it.
struct CpakItem {
    Q_GADGET
    QML_VALUE_TYPE(cpakItem)
    Q_PROPERTY(QString origin MEMBER origin FINAL)
    Q_PROPERTY(QString title MEMBER title FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QString installedVersion MEMBER installedVersion FINAL)
    Q_PROPERTY(QStringList permissions MEMBER permissions FINAL)
    /// The Flatpak app id of an app installed from Flathub, empty for other apps.
    Q_PROPERTY(QString flatpakId MEMBER flatpakId FINAL)
    Q_PROPERTY(bool available MEMBER available FINAL)
    Q_PROPERTY(bool installed MEMBER installed FINAL)
    Q_PROPERTY(bool installable MEMBER installable FINAL)
    Q_PROPERTY(bool upgradable MEMBER upgradable FINAL)
    Q_PROPERTY(QString newVersion MEMBER newVersion FINAL)

 public:
    QString origin;  ///< "github.com/bottlesdevs/bottles": names the app everywhere
    QString title;
    QString summary;
    QString version;  ///< the catalog's current version
    QString installedVersion;
    QStringList permissions;   ///< "Files: home, read write", what the app asks for
    QString flatpakId;         ///< "pm.mirko.Atoms" for the origin "flatpak/flathub/pm.mirko.atoms"
    bool available   = false;  ///< the signed catalog lists it
    bool installed   = false;
    bool installable = false;
    bool upgradable  = false;
    QString newVersion;
    QString haystack;  ///< lower case text the search looks at
};

namespace cpak {

/// What `cpak discover list` printed, in the order of the catalog. An empty list if it is not
/// the catalog JSON. Icons are taken out to `iconDirectory` and returned by origin.
struct Catalog {
    QList<CpakItem> items;
    IconIndex icons;
};
[[nodiscard]] Catalog parseCatalog(const QByteArray& json, const QString& iconDirectory);

/// The apps `cpak list --json` reports as installed, without the runtimes that were only
/// pulled in for them.
[[nodiscard]] QList<CpakItem> parseInstalled(const QByteArray& json);

/// Adds the installed apps that the catalog does not list, such as Flatpak apps installed through
/// cpak, and takes their version from `installed` for the ones it does.
void mergeInstalled(Catalog& catalog, const QList<CpakItem>& installed);

/// The origins `cpak update --check --json` lists as having an update, with their new version.
[[nodiscard]] QHash<QString, QString> parseUpdates(const QByteArray& json);

}  // namespace cpak

/// What one read of the catalog found.
struct CpakState {
    QString program;  ///< the cpak command, empty when it is not installed
    cpak::Catalog catalog;
};

/// Apps from cpak, driven through the `cpak` command and its `discover` interface for software
/// centers. cpak runs them in a sandbox of its own and works as the user. Only used after the
/// user turned it on.
class CpakClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// The cpak command is installed.
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int appCount READ appCount NOTIFY changed FINAL)
    /// The catalog could not be read although cpak is installed.
    Q_PROPERTY(bool failed READ failed NOTIFY changed FINAL)
    /// Bumped whenever the catalog was read again.
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(QList<CpakItem> results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QList<CpakItem> installed READ installed NOTIFY changed FINAL)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY changed FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged FINAL)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY updatesChanged FINAL)

 public:
    explicit CpakClient(QObject* parent = nullptr);
    ~CpakClient() override;

    bool available() const { return !m_program.isEmpty(); }
    bool loading() const { return m_loading; }
    bool failed() const { return m_failed; }
    int appCount() const { return static_cast<int>(m_items.size()); }
    int revision() const { return m_revision; }
    QList<CpakItem> results() const { return m_results; }
    QList<CpakItem> installed() const { return m_installed; }
    int installedCount() const { return static_cast<int>(m_installed.size()); }
    int updateCount() const;
    bool checkingUpdates() const { return m_checkUpdates.state() != QProcess::NotRunning; }

    Q_INVOKABLE void search(const QString& query);
    /// What to search for when `query` was pasted: "cpak install github.com/a/b", a web address
    /// or a clone address of the repository all become "github.com/a/b". Anything else, such as
    /// "bottles" or two words, is returned as typed.
    [[nodiscard]] static QString normalizeQuery(const QString& query);
    /// Everything known about one app; empty if cpak neither lists nor has installed it.
    Q_INVOKABLE [[nodiscard]] CpakItem app(const QString& origin) const;
    /// A version for display: the first 8 characters of a commit ID, which is what cpak records
    /// for an app installed by commit; any other version as it is.
    Q_INVOKABLE [[nodiscard]] static QString shortVersion(const QString& version);
    /// The copy of the Flatpak app `appId` that cpak has installed; empty if there is none.
    Q_INVOKABLE [[nodiscard]] CpakItem flatpakCopy(const QString& appId) const;

    /// Reads the catalog again, then looks for updates.
    Q_INVOKABLE void refresh();
    /// Forgets everything; used when cpak is turned off.
    void clear();

    /// Arguments for `cpak`; empty when the catalog does not list the app.
    [[nodiscard]] QStringList installArguments(const QString& origin) const;
    [[nodiscard]] QStringList removeArguments(const QString& origin) const;
    /// Installs a Flatpak app into cpak's sandbox. The caller asked for it by name.
    [[nodiscard]] static QStringList installFlatpakArguments(const QString& appId);
    [[nodiscard]] QString title(const QString& origin) const;

 signals:
    void changed();
    void loadingChanged();
    void resultsChanged();
    void updatesChanged();
    /// New icons for `image://pkgicon/cpak/<origin>`.
    void iconsLoaded(const IconIndex& icons);

 private:
    void onLoaded();
    void checkUpdates();
    /// Builds the lists again, e.g. to mark the apps that can be updated.
    void rebuild();
    [[nodiscard]] const CpakItem* find(const QString& origin) const;

    QString m_program;
    QList<CpakItem> m_items;
    QHash<QString, qsizetype> m_index;  ///< origin -> position in m_items
    QHash<QString, QString> m_updates;  ///< origin -> new version
    QFutureWatcher<CpakState> m_watcher;
    QProcess m_checkUpdates;
    QList<CpakItem> m_results;
    QList<CpakItem> m_installed;
    QString m_query;
    /// Moves on whenever the catalog is read again or everything is cleared; an update
    /// check from an older generation is stale.
    int m_generation      = 0;
    int m_checkGeneration = 0;
    int m_revision        = 0;
    bool m_loading        = false;
    bool m_failed         = false;
    bool m_reloadQueued   = false;
};
