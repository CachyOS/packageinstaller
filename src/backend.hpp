// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "aurclient.hpp"
#include "catalog.hpp"
#include "flatpakclient.hpp"
#include "packagemodel.hpp"
#include "transaction.hpp"
#include "updatenotice.hpp"

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class IconProvider;
class QQmlEngine;
class QJSEngine;

class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(PackageModel* packages READ packages CONSTANT FINAL)
    Q_PROPERTY(AurClient* aur READ aur CONSTANT FINAL)
    Q_PROPERTY(Transaction* transaction READ transaction CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QList<CuratedSection> curated READ curated NOTIFY curatedChanged FINAL)
    Q_PROPERTY(int curatedCount READ curatedCount NOTIFY curatedChanged FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY checkingUpdatesChanged FINAL)
    Q_PROPERTY(bool aurEnabled READ aurEnabled WRITE setAurEnabled NOTIFY aurEnabledChanged FINAL)
    Q_PROPERTY(bool paruAvailable READ paruAvailable NOTIFY paruAvailableChanged FINAL)
    Q_PROPERTY(FlatpakClient* flatpak READ flatpak CONSTANT FINAL)
    Q_PROPERTY(UpdateNotice* updateNotice READ updateNotice CONSTANT FINAL)
    Q_PROPERTY(bool flatpakEnabled READ flatpakEnabled WRITE setFlatpakEnabled NOTIFY flatpakEnabledChanged FINAL)

 public:
    explicit Backend(IconProvider* icons, QObject* parent = nullptr);
    ~Backend() override;

    static Backend* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    PackageModel* packages() { return &m_packages; }
    AurClient* aur() { return &m_aur; }
    Transaction* transaction() { return &m_transaction; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    int revision() const { return m_revision; }
    /// The curated shelves, holding only the names the repositories have.
    QList<CuratedSection> curated() const { return m_curated; }
    int curatedCount() const { return static_cast<int>(m_curated.size()); }
    int updateCount() const { return m_packages.upgradableCount(); }
    bool checkingUpdates() const { return m_checkingUpdates; }
    bool aurEnabled() const { return m_aurEnabled; }
    void setAurEnabled(bool enabled);
    bool paruAvailable() const { return m_paruAvailable; }
    FlatpakClient* flatpak() { return &m_flatpak; }
    bool flatpakEnabled() const { return m_flatpakEnabled; }
    void setFlatpakEnabled(bool enabled);
    UpdateNotice* updateNotice() { return &m_updateNotice; }

    /// Re-reads the pacman databases and the AppStream catalog in the background.
    Q_INVOKABLE void reload();
    /// Everything known about one package. `name` is empty if it is not in the databases.
    Q_INVOKABLE Package package(const QString& name) const;

    Q_INVOKABLE void install(const QString& name);
    Q_INVOKABLE void remove(const QString& name);
    Q_INVOKABLE void upgradeAll();
    Q_INVOKABLE void installFromAur(const QString& name);
    /// Builds several reviewed AUR packages in one paru run.
    Q_INVOKABLE void installManyFromAur(const QStringList& names);

    Q_INVOKABLE void installFlatpak(const QString& id);
    /// Removes one installed copy, named by its `key`.
    Q_INVOKABLE void removeFlatpak(const QString& key);
    /// Updates every installed Flatpak app and runtime.
    Q_INVOKABLE void updateFlatpaks();
    /// Sets up Flathub as a system-wide remote.
    Q_INVOKABLE void addFlathub();

 signals:
    void loadingChanged();
    void revisionChanged();
    void curatedChanged();
    void checkingUpdatesChanged();
    void aurEnabledChanged();
    void paruAvailableChanged();
    void flatpakEnabledChanged();
    /// Something the user asked for could not be done, and `text` says why.
    void notice(const QString& text);

 private:
    void onLoaded(std::expected<catalog::LoadResult, QString> result);
    /// The network and subprocess checks that follow a database load.
    void refreshSources();
    void checkUpdates();
    void onUpdatesChecked(int exitCode, QProcess::ExitStatus status);
    /// Marks the updates checkupdates last listed again, after the package list was read anew.
    void applyKnownUpdates();
    /// Stores a setting and announces it. Returns false when nothing changed.
    bool store(bool& member, bool value, const QString& key, void (Backend::*changed)());

    PackageModel m_packages;
    AurClient m_aur;
    FlatpakClient m_flatpak;
    Transaction m_transaction;
    UpdateNotice m_updateNotice;
    IconProvider* m_icons;
    QSettings m_settings;
    QProcess m_checkUpdates;
    QFileSystemWatcher m_dbWatcher;  ///< notices pacman runs outside of this app
    QTimer m_dbChanged;
    catalog::CuratedList m_curatedList;
    QList<CuratedSection> m_curated;
    QHash<QString, QString> m_newVersions;  ///< name and new version, as checkupdates last listed them
    QString m_error;
    int m_revision         = 0;
    bool m_loading         = false;
    bool m_reloadQueued    = false;
    bool m_loadedOnce      = false;
    bool m_checkingUpdates = false;
    bool m_checkOutdated   = false;  ///< the databases were reloaded while checkupdates was running
    bool m_aurEnabled      = false;
    bool m_flatpakEnabled  = false;
    bool m_paruAvailable   = false;
};
