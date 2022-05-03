// Copyright (C) 2022 Vladislav Nepogodin
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

#ifndef PACMANCACHE_HPP
#define PACMANCACHE_HPP

#include "versionnumber.hpp"

#include <alpm.h>

#include <map>

class PacmanCache {
 public:
    explicit PacmanCache(alpm_handle_t* handle) : m_handle(handle) { refresh_list(); }

    void refresh_list();
    [[nodiscard]] std::map<QString, QStringList> get_candidates() const { return m_candidates; }

    static QString getArch();

 private:
    std::map<QString, QStringList> m_candidates;
    alpm_handle_t* m_handle{};
};

#endif  // PACMANCACHE_HPP
