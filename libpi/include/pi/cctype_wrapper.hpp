// Copyright (C) 2024-2026 Vladislav Nepogodin
//
// This file is part of CachyOS GUCC.
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

#pragma once

namespace pi::utils {

// local-independent cctype wrapper constexpr variants.
[[nodiscard]] constexpr auto is_lower(char ch) noexcept -> bool { return ch >= 'a' && ch <= 'z'; }
[[nodiscard]] constexpr auto is_upper(char ch) noexcept -> bool { return ch >= 'A' && ch <= 'Z'; }
[[nodiscard]] constexpr auto is_digit(char ch) noexcept -> bool { return ch >= '0' && ch <= '9'; }
[[nodiscard]] constexpr auto is_alpha(char ch) noexcept -> bool { return is_lower(ch) || is_upper(ch); }
[[nodiscard]] constexpr auto is_alnum(char ch) noexcept -> bool { return is_alpha(ch) || is_digit(ch); }
[[nodiscard]] constexpr auto to_lower(char ch) noexcept -> char { return is_upper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch; }
[[nodiscard]] constexpr auto to_upper(char ch) noexcept -> char { return is_lower(ch) ? static_cast<char>(ch - 'a' + 'A') : ch; }

}  // namespace pi::utils
