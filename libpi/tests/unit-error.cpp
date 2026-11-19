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

#include "doctest_compatibility.h"

#include "pi/error.hpp"

#include <string>

TEST_CASE("error type test")
{
    SECTION("make_error carries code and context")
    {
        const pi::Result<void> result = pi::make_error(pi::ErrorCode::FileIo, "boom");
        REQUIRE(!result.has_value());
        REQUIRE_EQ(result.error().code, pi::ErrorCode::FileIo);
        REQUIRE_EQ(result.error().context, std::string{"boom"});
    }

    SECTION("to_string renders <code>: <context>")
    {
        const pi::Error err{.code = pi::ErrorCode::SubprocessFailed, .context = "cmd failed"};
        REQUIRE_EQ(pi::to_string(err), std::string{"SubprocessFailed: cmd failed"});
    }

    SECTION("default Error is Unknown")
    {
        const pi::Error err{};
        REQUIRE_EQ(err.code, pi::ErrorCode::Unknown);
        REQUIRE_EQ(pi::to_string(err), std::string{"Unknown: "});
    }

    SECTION("Result<T> value path")
    {
        const pi::Result<int> ok = 42;
        REQUIRE(ok.has_value());
        REQUIRE_EQ(*ok, 42);
    }
}
