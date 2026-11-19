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

#include <cstdint>  // for int32_t

#include <chrono>            // for seconds
#include <functional>        // for function
#include <initializer_list>  // for initializer_list
#include <span>              // for span
#include <string>            // for string

namespace pi::utils {

/// Per-command options.
struct RunOptions {
    std::chrono::seconds timeout{std::chrono::seconds{0}};
    std::span<const std::string> env{};
    /// Polled while the command runs, true kills it.
    std::function<bool()> cancel_requested{};
};

struct ProcessResult {
    bool timed_out{false};
    bool cancelled{false};

    std::int32_t exit_code{-1};
    /// Stdout only, stderr is dropped.
    std::string output{};

    [[nodiscard]] auto ok() const noexcept -> bool {
        return exit_code == 0 && !timed_out && !cancelled;
    }
};

/// argv[0] is looked up in PATH. LC_ALL is C.UTF-8 for the child.
auto run(std::span<const std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

auto run(std::initializer_list<std::string> argv, const RunOptions& opts = {}) noexcept -> ProcessResult;

}  // namespace pi::utils
