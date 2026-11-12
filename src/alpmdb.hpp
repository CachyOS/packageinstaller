// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// One package as shown in the UI: libalpm data merged with AppStream metadata.
struct Package {
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

    [[nodiscard]] bool installed() const { return !installedVersion.isEmpty(); }
    [[nodiscard]] bool foreign() const { return repo == QLatin1String("local"); }
};

namespace alpmdb {

/// Reads the local and sync databases configured in pacman.conf (read-only, no lock).
/// Packages shadowed by a repository listed earlier are dropped, like pacman does.
/// `dbPath` receives pacman's DBPath, `error` a message when nothing could be read.
QList<Package> load(QString* dbPath, QString* error);

int vercmp(const QString& a, const QString& b);

/// The dependency strings ("foo>=1.2") that neither an installed package nor a package
/// in the repositories satisfies. Reads the databases, so call it off the GUI thread.
QStringList unsatisfied(const QStringList& depends);

}  // namespace alpmdb
