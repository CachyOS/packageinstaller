// SPDX-License-Identifier: GPL-3.0-or-later
#include "alpmdb.hpp"
#include "process.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <alpm.h>

#include <functional>
#include <memory>

using namespace Qt::StringLiterals;

QString Package::source() const {
    if (foreign()) {
        return u"local"_s;
    }
    return repo.startsWith("cachyos"_L1) ? u"cachyos"_s : u"arch"_s;
}

namespace {

using Handle = std::unique_ptr<alpm_handle_t, decltype(&alpm_release)>;

struct Config {
    QString dbPath = u"/var/lib/pacman/"_s;
    QStringList repos;
    QStringList ignorePkg;
    QStringList ignoreGroup;
};

/// The parts of pacman.conf in use, read with a single `pacman-conf` run.
Config readConfig() {
    Config config;
    bool options = false;
    for (const auto& line : outputLines(u"pacman-conf"_s, {}, 10000)) {
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            const auto section = line.sliced(1, line.size() - 2);
            options            = section == "options"_L1;
            if (!options) {
                config.repos << section;
            }
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (!options || eq < 0) {
            continue;
        }
        const auto key   = QStringView(line).first(eq).trimmed();
        const auto value = QStringView(line).sliced(eq + 1).trimmed().toString();
        if (key == "DBPath"_L1) {
            config.dbPath = value;
        } else if (key == "IgnorePkg"_L1) {
            config.ignorePkg << value;
        } else if (key == "IgnoreGroup"_L1) {
            config.ignoreGroup << value;
        }
    }
    return config;
}

/// A handle with every sync repository registered, in pacman.conf order.
std::expected<Handle, QString> open(const Config& config) {
    alpm_errno_t err{};
    Handle handle(alpm_initialize("/", config.dbPath.toUtf8().constData(), &err), &alpm_release);
    if (!handle) {
        return std::unexpected(QCoreApplication::translate("alpmdb", "Failed to open the pacman database: %1")
                                   .arg(QString::fromUtf8(alpm_strerror(err))));
    }
    for (const auto& repo : config.repos) {
        alpm_register_syncdb(handle.get(), repo.toUtf8().constData(), ALPM_SIG_USE_DEFAULT);
    }
    // let libalpm match the names, patterns and groups the way pacman does
    for (const auto& pattern : config.ignorePkg) {
        alpm_option_add_ignorepkg(handle.get(), pattern.toUtf8().constData());
    }
    for (const auto& group : config.ignoreGroup) {
        alpm_option_add_ignoregroup(handle.get(), group.toUtf8().constData());
    }
    return handle;
}

/// Dependency and licence names repeat across thousands of packages; one copy of each is kept.
using Interned = QSet<QString>;

template <typename T, typename Proj = std::identity>
QStringList toStringList(alpm_list_t* list, Interned& interned, Proj proj = {}) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(alpm_list_count(list)));
    for (auto* it = list; it != nullptr; it = alpm_list_next(it)) {
        out << *interned.insert(QString::fromUtf8(std::invoke(proj, static_cast<T*>(it->data))));
    }
    return out;
}

Package fromAlpm(alpm_pkg_t* pkg, const QString& repo, Interned& interned) {
    Package p;
    p.name         = QString::fromUtf8(alpm_pkg_get_name(pkg));
    p.version      = QString::fromUtf8(alpm_pkg_get_version(pkg));
    p.description  = QString::fromUtf8(alpm_pkg_get_desc(pkg));
    p.url          = QString::fromUtf8(alpm_pkg_get_url(pkg));
    p.repo         = repo;
    p.licenses     = toStringList<const char>(alpm_pkg_get_licenses(pkg), interned);
    p.depends      = toStringList<alpm_depend_t>(alpm_pkg_get_depends(pkg), interned, &alpm_depend_t::name);
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
    const auto handle = open(readConfig());
    if (!handle) {
        return depends;
    }
    auto* installed = alpm_db_get_pkgcache(alpm_get_localdb(handle->get()));
    auto* sync      = alpm_get_syncdbs(handle->get());

    QStringList out;
    for (const auto& depend : depends) {
        const auto text = depend.toUtf8();
        if (alpm_find_satisfier(installed, text.constData()) == nullptr
            && alpm_find_dbs_satisfier(handle->get(), sync, text.constData()) == nullptr) {
            out << depend;
        }
    }
    return out;
}

std::expected<Databases, QString> load() {
    const auto config = readConfig();
    const auto opened = open(config);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    auto* handle = opened->get();

    Databases result{.packages = {}, .dbPath = config.dbPath};
    auto& packages = result.packages;
    QHash<QString, qsizetype> index;
    QList<alpm_pkg_t*> candidates;  // the repository package behind each entry
    Interned interned;

    for (auto* db = alpm_get_syncdbs(handle); db != nullptr; db = alpm_list_next(db)) {
        auto* sync      = static_cast<alpm_db_t*>(db->data);
        const auto repo = QString::fromUtf8(alpm_db_get_name(sync));
        for (auto* it = alpm_db_get_pkgcache(sync); it != nullptr; it = alpm_list_next(it)) {
            auto* pkg       = static_cast<alpm_pkg_t*>(it->data);
            const auto name = QString::fromUtf8(alpm_pkg_get_name(pkg));
            if (index.contains(name)) {
                continue;  // an earlier repository wins
            }
            index.insert(name, packages.size());
            packages << fromAlpm(pkg, repo, interned);
            candidates << pkg;
        }
    }

    for (auto* it = alpm_db_get_pkgcache(alpm_get_localdb(handle)); it != nullptr; it = alpm_list_next(it)) {
        auto* pkg          = static_cast<alpm_pkg_t*>(it->data);
        const auto name    = QString::fromUtf8(alpm_pkg_get_name(pkg));
        const auto version = QString::fromUtf8(alpm_pkg_get_version(pkg));
        if (const auto found = index.constFind(name); found != index.cend()) {
            auto& p            = packages[*found];
            p.installedVersion = version;
            // like pacman: held back when the installed package or its update is ignored,
            // which matters when a new version joins an ignored group
            p.ignored    = alpm_pkg_should_ignore(handle, pkg) != 0 || alpm_pkg_should_ignore(handle, candidates.at(*found)) != 0;
            p.upgradable = !p.ignored && vercmp(version, p.version) < 0;
        } else {
            auto p             = fromAlpm(pkg, u"local"_s, interned);
            p.installedVersion = version;
            p.ignored          = alpm_pkg_should_ignore(handle, pkg) != 0;
            packages << p;
        }
    }
    packages.squeeze();
    return result;
}

}  // namespace alpmdb
