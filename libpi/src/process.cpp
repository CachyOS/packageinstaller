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

#include "pi/process.hpp"
#include "pi/string_utils.hpp"

#include "third_party/subprocess.h"

#include <unistd.h>  // for environ

#include <array>               // for array
#include <chrono>              // for steady_clock, milliseconds
#include <condition_variable>  // for condition_variable
#include <iterator>            // for unreachable_sentinel
#include <mutex>               // for mutex, scoped_lock, unique_lock
#include <ranges>              // for ranges::*
#include <string>              // for string
#include <string_view>         // for string_view
#include <thread>              // for jthread
#include <vector>              // for vector

#include <spdlog/spdlog.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

constexpr auto kPollInterval = 200ms;

constexpr auto as_string_views(char** raw) noexcept {
    // NOTE: that must be UB, but it works as of now
    return std::ranges::subrange(raw, std::unreachable_sentinel)
        | std::ranges::views::take_while([](const char* entry) { return entry != nullptr; })
        | std::ranges::views::transform([](const char* entry) { return std::string_view{entry}; });
}

constexpr auto build_environment() -> std::vector<std::string> {
    auto filtered_env = as_string_views(environ)
        | std::ranges::views::filter([](std::string_view entry) { return !entry.starts_with("LC_ALL="sv); })
        | std::ranges::to<std::vector<std::string>>();

    // lets force to show programs output in english, in UTF-8 for the GUI
    filtered_env.emplace_back("LC_ALL=C.UTF-8"s);
    return filtered_env;
}

auto c_string_array(std::span<const std::string> storage) -> std::vector<const char*> {
    auto ptrs = storage
        | std::ranges::views::transform(&std::string::c_str)
        | std::ranges::to<std::vector<const char*>>();
    ptrs.emplace_back(nullptr);
    return ptrs;
}

[[gnu::pure]] auto child_environment() -> const std::vector<const char*>& {
    static const std::vector<std::string> host_storage = build_environment();
    static const std::vector<const char*> host_ptrs    = c_string_array(host_storage);
    return host_ptrs;
}

}  // namespace

namespace pi::utils {

auto run(std::span<const std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    if (argv.empty()) {
        spdlog::error("[exec] refusing to run an empty command");
        return ProcessResult{.exit_code = -1};
    }
    if (opts.cancel_requested && opts.cancel_requested()) {
        return ProcessResult{.cancelled = true, .exit_code = -1};
    }

    auto argv_ptrs = c_string_array(argv);

    const auto& base_env = child_environment();
    std::vector<const char*> extended_env;
    if (!opts.env.empty()) {
        extended_env.assign(base_env.begin(), std::prev(base_env.end()));
        std::ranges::copy(opts.env | std::ranges::views::transform(&std::string::c_str), std::back_inserter(extended_env));
        extended_env.emplace_back(nullptr);
    }
    const auto& env_ptrs = opts.env.empty() ? base_env : extended_env;

    subprocess_s process{};
    const int options = subprocess_option_enable_async | subprocess_option_search_user_path;
    if (subprocess_create_ex(argv_ptrs.data(), options, env_ptrs.data(), nullptr, &process) != 0) {
        spdlog::error("[exec] failed to spawn: {}", utils::join(argv, ' '));
        return ProcessResult{.exit_code = -1};
    }

    // the child gets no input
    if (auto* child_stdin = subprocess_stdin(&process); child_stdin != nullptr) {
        std::fclose(child_stdin);
        process.stdin_file = nullptr;
    }

    // deadline propagation, killed only while not yet reaped
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done{};
    bool timed_out{};
    bool cancelled{};
    std::jthread watchdog;
    if (opts.timeout.count() > 0 || opts.cancel_requested) {
        watchdog = std::jthread([&] {
            const auto deadline = std::chrono::steady_clock::now() + opts.timeout;
            std::unique_lock<std::mutex> lock(done_mutex);
            while (!done_cv.wait_for(lock, kPollInterval, [&] { return done; })) {
                cancelled = opts.cancel_requested && opts.cancel_requested();
                timed_out = !cancelled && opts.timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline;
                if (cancelled || timed_out) {
                    subprocess_terminate(&process);
                    return;
                }
            }
        });
    }

    ProcessResult result{};
    {
        // an unread stderr pipe would block a chatty child
        const std::jthread drain([&process] {
            std::array<char, 8192> buf{};
            while (subprocess_read_stderr(&process, buf.data(), static_cast<std::uint32_t>(buf.size())) != 0) { }
        });

        std::array<char, 8192> buf{};
        std::uint32_t bytes_read{};
        do {
            bytes_read = subprocess_read_stdout(&process, buf.data(), static_cast<std::uint32_t>(buf.size()));
            result.output.append(buf.data(), bytes_read);
        } while (bytes_read != 0);
    }

    // wake timeout watcher
    {
        const std::scoped_lock lock(done_mutex);
        done = true;
    }
    done_cv.notify_all();
    if (watchdog.joinable()) {
        watchdog.join();
    }

    int ret{-1};
    if (subprocess_join(&process, &ret) != 0) {
        spdlog::error("[exec] failed to join: {}", utils::join(argv, ' '));
        ret = -1;
    }
    subprocess_destroy(&process);

    result.timed_out = timed_out;
    result.cancelled = cancelled;
    result.exit_code = ret;
    if (result.output.ends_with('\n')) {
        result.output.pop_back();
    }
    return result;
}

auto run(std::initializer_list<std::string> argv, const RunOptions& opts) noexcept -> ProcessResult {
    return run(std::span{argv.begin(), argv.end()}, opts);
}

}  // namespace pi::utils
