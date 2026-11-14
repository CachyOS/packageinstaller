// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QtQml/qqmlregistration.h>

/// One application from the AppStream catalogue of a Flatpak remote.
struct FlatpakApp {
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id FINAL)
    Q_PROPERTY(QString ref MEMBER ref FINAL)
    Q_PROPERTY(QString branch MEMBER branch FINAL)
    Q_PROPERTY(QString remote MEMBER remote FINAL)
    Q_PROPERTY(QString title MEMBER title FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
    Q_PROPERTY(QString description MEMBER description FINAL)
    Q_PROPERTY(QString developer MEMBER developer FINAL)
    Q_PROPERTY(QString homepage MEMBER homepage FINAL)
    Q_PROPERTY(QString license MEMBER license FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QStringList screenshots MEMBER screenshots FINAL)

 public:
    QString id;      ///< "org.mozilla.firefox"
    QString ref;     ///< "org.mozilla.firefox/x86_64/stable"
    QString branch;
    QString remote;  ///< "flathub"
    QString title;
    QString summary;
    QString description;
    QString developer;
    QString homepage;
    QString license;
    QString version;  ///< in the catalogue, or the new version of an update
    QStringList screenshots;
    QString haystack;  ///< lower case text the search looks at
};

/// One installed ref, as `flatpak list` reports it.
struct FlatpakInstalled {
    QString ref;  ///< "org.mozilla.firefox/x86_64/stable"
    QString id;
    QString name;
    QString version;
    QString remote;
    QString installation;  ///< "system", "user" or the name of another system-wide installation
    QString size;
    bool isApp = false;

    /// An app can be installed more than once (per user and system-wide, several branches);
    /// this tells the copies apart.
    QString key() const { return installation + u':' + ref; }
};

/// One app or runtime as QML sees it: a catalogue entry, an installed copy, or both.
struct FlatpakItem : FlatpakApp {
    Q_GADGET
    QML_VALUE_TYPE(flatpakItem)
    Q_PROPERTY(QString key MEMBER key FINAL)
    Q_PROPERTY(bool available MEMBER available FINAL)
    Q_PROPERTY(bool installed MEMBER installed FINAL)
    Q_PROPERTY(QString installedVersion MEMBER installedVersion FINAL)
    Q_PROPERTY(QString installedFrom MEMBER installedFrom FINAL)
    Q_PROPERTY(QString installation MEMBER installation FINAL)
    Q_PROPERTY(QString size MEMBER size FINAL)
    Q_PROPERTY(bool isApp MEMBER isApp FINAL)
    Q_PROPERTY(bool upgradable MEMBER upgradable FINAL)
    Q_PROPERTY(QString newVersion MEMBER newVersion FINAL)

 public:
    QString key;  ///< names one installed copy, empty for an app that is not installed
    bool available = false;  ///< a remote offers it
    bool installed = false;
    QString installedVersion;
    QString installedFrom;
    QString installation;
    QString size;
    bool isApp      = false;
    bool upgradable = false;  ///< an installed copy for which a newer version was found
    QString newVersion;       ///< of an upgradable copy, if flatpak said which
};

struct FlatpakState {
    QString program;         ///< the flatpak command, empty when it is not installed
    QStringList remotes;     ///< system-wide remotes
    QList<FlatpakApp> apps;
    QList<FlatpakInstalled> installed;
    IconIndex icons;
};

