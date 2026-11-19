// SPDX-License-Identifier: GPL-3.0-or-later
#include "pi/pacman_conf.hpp"
#include "pi/conf.hpp"
#include "pi/process.hpp"
#include "pi/string_utils.hpp"

#include <chrono>  // for seconds

using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace pi::pacman_conf {

auto parse(std::string_view output) noexcept -> Config {
    Config config;
    bool options = false;
    for (const auto line : utils::make_split_view(output)) {
        if (const auto section = conf::section_name(line)) {
            options = *section == "options"sv;
            if (!options) {
                config.repos.emplace_back(*section);
            }
            continue;
        }
        const auto pair = utils::split_once(line, '=');
        if (!options || !pair) {
            continue;
        }
        const auto key   = utils::trim(pair->first);
        const auto value = std::string{utils::trim(pair->second)};
        if (key == "DBPath"sv) {
            config.db_path = value;
        } else if (key == "IgnorePkg"sv) {
            config.ignore_pkg.push_back(value);
        } else if (key == "IgnoreGroup"sv) {
            config.ignore_group.push_back(value);
        }
    }
    return config;
}

auto read() noexcept -> Config {
    const auto result = utils::run({"pacman-conf"}, {.timeout = 10s});
    return result.ok() ? parse(result.output) : Config{};
}

}  // namespace pi::pacman_conf
