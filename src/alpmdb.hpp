// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <expected>

/// One package as shown in the UI: libalpm data merged with AppStream metadata.
struct Package {
    Q_GADGET
    QML_VALUE_TYPE(packageInfo)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QString installedVersion MEMBER installedVersion FINAL)
    Q_PROPERTY(QString description MEMBER description FINAL)
    Q_PROPERTY(QString repo MEMBER repo FINAL)
    Q_PROPERTY(QString url MEMBER url FINAL)
    Q_PROPERTY(QStringList licenses MEMBER licenses FINAL)
    Q_PROPERTY(QStringList depends MEMBER depends FINAL)
    Q_PROPERTY(qint64 installSize MEMBER installSize FINAL)
    Q_PROPERTY(qint64 downloadSize MEMBER downloadSize FINAL)
    Q_PROPERTY(bool upgradable MEMBER upgradable FINAL)
    Q_PROPERTY(bool ignored MEMBER ignored FINAL)
    Q_PROPERTY(bool isApp MEMBER isApp FINAL)
    Q_PROPERTY(QString title MEMBER title FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
    Q_PROPERTY(QString longDescription MEMBER longDescription FINAL)
    Q_PROPERTY(QString developer MEMBER developer FINAL)
    Q_PROPERTY(QStringList categories MEMBER categories FINAL)
    Q_PROPERTY(QStringList screenshots MEMBER screenshots FINAL)
    Q_PROPERTY(bool installed READ installed FINAL)
    Q_PROPERTY(bool foreign READ foreign FINAL)
    Q_PROPERTY(QString source READ source FINAL)

 public:
    QString name;
    QString version;           ///< version in the sync databases (or installed version for foreign packages)
    QString installedVersion;  ///< empty when not installed
    QString description;
    QString repo;              ///< sync repository, or "local" for foreign (AUR / manually installed) packages
    QString url;
    QStringList licenses;
    QStringList depends;
    qint64 installSize  = 0;
    qint64 downloadSize = 0;
    bool upgradable     = false;
    bool ignored        = false;  ///< held back by IgnorePkg or IgnoreGroup in pacman.conf

    // AppStream
    bool isApp = false;
    QString title;  ///< human readable name, falls back to `name`
    QString summary;
    QString longDescription;
    QString developer;
    QStringList categories;
    QStringList screenshots;

    bool installed() const { return !installedVersion.isEmpty(); }
    bool foreign() const {
        using namespace Qt::StringLiterals;
        return repo == "local"_L1;
    }
    /// "local", "cachyos" or "arch".
    QString source() const;
};

namespace alpmdb {

struct Databases {
    QList<Package> packages;
    QString dbPath;  ///< pacman's DBPath
};

/// Reads the local and sync databases configured in pacman.conf (read-only, no lock).
/// Packages shadowed by a repository listed earlier are dropped, like pacman does.
std::expected<Databases, QString> load();

int vercmp(const QString& a, const QString& b);

/// The dependency strings ("foo>=1.2") that neither an installed package nor a package
/// in the repositories satisfies. Opens its own handle, so it may run on any thread.
QStringList unsatisfied(const QStringList& depends);

}  // namespace alpmdb
