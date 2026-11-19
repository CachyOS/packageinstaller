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

#include <cstdint>  // for uint8_t

#include <expected>  // for expected, unexpected
#include <string>    // for string
#include <utility>   // for forward

#include <fmt/compile.h>
#include <fmt/format.h>

namespace pi {

/// @brief Broad category of a gucc failure.
enum class ErrorCode : std::uint8_t {
    SubprocessFailed,  ///< spawned command didn't run, or exited non-zero
    FileIo,            ///< read/write/stat of a file or path failed
    ParseError,        ///< couldn't parse the input into what we expected
    InvalidArgument,   ///< caller passed something the function won't accept
    NotFound,          ///< file/device/entry isn't there
    PermissionDenied,  ///< not enough privileges
    Unsupported,       ///< not implemented, or not valid for this input
    Unknown,           ///< fallback when nothing else fits
};

/// @brief A gucc failure.
struct Error final {
    ErrorCode code{ErrorCode::Unknown};
    std::string context;  ///< what went wrong, in words
};

/// @brief Result type for gucc operations that can fail.
///
/// On success you get a T, on failure an Error.
template <class T>
using Result = std::expected<T, Error>;

/// @brief Make a failed result from a code and a message.
[[nodiscard]] auto make_error(ErrorCode code, std::string context) noexcept -> std::unexpected<Error>;

/// @brief Make a failed result, formatting its message.
///
/// @code
/// return pi::make_error(ErrorCode::FileIo, FMT_COMPILE("open {}: {}"), path, err);
/// @endcode
template <class S, class... Args>
    requires(fmt::is_compiled_string<S>::value || bool{FMT_CLANG_ANALYZER})
[[nodiscard]] auto make_error(ErrorCode code, const S& format, Args&&... args) noexcept -> std::unexpected<Error> {
    if constexpr (fmt::is_compiled_string<S>::value) {
        return make_error(code, fmt::format(format, std::forward<Args>(args)...));
    } else {
        return make_error(code, fmt::format(fmt::runtime(format), std::forward<Args>(args)...));
    }
}

/// @brief Format an Error as "code: context", handy for logs.
[[nodiscard]] auto to_string(const Error& error) noexcept -> std::string;

}  // namespace pi
