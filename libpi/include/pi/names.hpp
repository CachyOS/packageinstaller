// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pi/cctype_wrapper.hpp"

#include <cstddef>  // for size_t

#include <algorithm>    // for all_of
#include <string_view>  // for string_view

namespace pi::utils {

struct NameRules {
    std::size_t max_size{};
    bool upper{};
};

inline constexpr NameRules kPackageName{.max_size = 255, .upper = true};
// an AUR package base ends up in a directory name and in URLs
inline constexpr NameRules kPkgbase{.max_size = 128, .upper = false};
// a source or install file a PKGBUILD names next to itself
inline constexpr NameRules kLocalFileName{.max_size = 128, .upper = true};

/// Letters, digits and "@._+-", not starting with "." or "-": a name that stays inside its directory.
[[nodiscard]] constexpr auto is_plain_name(std::string_view name, NameRules rules) noexcept -> bool {
    const auto allowed = [rules](char ch) {
        return is_lower(ch) || is_digit(ch) || (rules.upper && is_upper(ch)) || std::string_view{"@._+-"}.contains(ch);
    };
    return !name.empty() && name.size() <= rules.max_size && name.front() != '.' && name.front() != '-'
        && std::ranges::all_of(name, allowed);
}

[[nodiscard]] constexpr auto is_package_name(std::string_view name) noexcept -> bool {
    return is_plain_name(name, kPackageName);
}

/// "name" or "repo/name".
[[nodiscard]] constexpr auto is_repo_target(std::string_view target) noexcept -> bool {
    const auto slash = target.find('/');
    if (slash == std::string_view::npos) {
        return is_package_name(target);
    }
    return is_package_name(target.substr(0, slash)) && is_package_name(target.substr(slash + 1));
}

}  // namespace pi::utils
