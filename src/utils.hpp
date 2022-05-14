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

#ifndef UTILS_HPP
#define UTILS_HPP

#include "ini.hpp"
#include "versionnumber.hpp"

#include <alpm.h>
#include <alpm_list.h>

#include <algorithm>  // for transform
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

#include <range/v3/algorithm/all_of.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#pragma clang diagnostic pop
#else
#include <ranges>
namespace ranges = std::ranges;
#endif

#if 0
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <QString>
#include <QStringList>

template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_base_of<QString, T>::value, char>> : fmt::formatter<std::string> {
    template <typename FormatCtx>
    auto format(const QString& str, FormatCtx& ctx) {
        return fmt::formatter<std::string>::format(str.toStdString(), ctx);
    }
};

namespace fmt {

template <>
struct formatter<std::pair<const QString, VersionNumber>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const std::pair<const QString, VersionNumber>& fp, FormatContext& ctx) {
        return format_to(ctx.out(), "\n{}{} {}{}", "{", fp.first, fp.second, "}");
    }
};

template <>
struct formatter<std::unordered_map<QString, VersionNumber>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const std::unordered_map<QString, VersionNumber>& fm, FormatContext& ctx) {
        return format_to(ctx.out(), "{}", fmt::join(fm.begin(), fm.end(), ""));
    }
};

template <>
struct formatter<QStringList> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QStringList& fm, FormatContext& ctx) {
        return format_to(ctx.out(), "{}", fmt::join(fm, ", "));
    }
};
}  // namespace fmt
#endif

namespace utils {
inline std::size_t replace_all(std::string& inout, const std::string_view& what, const std::string_view& with) {
    std::size_t count{};
    std::size_t pos{};
    while (std::string::npos != (pos = inout.find(what.data(), pos, what.length()))) {
        inout.replace(pos, what.length(), with.data(), with.length());
        pos += with.length(), ++count;
    }
    return count;
}

inline std::size_t remove_all(std::string& inout, const std::string_view& what) {
    return replace_all(inout, what, "");
}
auto make_multiline(const std::string_view& str, bool reverse, const std::string_view&& delim) noexcept -> std::vector<std::string> {
    static constexpr auto functor = [](auto&& rng) {
        return std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
    };
    static constexpr auto second = [](auto&& rng) { return rng != ""; };

#if defined(__clang__)
    const auto& splitted_view = str
        | ranges::views::split(delim);
    const auto& view_res = splitted_view
        | ranges::views::transform(functor);
#else
    auto view_res = str
        | ranges::views::split(delim)
        | ranges::views::transform(functor);
#endif

    std::vector<std::string> lines{};
    ranges::for_each(view_res | ranges::views::filter(second), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline(const std::vector<std::string_view>& multiline, bool reverse, const std::string_view&& delim) noexcept -> std::string {
    std::string res{};
    for (const auto& line : multiline) {
        res += line;
        res += delim.data();
    }

    if (reverse) {
        ranges::reverse(res);
    }

    return res;
}

template <std::input_iterator I, std::sentinel_for<I> S>
auto make_multiline(I first, S last, bool reverse, const std::string_view&& delim) noexcept -> std::string {
    std::string res{};
    for (; first != last; ++first) {
        res += *first;
        res += delim.data();
    }

    if (reverse) {
        ranges::reverse(res);
    }

    return res;
}
}  // namespace utils

namespace detail {

namespace {
    void parse_cachedirs(alpm_handle_t* handle) noexcept {
        static constexpr auto cachedir = "/var/cache/pacman/pkg/";

        alpm_list_t* cachedirs = nullptr;
        cachedirs              = alpm_list_add(cachedirs, const_cast<void*>(reinterpret_cast<const void*>(cachedir)));
        alpm_option_set_cachedirs(handle, cachedirs);
    }

    void parse_includes(alpm_handle_t* handle, alpm_db_t* db, const auto& section, const auto& file) noexcept {
        const auto* archs = alpm_option_get_architectures(handle);
        const auto* arch  = reinterpret_cast<const char*>(archs->data);

        mINI::INIFile file_nested(file);
        // next, create a structure that will hold data
        mINI::INIStructure mirrorlist;

        // now we can read the file
        file_nested.read(mirrorlist);
        for (const auto& mirror : mirrorlist) {
            auto repo = mirror.second.begin()->second;
            if (repo.starts_with("/")) {
                continue;
            }
            utils::replace_all(repo, "$arch", arch);
            utils::replace_all(repo, "$repo", section.c_str());
            alpm_db_add_server(db, repo.c_str());
        }
    }

    void parse_repos(alpm_handle_t* handle) noexcept {
        static constexpr auto pacman_conf_path = "/etc/pacman.conf";

        mINI::INIFile file(pacman_conf_path);
        // next, create a structure that will hold data
        mINI::INIStructure ini;

        // now we can read the file
        file.read(ini);
        for (const auto& it : ini) {
            const auto& section = it.first;
            const auto& nested  = it.second;
            if (section == "options") {
                for (const auto& it_nested : nested) {
                    if (it_nested.first != "architecture") {
                        continue;
                    }
                    // add CacheDir
                    const auto& archs = utils::make_multiline(it_nested.second, false, " ");
                    for (const auto& arch : archs) {
                        alpm_option_add_architecture(handle, arch.c_str());
                    }
                }
                continue;
            }
            auto* db = alpm_register_syncdb(handle, section.c_str(), ALPM_SIG_USE_DEFAULT);

            for (const auto& it_nested : nested) {
                const auto& param = it_nested.first;
                const auto& value = it_nested.second;
                if (param == "include") {
                    parse_includes(handle, db, section, value);
                }
            }
        }
    }
}  // namespace

void parse_config(alpm_handle_t* handle) noexcept {
    parse_repos(handle);
    parse_cachedirs(handle);
}
}  // namespace detail

void update_pacman(alpm_handle_t* handle) noexcept {
    detail::parse_config(handle);
}

#endif  // UTILS_HPP
