// SPDX-License-Identifier: GPL-3.0-or-later
#include "doctest_compatibility.h"

#include "pi/pacman_conf.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

TEST_CASE("pacman_conf parse")
{
    SECTION("options and repositories in order")
    {
        const auto config = pi::pacman_conf::parse(
            "[options]\nRootDir = /\nDBPath = /var/lib/pacman/\nIgnorePkg = linux\nIgnorePkg = mesa\n"
            "IgnoreGroup = gnome\n[cachyos-v3]\nServer = https://example.org/$repo\n[core]\n[extra]\n"sv);
        CHECK_EQ(config.db_path, "/var/lib/pacman/");
        CHECK_EQ(config.repos, std::vector<std::string>{"cachyos-v3", "core", "extra"});
        CHECK_EQ(config.ignore_pkg, std::vector<std::string>{"linux", "mesa"});
        CHECK_EQ(config.ignore_group, std::vector<std::string>{"gnome"});
    }
    SECTION("keys outside options are ignored")
    {
        const auto config = pi::pacman_conf::parse("[core]\nIgnorePkg = linux\nDBPath = /tmp\n"sv);
        CHECK_EQ(config.db_path, "/var/lib/pacman/");
        CHECK(config.ignore_pkg.empty());
        REQUIRE_EQ(config.repos.size(), 1);
    }
    SECTION("a second options section still counts")
    {
        const auto config = pi::pacman_conf::parse("[options]\n[core]\n[options]\nIgnorePkg = a\n"sv);
        CHECK_EQ(config.repos, std::vector<std::string>{"core"});
        CHECK_EQ(config.ignore_pkg, std::vector<std::string>{"a"});
    }
    SECTION("empty output")
    {
        const auto config = pi::pacman_conf::parse(""sv);
        CHECK_EQ(config.db_path, "/var/lib/pacman/");
        CHECK(config.repos.empty());
    }
}