/// Flatpak apps, driven through the `flatpak` command; the catalogue is the AppStream data
/// that Flatpak keeps for its remotes. Only used after the user turned Flatpak on.
class FlatpakClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// The flatpak command is installed.
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    /// At least one system-wide remote is set up.
    Q_PROPERTY(bool hasRemote READ hasRemote NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int appCount READ appCount NOTIFY changed FINAL)
    /// Bumped whenever the catalogue or the installed apps were read again.
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    /// Apps matching the last search.
    Q_PROPERTY(QList<FlatpakItem> results READ results NOTIFY resultsChanged FINAL)
    /// Installed apps, one entry per installed copy.
    Q_PROPERTY(QList<FlatpakItem> installed READ installed NOTIFY changed FINAL)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY changed FINAL)
    /// Installed apps and runtimes with a newer version.
    Q_PROPERTY(QList<FlatpakItem> updates READ updates NOTIFY updatesChanged FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged FINAL)
    /// How many of the updates are runtimes and extensions rather than apps.
    Q_PROPERTY(int runtimeUpdates READ runtimeUpdates NOTIFY updatesChanged FINAL)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY updatesChanged FINAL)

 public:
    explicit FlatpakClient(QObject* parent = nullptr);
    ~FlatpakClient() override;

    bool available() const { return !m_state.program.isEmpty(); }
    bool hasRemote() const { return !m_state.remotes.isEmpty(); }
    bool loading() const { return m_loading; }
    int appCount() const { return static_cast<int>(m_state.apps.size()); }
    int revision() const { return m_revision; }
    QList<FlatpakItem> results() const { return m_results; }
    QList<FlatpakItem> installed() const { return m_installed; }
    QList<FlatpakItem> updates() const { return m_updates; }
    int installedCount() const { return static_cast<int>(m_installed.size()); }
    int updateCount() const { return static_cast<int>(m_updates.size()); }
    int runtimeUpdates() const;
    bool checkingUpdates() const { return m_checkUpdates.state() != QProcess::NotRunning; }

    Q_INVOKABLE void search(const QString& query);
    /// Everything known about one app; empty if neither a remote nor the system knows it.
    /// `key` picks one installed copy; without it the copy that matches the catalogue is shown.
    Q_INVOKABLE [[nodiscard]] FlatpakItem app(const QString& id, const QString& key) const;

    /// Reads the remotes, the catalogue and the installed apps again, then looks for updates.
    Q_INVOKABLE void refresh();
    /// Forgets everything; used when Flatpak is turned off.
    void clear();

    /// Arguments for `flatpak` that install the app `id` or remove the installed copy `key`;
    /// empty when there is no such app.
    [[nodiscard]] QStringList installArguments(const QString& id) const;
    [[nodiscard]] QStringList removeArguments(const QString& key) const;
    /// Arguments that remove the runtimes no app needs any more, where the copy `key` is installed.
    [[nodiscard]] QStringList cleanupArguments(const QString& key) const;
    [[nodiscard]] QString title(const QString& id) const;
    [[nodiscard]] QString installedTitle(const QString& key) const;

 signals:
    void changed();
    void loadingChanged();
    void resultsChanged();
    void updatesChanged();
    /// New icons for `image://pkgicon/flatpak/<id>`.
    void iconsLoaded(const IconIndex& icons);

 private:
    void onLoaded();
    void checkUpdates();
    /// Builds `installed` and `results` again, e.g. to mark the copies that can be updated.
    void rebuild();
    [[nodiscard]] FlatpakItem toItem(const FlatpakApp* app, const FlatpakInstalled* copy) const;
    [[nodiscard]] const FlatpakApp* find(const QString& id) const;
    [[nodiscard]] const FlatpakInstalled* findCopy(const QString& key) const;
    /// The installed copy to show for an app from the catalogue.
    [[nodiscard]] const FlatpakInstalled* bestCopy(const QString& id) const;

    FlatpakState m_state;
    QHash<QString, qsizetype> m_index;  ///< app id -> position in m_state.apps
    QFutureWatcher<FlatpakState> m_watcher;
    QProcess m_checkUpdates;
    QList<FlatpakItem> m_results;
    QList<FlatpakItem> m_installed;
    QList<FlatpakItem> m_updates;
    QString m_query;
    /// Moves on whenever the installed apps are read again or everything is cleared. An
    /// update check started in an older generation is stale.
    int m_generation      = 0;
    int m_checkGeneration = 0;
    int m_revision        = 0;
    bool m_loading        = false;
    bool m_reloadQueued   = false;
};
