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

#ifndef LOCKFILE_HPP
#define LOCKFILE_HPP

#include <string>
#include <string_view>

class LockFile final {
 public:
    explicit LockFile(const std::string_view& file_path)
      : m_file_path(file_path.data()) { }

    bool isLocked() noexcept;
    bool lock() noexcept;
    bool unlock() noexcept;

 private:
    int m_fd{};
    std::string m_file_path{};
};

#endif  // LOCKFILE_HPP
