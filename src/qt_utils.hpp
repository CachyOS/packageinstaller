// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for forward
#include <vector>       // for vector

#include <QString>
#include <QStringList>

namespace qt_utils {

[[nodiscard]] inline auto to_qstring(std::string_view str) -> QString {
    return QString::fromUtf8(str.data(), static_cast<qsizetype>(str.size()));
}

/// Strings or string views, read as UTF-8.
template <std::ranges::input_range R>
[[nodiscard]] inline auto to_qstring_list(R&& strings) -> QStringList {
    return std::forward<R>(strings) | std::views::transform(&to_qstring) | std::ranges::to<QStringList>();
}

[[nodiscard]] inline auto to_std_strings(const QStringList& strings) -> std::vector<std::string> {
    return strings | std::views::transform(&QString::toStdString) | std::ranges::to<std::vector>();
}

}  // namespace qt_utils
