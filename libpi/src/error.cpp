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

#include "pi/error.hpp"

#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

using pi::ErrorCode;

// TODO(vnepogodin): refactor that using staticmap enum
[[nodiscard]] auto code_to_string(ErrorCode code) noexcept -> std::string_view {
    switch (code) {
    case ErrorCode::SubprocessFailed:
        return "SubprocessFailed"sv;
    case ErrorCode::FileIo:
        return "FileIo"sv;
    case ErrorCode::ParseError:
        return "ParseError"sv;
    case ErrorCode::InvalidArgument:
        return "InvalidArgument"sv;
    case ErrorCode::NotFound:
        return "NotFound"sv;
    case ErrorCode::PermissionDenied:
        return "PermissionDenied"sv;
    case ErrorCode::Unsupported:
        return "Unsupported"sv;
    case ErrorCode::Unknown:
        return "Unknown"sv;
    }
    return "Unknown"sv;
}

}  // namespace

namespace pi {

auto make_error(ErrorCode code, std::string context) noexcept -> std::unexpected<Error> {
    return std::unexpected<Error>{Error{.code = code, .context = std::move(context)}};
}

auto to_string(const Error& error) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("{}: {}"), code_to_string(error.code), error.context);
}

}  // namespace pi
