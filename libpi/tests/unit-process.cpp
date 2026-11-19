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

#include "pi/process.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using pi::utils::ProcessResult;
using pi::utils::run;
using pi::utils::RunOptions;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {
auto run_shell(const std::string& cmdline, const RunOptions& opts = {}) -> ProcessResult {
    return run({"/bin/sh", "-c", cmdline}, opts);
}
}  // namespace

TEST_CASE("process")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);

    SECTION("stdin closed")
    {
        const auto res = run({"/usr/bin/cat"});
        REQUIRE(res.ok());
        REQUIRE(res.output.empty());
    }
    SECTION("extra env visible to child")
    {
        const std::vector<std::string> env{"COS_TEST_VAR=hello"};
        RunOptions opts{};
        opts.env       = env;
        const auto res = run_shell("printf '%s' \"$COS_TEST_VAR\"", opts);
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "hello"sv);
    }
    SECTION("trailing newline")
    {
        const std::vector<std::string> argv{"/usr/bin/echo", "hello"};
        const auto res = run(argv);
        REQUIRE(res.ok());
        REQUIRE_EQ(res.exit_code, 0);
        REQUIRE_EQ(res.output, "hello"sv);
    }
    SECTION("lines kept")
    {
        const auto res = run_shell("printf 'a\\tb\\n\\nc\\n'");
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "a\tb\n\nc"sv);
    }
    SECTION("exit is reported")
    {
        const auto res = run_shell("exit 3");
        REQUIRE_EQ(res.exit_code, 3);
        REQUIRE(!res.ok());
    }
    SECTION("killed by a signal is not ok")
    {
        const auto res = run_shell("kill -9 $$");
        REQUIRE(!res.ok());
        REQUIRE_FALSE(res.timed_out);
        REQUIRE_FALSE(res.cancelled);
    }
    SECTION("missing binary fails instead of hanging")
    {
        RunOptions opts{};
        opts.timeout   = 5s;
        const auto res = run({"/usr/bin/cos-test-does-not-exist"}, opts);
        REQUIRE_FALSE(res.timed_out);
        REQUIRE_EQ(res.exit_code, -1);
    }
    SECTION("missing binary in PATH")
    {
        const auto res = run({"cos-test-does-not-exist"});
        REQUIRE(!res.ok());
        REQUIRE_EQ(res.exit_code, -1);
    }
    SECTION("program looked up in PATH")
    {
        const auto res = run({"echo", "found"});
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "found"sv);
    }
    SECTION("empty command")
    {
        const auto res = run(std::vector<std::string>{});
        REQUIRE(!res.ok());
        REQUIRE_EQ(res.exit_code, -1);
    }
    SECTION("stdout and stderr kept apart")
    {
        const auto res = run_shell("echo out; echo err 1>&2");
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "out"sv);
    }
    SECTION("a lot of stderr does not block")
    {
        RunOptions opts{};
        opts.timeout   = 20s;
        const auto res = run_shell("head -c 1000000 /dev/zero 1>&2; echo done", opts);
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "done"sv);
    }
    SECTION("large output")
    {
        const auto res = run_shell("head -c 1000000 /dev/zero | tr '\\0' x");
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output.size(), 1000000);
    }
    SECTION("LC_ALL set")
    {
        const auto res = run_shell("printf 'LC=%s PATH_SET=%s' \"$LC_ALL\" \"${PATH:+yes}\"");
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "LC=C.UTF-8 PATH_SET=yes"sv);
    }
    SECTION("timeout")
    {
        RunOptions opts{};
        opts.timeout       = 1s;
        const auto start   = std::chrono::steady_clock::now();
        const auto res     = run({"/usr/bin/sleep", "30"}, opts);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(res.timed_out);
        REQUIRE_FALSE(res.cancelled);
        REQUIRE(!res.ok());
        REQUIRE(elapsed < 10s);
    }
    SECTION("finishes before the timeout")
    {
        RunOptions opts{};
        opts.timeout   = 30s;
        const auto res = run({"/usr/bin/echo", "quick"}, opts);
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "quick"sv);
    }
    SECTION("cancel before")
    {
        RunOptions opts{};
        opts.cancel_requested = [] { return true; };
        const auto res        = run({"/usr/bin/echo", "nope"}, opts);
        REQUIRE(res.cancelled);
        REQUIRE(!res.ok());
        REQUIRE(res.output.empty());
    }
    SECTION("cancel mid run")
    {
        std::atomic_bool cancel{false};
        RunOptions opts{};
        opts.cancel_requested = [&cancel] { return cancel.load(); };
        ProcessResult res;
        const auto start = std::chrono::steady_clock::now();
        std::thread worker([&] { res = run_shell("sleep 30", opts); });
        std::this_thread::sleep_for(150ms);
        cancel.store(true);
        worker.join();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        REQUIRE(res.cancelled);
        REQUIRE_FALSE(res.timed_out);
        REQUIRE(!res.ok());
        REQUIRE(elapsed < 10s);
    }
    SECTION("not cancelled")
    {
        RunOptions opts{};
        opts.cancel_requested = [] { return false; };
        const auto res        = run_shell("sleep 0.5; echo kept", opts);
        REQUIRE(res.ok());
        REQUIRE_EQ(res.output, "kept"sv);
    }
}
