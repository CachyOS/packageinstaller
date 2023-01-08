// Copyright (C) 2022-2023 Vladislav Nepogodin
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

#include "versionnumber.hpp"

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
auto make_multiline(const std::string_view& str, bool reverse, const std::string_view&& delim) noexcept -> std::vector<std::string>;

auto make_multiline(const std::vector<std::string_view>& multiline, bool reverse, const std::string_view&& delim) noexcept -> std::string;

template <std::input_iterator I, std::sentinel_for<I> S>
auto make_multiline_range(I first, S last, bool reverse, const std::string_view&& delim) noexcept -> std::string {
    std::string res{};
    for (; first != last; ++first) {
        res += *first;
        res += delim.data();
    }

    if (reverse) {
        ::ranges::reverse(res);
    }

    return res;
}
}  // namespace utils

#endif  // UTILS_HPP
