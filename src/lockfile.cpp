/**********************************************************************
 *  lockfile.cpp
 **********************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Package Installer.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-packageinstaller is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
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

#include "lockfile.hpp"

#include <cstdio>
#include <sys/file.h>
#include <unistd.h>

// Checks if file is locked by another process (if locked by the same process returns false)
bool LockFile::isLocked() noexcept {
    m_fd = open(m_file_path.data(), O_RDONLY);
    if (m_fd < -1) {
        std::perror("open");
        return false;
    }
    return (lockf(m_fd, F_TEST, 0) != 0);
}

bool LockFile::lock() noexcept {
    m_fd = open(m_file_path.data(), O_WRONLY);
    if (m_fd < -1) {
        std::perror("open");
        return false;
    }
    // Create a file lock
    return (lockf(m_fd, F_LOCK, 0) == 0);
}

bool LockFile::unlock() noexcept {
    m_fd = open(m_file_path.data(), O_WRONLY);
    close(m_fd);
    return true;
}
