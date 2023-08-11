// Copyright (C) 2022-2023 Vladislav Nepogodin
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "pacmancache.hpp"
#include "cmd.hpp"

#include <unordered_map>

static QStringList get_upgrade_packages(alpm_handle_t* handle) {
    if (handle == nullptr)
        return {};

    QStringList upgrade_packages;

    auto* localdb = alpm_get_localdb(handle);
    for (alpm_list_t* i = alpm_db_get_pkgcache(localdb); i != nullptr; i = i->next) {
        auto* lpkg           = reinterpret_cast<alpm_pkg_t*>(i->data);
        const char* pkg_name = alpm_pkg_get_name(lpkg);

        if (alpm_sync_get_new_version(lpkg, alpm_get_syncdbs(handle)) == nullptr) {
            continue;
        }

        upgrade_packages << QString::fromUtf8(pkg_name);
    }

    return upgrade_packages;
}

void PacmanCache::refresh_list() {
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;

    auto* dbs = alpm_get_syncdbs(m_handle);
    for (alpm_list_t* i = dbs; i != nullptr; i = i->next) {
        auto* db = reinterpret_cast<alpm_db_t*>(i->data);

        auto* pkgcache = alpm_db_get_pkgcache(db);
        for (alpm_list_t* j = pkgcache; j != nullptr; j = j->next) {
            auto* pkg            = reinterpret_cast<alpm_pkg_t*>(j->data);
            const char* pkg_name = alpm_pkg_get_name(pkg);
            const char* pkg_desc = alpm_pkg_get_desc(pkg);
            const char* pkg_ver  = alpm_pkg_get_version(pkg);

            package_list << pkg_name;
            version_list << pkg_ver;
            description_list << pkg_desc;
        }
    }

    m_upd_candidates = get_upgrade_packages(m_handle);

    for (int i = 0; i < package_list.size(); ++i) {
        if (m_candidates.contains(package_list.at(i)) && (VersionNumber(version_list.at(i).toStdString()) <= VersionNumber(m_candidates.at(package_list.at(i)).at(0).toStdString())))
            continue;
        m_candidates[package_list.at(i)] = (QStringList() << version_list.at(i) << description_list.at(i));
    }
}

QStringView PacmanCache::getArch() {
    // Pair of arch names returned by "uname" and corresponding DEB_BUILD_ARCH formats
    static const std::unordered_map<QString, QString> arch_names{
        {"x86_64", "amd64"},
        {"x86_64_v3", "amd64"},
        {"i686", "i386"},
        {"armv7l", "armhf"}};

    Cmd cmd;
    return arch_names.at(cmd.getCmdOut("uname -m", true));
}
