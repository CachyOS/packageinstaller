// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "aurclient.hpp"
#include "catalog.hpp"
#include "packagemodel.hpp"
#include "transaction.hpp"

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class IconProvider;
class QQmlEngine;
class QJSEngine;

class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(PackageModel* packages READ packages CONSTANT)
    Q_PROPERTY(AurClient* aur READ aur CONSTANT)
    Q_PROPERTY(Transaction* transaction READ transaction CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QVariantList curated READ curated NOTIFY revisionChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY revisionChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY checkingUpdatesChanged)
    Q_PROPERTY(bool aurEnabled READ aurEnabled WRITE setAurEnabled NOTIFY aurEnabledChanged)
    Q_PROPERTY(bool paruAvailable READ paruAvailable CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

 public:
    explicit Backend(IconProvider* icons, QObject* parent = nullptr);

    static Backend* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] PackageModel* packages() { return &m_packages; }
    [[nodiscard]] AurClient* aur() { return &m_aur; }
    [[nodiscard]] Transaction* transaction() { return &m_transaction; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QVariantList curated() const { return m_curated; }
    [[nodiscard]] int updateCount() const { return m_packages.upgradableCount(); }
    [[nodiscard]] bool checkingUpdates() const { return m_checkingUpdates; }
    [[nodiscard]] bool aurEnabled() const { return m_aurEnabled; }
    void setAurEnabled(bool enabled);
    [[nodiscard]] bool paruAvailable() const { return m_paruAvailable; }
    [[nodiscard]] QString version() const { return QStringLiteral(PI_VERSION); }

    /// Re-reads the pacman databases and the AppStream catalog in the background.
    Q_INVOKABLE void reload();
    /// Everything known about one package; empty if it is not in the databases.
    Q_INVOKABLE [[nodiscard]] QVariantMap package(const QString& name) const;
    Q_INVOKABLE [[nodiscard]] QString formatSize(qint64 bytes) const;

    Q_INVOKABLE void install(const QString& name);
    Q_INVOKABLE void remove(const QString& name);
    Q_INVOKABLE void upgradeAll();
    Q_INVOKABLE void installFromAur(const QString& name);
    /// Builds several reviewed AUR packages in one paru run.
    Q_INVOKABLE void installManyFromAur(const QStringList& names);

 signals:
    void loadingChanged();
    void revisionChanged();
    void checkingUpdatesChanged();
    void aurEnabledChanged();

 private:
    void onLoaded();
    void loadCuratedList();
    void rebuildCurated();
    void checkUpdates();

    struct CuratedEntry {
        QString name;
        QStringList companions;  ///< installed together with `name` (codecs, plugins)
    };
    struct CuratedSection {
        QString title;
        QList<CuratedEntry> entries;
    };

    PackageModel m_packages;
    AurClient m_aur;
    Transaction m_transaction;
    IconProvider* m_icons;
    QFutureWatcher<LoadResult> m_watcher;
    QProcess m_checkUpdates;
    QFileSystemWatcher m_dbWatcher;  ///< notices pacman runs outside of this app
    QTimer m_dbChanged;
    QList<CuratedSection> m_sections;
    QHash<QString, QStringList> m_companions;
    QVariantList m_curated;
    QString m_error;
    int m_revision         = 0;
    bool m_loading         = false;
    bool m_reloadQueued    = false;
    bool m_checkingUpdates = false;
    bool m_aurEnabled      = false;
    bool m_paruAvailable   = false;
};
