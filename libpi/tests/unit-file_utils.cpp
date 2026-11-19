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
#include "test_temp_root.hpp"

#include "pi/file_utils.hpp"

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using pi::tests::TempRoot;

TEST_CASE("read_whole_file")
{
  auto sink   = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
  auto logger = std::make_shared<spdlog::logger>("default", sink);
  spdlog::set_default_logger(logger);

  using pi::file_utils::read_whole_file;

  TempRoot root{"gucc-file-utils"};
  const auto path = (root.path() / "file").string();

  SECTION("regular file")
  {
    REQUIRE(pi::file_utils::write_file(path, "line1\nline2\n"sv));
    REQUIRE_EQ(read_whole_file(path), "line1\nline2\n");
  }
  SECTION("file larger than the initial buffer, with embedded NULs")
  {
    auto content    = std::string(100'000, 'x');
    content[50'000] = '\0';
    REQUIRE(pi::file_utils::write_file(path, content));
    REQUIRE_EQ(read_whole_file(path), content);
  }
  SECTION("empty file")
  {
    REQUIRE(pi::file_utils::write_file(path, ""sv));
    REQUIRE(read_whole_file(path).empty());
  }
  SECTION("missing file")
  {
    REQUIRE(read_whole_file((root.path() / "missing").string()).empty());
  }
  SECTION("directory")
  {
    REQUIRE(read_whole_file(root.path().string()).empty());
  }
  SECTION("procfs reports size 0 but has content")
  {
    const auto status = read_whole_file("/proc/self/status");
    REQUIRE_FALSE(status.empty());
    REQUIRE(status.contains("Name:"sv));
  }
  SECTION("path slice without a null terminator")
  {
    REQUIRE(pi::file_utils::write_file(path, "slice"sv));
    const auto padded = path + "-not-part-of-the-path";
    REQUIRE_EQ(read_whole_file(std::string_view{padded}.substr(0, path.size())), "slice");
  }
  SECTION("write helpers honour a slice too")
  {
    const auto padded = path + "-suffix";
    const auto slice  = std::string_view{padded}.substr(0, path.size());
    REQUIRE(pi::file_utils::create_file_for_overwrite(slice, "a"sv));
    REQUIRE(pi::file_utils::write_to_file("b"sv, slice));
    REQUIRE_EQ(read_whole_file(path), "b");
  }
}
