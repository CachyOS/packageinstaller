// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace pi::pacman_conf {

struct Config {
    std::string db_path{"/var/lib/pacman/"};
    std::vector<std::string> repos{};
    std::vector<std::string> ignore_pkg{};
    std::vector<std::string> ignore_group{};
};

/// Reads the output of pacman-conf.
[[nodiscard]] auto parse(std::string_view output) noexcept -> Config;

/// Runs pacman-conf. Defaults when it fails.
[[nodiscard]] auto read() noexcept -> Config;

}  // namespace pi::pacman_conf
