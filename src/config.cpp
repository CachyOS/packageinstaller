// Copyright (C) 2022 Vladislav Nepogodin
//
// This file is part of CachyOS new-cli-installer.
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

#include "config.hpp"

#include <memory>  // for unique_ptr, make_unique, operator==

static std::unique_ptr<Config> s_config = nullptr;

bool Config::initialize() noexcept {
    if (s_config != nullptr) {
        return false;
    }
    s_config = std::make_unique<Config>();
    if (s_config) {
        s_config->m_data["setupmode"] = false;
    }

    return s_config.get();
}

auto Config::instance() -> Config* {
    return s_config.get();
}
