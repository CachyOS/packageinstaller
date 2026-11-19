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

#ifndef FILE_UTILS_HPP
#define FILE_UTILS_HPP

#include "pi/error.hpp"
#include "pi/string_utils.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t

#include <concepts>     // for invocable
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for forward

namespace pi::file_utils {

enum class Links : std::uint8_t {
    Refuse,
    Follow,
};

auto read_whole_file(std::string_view filepath) noexcept -> std::string;
auto read_first_line(std::string_view filepath) noexcept -> std::string;
auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool;

// If the file doesn't exist, then it create one and write into it.
// If the file exists already, then it will overwrite file content with provided data.
auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool;

// Same as the bool helpers above, but fail with FileIo naming the path.
auto write_file(std::string_view filepath, std::string_view data) noexcept -> Result<void>;
auto create_directories(std::string_view path) noexcept -> Result<void>;
/// Overwrites @p to if it exists.
auto copy_file(std::string_view from, std::string_view to) noexcept -> Result<void>;

/// Rewrites @p filepath with functor.
template <std::invocable<std::string_view> Fn>
auto rewrite_lines(std::string_view filepath, Fn&& fn) noexcept -> Result<void> {
    return write_file(filepath, utils::map_lines(read_whole_file(filepath), std::forward<Fn>(fn)));
}

[[nodiscard]] auto read_whole_file(std::string_view filepath, std::size_t max_size, Links links) noexcept -> Result<std::string>;

}  // namespace pi::file_utils

#endif  // FILE_UTILS_HPP
