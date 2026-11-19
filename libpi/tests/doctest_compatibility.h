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

#ifndef DOCTEST_COMPATIBILITY_H
#define DOCTEST_COMPATIBILITY_H

#define DOCTEST_CONFIG_VOID_CAST_EXPRESSIONS
#define DOCTEST_THREAD_LOCAL // enable single-threaded builds on XCode 6/7 - https://github.com/onqtam/doctest/issues/172
#include <doctest/doctest.h>

// Catch doesn't require a semicolon after CAPTURE but doctest does
#undef CAPTURE
#define CAPTURE(x) DOCTEST_CAPTURE(x);

// Sections from Catch are called Subcases in doctest and don't work with std::string by default
#undef SUBCASE
#define SECTION(x) DOCTEST_SUBCASE(x)

// convenience macro around INFO since it doesn't support temporaries (it is optimized to avoid allocations for runtime speed)
#define INFO_WITH_TEMP_IMPL(x, var_name) const auto var_name = x; INFO(var_name) // lvalue!
#define INFO_WITH_TEMP(x) INFO_WITH_TEMP_IMPL(x, DOCTEST_ANONYMOUS(DOCTEST_STD_STRING_))

// doctest doesn't support THROWS_WITH for std::string out of the box (has to include <string>...)
#define CHECK_THROWS_WITH_STD_STR_IMPL(expr, str, var_name)                    \
    do {                                                                       \
        std::string var_name = str;                                            \
        CHECK_THROWS_WITH(expr, var_name.c_str());                             \
    } while (false)
#define CHECK_THROWS_WITH_STD_STR(expr, str)                                   \
    CHECK_THROWS_WITH_STD_STR_IMPL(expr, str, DOCTEST_ANONYMOUS(DOCTEST_STD_STRING_))

// Catch does this by default
using doctest::Approx;

#endif
