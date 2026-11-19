// SPDX-License-Identifier: GPL-3.0-or-later
#include "doctest_compatibility.h"
#include "test_temp_root.hpp"

#include "pi/file_utils.hpp"

#include <sys/stat.h>

#include <filesystem>
#include <string>
#include <string_view>

using namespace std::string_view_literals;
using pi::file_utils::Links;
using pi::file_utils::read_whole_file;
using pi::tests::TempRoot;

TEST_CASE("read_whole_file with size cap")
{
    TempRoot root{"pi-file-utils-capped"};
    const auto path   = (root.path() / "file").string();
    const auto target = (root.path() / "target").string();
    const auto link   = (root.path() / "link").string();

    SECTION("regular file")
    {
        REQUIRE(pi::file_utils::write_file(path, "line1\nline2\n"sv));
        const auto text = read_whole_file(path, 64, Links::Refuse);
        REQUIRE(text.has_value());
        CHECK_EQ(*text, "line1\nline2\n");
    }
    SECTION("empty file is not an error")
    {
        REQUIRE(pi::file_utils::write_file(path, ""sv));
        const auto text = read_whole_file(path, 64, Links::Refuse);
        REQUIRE(text.has_value());
        CHECK(text->empty());
    }
    SECTION("exactly the cap")
    {
        REQUIRE(pi::file_utils::write_file(path, std::string(4096, 'x')));
        CHECK_EQ(read_whole_file(path, 4096, Links::Refuse).value_or(""), std::string(4096, 'x'));
    }
    SECTION("larger than a read chunk")
    {
        auto content    = std::string(100'000, 'x');
        content[50'000] = '\0';
        REQUIRE(pi::file_utils::write_file(path, content));
        CHECK_EQ(read_whole_file(path, content.size(), Links::Refuse).value_or(""), content);
    }
    SECTION("oversized")
    {
        REQUIRE(pi::file_utils::write_file(path, std::string(4097, 'x')));
        const auto text = read_whole_file(path, 4096, Links::Refuse);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error().code, pi::ErrorCode::InvalidArgument);
    }
    SECTION("symlink refused")
    {
        REQUIRE(pi::file_utils::write_file(target, "secret"sv));
        std::filesystem::create_symlink(target, link);
        CHECK_FALSE(read_whole_file(link, 64, Links::Refuse).has_value());
    }
    SECTION("symlink followed")
    {
        REQUIRE(pi::file_utils::write_file(target, "shared"sv));
        std::filesystem::create_symlink(target, link);
        CHECK_EQ(read_whole_file(link, 64, Links::Follow).value_or(""), "shared");
    }
    SECTION("dangling symlink")
    {
        std::filesystem::create_symlink(root.path() / "nowhere", link);
        CHECK_FALSE(read_whole_file(link, 64, Links::Refuse).has_value());
        CHECK_FALSE(read_whole_file(link, 64, Links::Follow).has_value());
    }
    SECTION("symlink to an oversized file")
    {
        REQUIRE(pi::file_utils::write_file(target, std::string(65, 'x')));
        std::filesystem::create_symlink(target, link);
        CHECK_FALSE(read_whole_file(link, 64, Links::Follow).has_value());
    }
    SECTION("directory")
    {
        const auto text = read_whole_file(root.path().string(), 64, Links::Follow);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error().code, pi::ErrorCode::InvalidArgument);
    }
    SECTION("fifo does not block")
    {
        const auto fifo = (root.path() / "fifo").string();
        REQUIRE_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        CHECK_FALSE(read_whole_file(fifo, 64, Links::Refuse).has_value());
        CHECK_FALSE(read_whole_file(fifo, 64, Links::Follow).has_value());
    }
    SECTION("device")
    {
        CHECK_FALSE(read_whole_file("/dev/zero", 64, Links::Follow).has_value());
    }
    SECTION("missing")
    {
        const auto text = read_whole_file((root.path() / "missing").string(), 64, Links::Follow);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error().code, pi::ErrorCode::FileIo);
    }
    SECTION("path slice without a null terminator")
    {
        REQUIRE(pi::file_utils::write_file(path, "slice"sv));
        const auto padded = path + "-not-part-of-the-path";
        CHECK_EQ(read_whole_file(std::string_view{padded}.substr(0, path.size()), 64, Links::Refuse).value_or(""), "slice");
    }
}
