// SPDX-License-Identifier: GPL-3.0-or-later
#include "doctest_compatibility.h"

#include "pi/names.hpp"

#include <string>
#include <string_view>

using namespace std::string_view_literals;
using pi::utils::is_package_name;
using pi::utils::is_plain_name;
using pi::utils::is_repo_target;
using pi::utils::kLocalFileName;
using pi::utils::kPackageName;
using pi::utils::kPkgbase;

TEST_CASE("is_plain_name")
{
    SECTION("accepted")
    {
        CHECK(is_package_name("linux-cachyos"sv));
        CHECK(is_package_name("lib32-gcc-libs"sv));
        CHECK(is_package_name("python3.12"sv));
        CHECK(is_package_name("gtk+"sv));
        CHECK(is_package_name("foo_bar"sv));
        CHECK(is_package_name("foo@bar"sv));
        CHECK(is_package_name("Qt6-Base"sv));
        CHECK(is_package_name("0ad"sv));
        CHECK(is_plain_name("paru-bin"sv, kPkgbase));
    }
    SECTION("leading dot or dash")
    {
        CHECK_FALSE(is_package_name("-rf"sv));
        CHECK_FALSE(is_package_name("--sync"sv));
        CHECK_FALSE(is_package_name("."sv));
        CHECK_FALSE(is_package_name(".."sv));
        CHECK_FALSE(is_package_name(".hidden"sv));
    }
    SECTION("path parts")
    {
        CHECK_FALSE(is_package_name("../x"sv));
        CHECK_FALSE(is_package_name("a/b"sv));
        CHECK_FALSE(is_package_name("a/b/c"sv));
        CHECK_FALSE(is_package_name("/etc/passwd"sv));
        CHECK_FALSE(is_package_name("file:///etc/passwd"sv));
        CHECK_FALSE(is_package_name("~root"sv));
    }
    SECTION("empty")
    {
        CHECK_FALSE(is_package_name(""sv));
        CHECK_FALSE(is_plain_name(""sv, kPkgbase));
    }
    SECTION("size limits")
    {
        CHECK(is_package_name(std::string(255, 'a')));
        CHECK_FALSE(is_package_name(std::string(256, 'a')));
        CHECK(is_plain_name(std::string(128, 'a'), kPkgbase));
        CHECK_FALSE(is_plain_name(std::string(129, 'a'), kPkgbase));
    }
    SECTION("upper case only where allowed")
    {
        CHECK(is_plain_name("Foo"sv, kPackageName));
        CHECK_FALSE(is_plain_name("Foo"sv, kPkgbase));
    }
    SECTION("local file name")
    {
        CHECK(is_plain_name(std::string(128, 'a'), kLocalFileName));
        CHECK_FALSE(is_plain_name(std::string(129, 'a'), kLocalFileName));
        CHECK(is_plain_name("Fix-Build.patch"sv, kLocalFileName));
        CHECK(is_plain_name("foo@1.install"sv, kLocalFileName));
        CHECK_FALSE(is_plain_name(".hidden"sv, kLocalFileName));
        CHECK_FALSE(is_plain_name("-rf"sv, kLocalFileName));
    }
    SECTION("utf-8")
    {
        CHECK_FALSE(is_package_name("caf\xc3\xa9"sv));
        CHECK_FALSE(is_package_name("\xe2\x80\x8bpacman"sv));
        CHECK_FALSE(is_package_name("\xff"sv));
    }
    SECTION("control characters and blanks")
    {
        CHECK_FALSE(is_package_name("foo\nbar"sv));
        CHECK_FALSE(is_package_name("foo\tbar"sv));
        CHECK_FALSE(is_package_name("foo bar"sv));
        CHECK_FALSE(is_package_name("foo\x1b[31m"sv));
        CHECK_FALSE(is_package_name("foo\0bar"sv));
        CHECK_FALSE(is_package_name("foo;rm"sv));
        CHECK_FALSE(is_package_name("$(id)"sv));
    }
}

TEST_CASE("is_repo_target")
{
    SECTION("accepted")
    {
        CHECK(is_repo_target("firefox"sv));
        CHECK(is_repo_target("cachyos/linux-cachyos"sv));
    }
    SECTION("refused")
    {
        CHECK_FALSE(is_repo_target("a/b/c"sv));
        CHECK_FALSE(is_repo_target("../x"sv));
        CHECK_FALSE(is_repo_target("/x"sv));
        CHECK_FALSE(is_repo_target("x/"sv));
        CHECK_FALSE(is_repo_target("core/-rf"sv));
        CHECK_FALSE(is_repo_target("-core/x"sv));
        CHECK_FALSE(is_repo_target(""sv));
        CHECK_FALSE(is_repo_target("core/"sv));
    }
}

static_assert(is_repo_target("extra/firefox"));
static_assert(!is_package_name("-x"));
