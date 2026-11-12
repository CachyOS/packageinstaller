// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

/// One application from the AppStream catalogue of a Flatpak remote.
struct FlatpakApp {
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
    QString version;
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
    [[nodiscard]] QString key() const { return installation + QLatin1Char(':') + ref; }
};

struct FlatpakState {
    bool available = false;  ///< the flatpak command exists
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
    Q_PROPERTY(bool available READ available NOTIFY changed)
    /// At least one system-wide remote is set up.
    Q_PROPERTY(bool hasRemote READ hasRemote NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int appCount READ appCount NOTIFY changed)
    /// Bumped whenever the catalogue or the installed apps were read again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// Apps matching the last search: [{id, key, ref, title, summary, remote, version, installed, ...}].
    /// `key` names one installed copy and is empty for an app that is not installed.
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    /// Installed apps, one entry per installed copy, same shape as `results`.
    Q_PROPERTY(QVariantList installed READ installed NOTIFY changed)
    /// Installed apps and runtimes with a newer version: [{id, ref, title, version, remote, isApp}].
    Q_PROPERTY(QVariantList updates READ updates NOTIFY updatesChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY updatesChanged)

 public:
    explicit FlatpakClient(QObject* parent = nullptr);
    ~FlatpakClient() override;

    [[nodiscard]] bool available() const { return m_state.available; }
    [[nodiscard]] bool hasRemote() const { return !m_state.remotes.isEmpty(); }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] int appCount() const { return static_cast<int>(m_state.apps.size()); }
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QVariantList results() const { return m_results; }
    [[nodiscard]] QVariantList installed() const { return m_installed; }
    [[nodiscard]] QVariantList updates() const { return m_updates; }
    [[nodiscard]] bool checkingUpdates() const { return m_checkUpdates.state() != QProcess::NotRunning; }

    Q_INVOKABLE void search(const QString& query);
    /// Everything known about one app; empty if neither a remote nor the system knows it.
    /// `key` picks one installed copy; without it the copy that matches the catalogue is shown.
    Q_INVOKABLE [[nodiscard]] QVariantMap app(const QString& id, const QString& key) const;

    /// Reads the remotes, the catalogue and the installed apps again, then looks for updates.
    Q_INVOKABLE void refresh();
    /// Forgets everything; used when Flatpak is turned off.
    void clear();

    /// Arguments for `flatpak` that install the app `id` or remove the installed copy `key`;
    /// empty when there is no such app.
    [[nodiscard]] QStringList installArguments(const QString& id) const;
    [[nodiscard]] QStringList removeArguments(const QString& key) const;
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
    void cancelLoad();
    [[nodiscard]] QVariantMap toMap(const FlatpakApp* app, const FlatpakInstalled* copy) const;
    [[nodiscard]] const FlatpakApp* find(const QString& id) const;
    [[nodiscard]] const FlatpakInstalled* findCopy(const QString& key) const;
    /// The installed copy to show for an app from the catalogue.
    [[nodiscard]] const FlatpakInstalled* bestCopy(const QString& id) const;

    FlatpakState m_state;
    QHash<QString, qsizetype> m_index;  ///< app id -> position in m_state.apps
    QFutureWatcher<FlatpakState> m_watcher;
    QProcess m_checkUpdates;
    std::shared_ptr<std::atomic_bool> m_cancel;  ///< stops the catalogue load that is running
    QVariantList m_results;
    QVariantList m_installed;
    QVariantList m_updates;
    QString m_query;
    int m_revision      = 0;
    bool m_loading      = false;
    bool m_reloadQueued = false;
    bool m_cleared      = true;
    bool m_checkOutdated = false;  ///< the installed apps changed while the update check was running
};
