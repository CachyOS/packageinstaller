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

#ifndef VERSIONNUMBER_HPP
#define VERSIONNUMBER_HPP

#include <alpm.h>

#include <string_view>

#include <QMetaType>
#include <QString>
#include <QStringView>

#include <fmt/core.h>

class VersionNumber final {
 public:
    VersionNumber() noexcept                           = default;
    VersionNumber(const VersionNumber& value) noexcept = default;  // copy constructor
    explicit VersionNumber(const std::string_view& value) noexcept : str(value) { }
    ~VersionNumber() noexcept = default;

    [[nodiscard]] std::string_view toStringView() const noexcept { return str.c_str(); }
    [[nodiscard]] std::string toString() const noexcept { return str; }
    [[nodiscard]] QStringView toQStringView() const noexcept { return {reinterpret_cast<const QChar*>(str.c_str())}; }
    [[nodiscard]] QString toQString() const noexcept { return {str.c_str()}; }

    VersionNumber& operator=(const VersionNumber& value) noexcept = default;

    // Operators
    /* clang-format off */
    inline bool operator<(const VersionNumber& value) const noexcept
    { return (compare(*this, value) == -1); }
    inline bool operator<=(const VersionNumber& value) const noexcept
    { return !(*this > value); }
    inline bool operator>(const VersionNumber& value) const noexcept
    { return (compare(*this, value) == 1); }
    inline bool operator>=(const VersionNumber& value) const noexcept
    { return !(*this < value); }
    inline bool operator==(const VersionNumber& value) const noexcept
    { return (compare(*this, value) == 0); }
    inline bool operator!=(const VersionNumber& value) const noexcept
    { return !(*this == value); }
    /* clang-format on */

 private:
    std::string str{};  // full version string

    // -1 for >second, 1 for <second, 0 for equal
    [[nodiscard]] static inline int compare(const char* first, const char* second) noexcept { return alpm_pkg_vercmp(first, second); }
    [[nodiscard]] static inline int compare(const VersionNumber& first, const VersionNumber& second) noexcept { return compare(first.str.c_str(), second.str.c_str()); }
};

Q_DECLARE_METATYPE(VersionNumber)

// Custom formatter
template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_base_of<VersionNumber, T>::value, char>> : fmt::formatter<std::string> {
    template <typename FormatCtx>
    auto format(const VersionNumber& vernum, FormatCtx& ctx) {
        return fmt::formatter<std::string>::format(vernum.toString(), ctx);
    }
};

#endif  // VERSIONNUMBER_HPP
