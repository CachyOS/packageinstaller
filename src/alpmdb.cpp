// SPDX-License-Identifier: GPL-3.0-or-later
#include "alpmdb.hpp"

#include <QHash>
#include <QProcess>

#include <alpm.h>

namespace {

QStringList pacmanConf(const QStringList& args) {
    QProcess proc;
    proc.start(QStringLiteral("pacman-conf"), args);
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QStringList stringList(alpm_list_t* list) {
    QStringList out;
    for (auto* it = list; it != nullptr; it = alpm_list_next(it)) {
        out << QString::fromUtf8(static_cast<const char*>(it->data));
    }
    return out;
}

QStringList dependList(alpm_list_t* list) {
    QStringList out;
    for (auto* it = list; it != nullptr; it = alpm_list_next(it)) {
        out << QString::fromUtf8(static_cast<alpm_depend_t*>(it->data)->name);
    }
    return out;
}

Package fromAlpm(alpm_pkg_t* pkg, const QString& repo) {
    Package p;
    p.name         = QString::fromUtf8(alpm_pkg_get_name(pkg));
    p.version      = QString::fromUtf8(alpm_pkg_get_version(pkg));
    p.description  = QString::fromUtf8(alpm_pkg_get_desc(pkg));
    p.url          = QString::fromUtf8(alpm_pkg_get_url(pkg));
    p.repo         = repo;
    p.licenses     = stringList(alpm_pkg_get_licenses(pkg));
    p.depends      = dependList(alpm_pkg_get_depends(pkg));
    p.installSize  = static_cast<qint64>(alpm_pkg_get_isize(pkg));
    p.downloadSize = static_cast<qint64>(alpm_pkg_get_size(pkg));
    p.title        = p.name;
    p.summary      = p.description;
    return p;
}

}  // namespace

namespace alpmdb {

int vercmp(const QString& a, const QString& b) {
    return alpm_pkg_vercmp(a.toUtf8().constData(), b.toUtf8().constData());
}

QStringList unsatisfied(const QStringList& depends) {
    const auto dbPath = pacmanConf({QStringLiteral("DBPath")}).value(0, QStringLiteral("/var/lib/pacman/"));
    const auto repos  = pacmanConf({QStringLiteral("--repo-list")});

    alpm_errno_t err{};
    alpm_handle_t* handle = alpm_initialize("/", dbPath.toUtf8().constData(), &err);
    if (handle == nullptr) {
        return depends;
    }
    for (const auto& repo : repos) {
        alpm_register_syncdb(handle, repo.toUtf8().constData(), ALPM_SIG_USE_DEFAULT);
    }
    auto* installed = alpm_db_get_pkgcache(alpm_get_localdb(handle));
    auto* sync      = alpm_get_syncdbs(handle);

    QStringList out;
    for (const auto& depend : depends) {
        const auto text = depend.toUtf8();
        if (alpm_find_satisfier(installed, text.constData()) == nullptr
            && alpm_find_dbs_satisfier(handle, sync, text.constData()) == nullptr) {
            out << depend;
        }
    }
    alpm_release(handle);
    return out;
}

QList<Package> load(QString* dbPathOut, QString* error) {
    const auto dbPath = pacmanConf({QStringLiteral("DBPath")}).value(0, QStringLiteral("/var/lib/pacman/"));
    if (dbPathOut != nullptr) {
        *dbPathOut = dbPath;
    }
    const auto repos  = pacmanConf({QStringLiteral("--repo-list")});

    alpm_errno_t err{};
    alpm_handle_t* handle = alpm_initialize("/", dbPath.toUtf8().constData(), &err);
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to open the pacman database: %1").arg(QString::fromUtf8(alpm_strerror(err)));
        }
        return {};
    }

    QList<Package> packages;
    QHash<QString, qsizetype> index;

    for (const auto& repo : repos) {
        alpm_db_t* db = alpm_register_syncdb(handle, repo.toUtf8().constData(), ALPM_SIG_USE_DEFAULT);
        if (db == nullptr) {
            continue;
        }
        for (auto* it = alpm_db_get_pkgcache(db); it != nullptr; it = alpm_list_next(it)) {
            auto* pkg       = static_cast<alpm_pkg_t*>(it->data);
            const auto name = QString::fromUtf8(alpm_pkg_get_name(pkg));
            if (index.contains(name)) {
                continue;  // an earlier repository wins
            }
            index.insert(name, packages.size());
            packages << fromAlpm(pkg, repo);
        }
    }

    alpm_db_t* local = alpm_get_localdb(handle);
    for (auto* it = alpm_db_get_pkgcache(local); it != nullptr; it = alpm_list_next(it)) {
        auto* pkg          = static_cast<alpm_pkg_t*>(it->data);
        const auto name    = QString::fromUtf8(alpm_pkg_get_name(pkg));
        const auto version = QString::fromUtf8(alpm_pkg_get_version(pkg));
        if (const auto found = index.constFind(name); found != index.cend()) {
            auto& p            = packages[*found];
            p.installedVersion = version;
            p.upgradable       = alpm_pkg_vercmp(alpm_pkg_get_version(pkg), p.version.toUtf8().constData()) < 0;
        } else {
            auto p             = fromAlpm(pkg, QStringLiteral("local"));
            p.installedVersion = version;
            packages << p;
        }
    }

    alpm_release(handle);

    if (packages.isEmpty() && error != nullptr) {
        *error = QStringLiteral("The pacman databases are empty. Refresh them with a system update.");
    }
    return packages;
}

}  // namespace alpmdb
