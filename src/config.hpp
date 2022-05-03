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

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>         // for string
#include <string_view>    // for string_view, hash
#include <unordered_map>  // for unordered_map
#include <vector>         // for vector

class Config final {
 public:
    using value_type      = std::unordered_map<std::string_view, bool>;
    using reference       = value_type&;
    using const_reference = const value_type&;

    Config() noexcept          = default;
    virtual ~Config() noexcept = default;

    static bool initialize() noexcept;
    [[gnu::pure]] static Config* instance();

    /* clang-format off */

    // Element access.
    auto data() noexcept -> reference
    { return m_data; }
    auto data() const noexcept -> const_reference
    { return m_data; }

    /* clang-format on */

 private:
    value_type m_data{};
};

#endif  // CONFIG_HPP
