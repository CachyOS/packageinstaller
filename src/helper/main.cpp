// SPDX-License-Identifier: GPL-3.0-or-later
//
// cachyos-pi-helper: the only part of the package installer that runs as root.
// It is started through pkexec and accepts a fixed set of verbs, which it maps
// to pacman invocations. Package names are validated and never reach a shell.
//
// The "pacman" verb exists for paru, which is started with this helper in place of
// sudo. It accepts only the three operations paru uses to install what it built, so
// that one authentication covers a whole build instead of one per step.

#include <cerrno>   // for errno
#include <csignal>  // for signal
#include <cstdio>   // for stderr
#include <cstdlib>  // for getenv

#include <algorithm>     // for all_of, contains, find
#include <array>         // for array
#include <filesystem>    // for path, canonical, is_regular_file
#include <optional>      // for optional
#include <print>         // for println
#include <ranges>        // for ranges::*
#include <span>          // for span
#include <string>        // for string
#include <string_view>   // for string_view
#include <system_error>  // for error_code, generic_category
#include <utility>       // for pair
#include <vector>        // for vector

#include <unistd.h>  // for setsid, execve

namespace fs = std::filesystem;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

constexpr auto kPacman = "/usr/bin/pacman"sv;

// pkexec itself answers 126 and 127 when the user is not let through
constexpr int kRefused    = 2;
constexpr int kNotStarted = 127;

constexpr std::array kParuOperations{"--sync"sv, "--upgrade"sv, "--database"sv};

// besides --noconfirm, which every operation takes
constexpr std::array kParuOptions{
    std::pair{"--sync"sv, "--needed"sv},
    std::pair{"--database"sv, "--asdeps"sv},
    std::pair{"--database"sv, "--asexplicit"sv},
};

using Args      = std::vector<std::string>;
using Arguments = std::span<const std::string_view>;

constexpr auto is_lower_or_digit(char c) noexcept -> bool {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

constexpr auto is_package_name(std::string_view name) noexcept -> bool {
    if (name.empty() || name.size() > 255 || name.front() == '-' || name.front() == '.') {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return is_lower_or_digit(c) || (c >= 'A' && c <= 'Z') || "@._+-"sv.contains(c);
    });
}

constexpr auto is_repo_target(std::string_view target) noexcept -> bool {
    const auto slash = target.find('/');
    if (slash == std::string_view::npos) {
        return is_package_name(target);
    }
    return is_package_name(target.substr(0, slash)) && is_package_name(target.substr(slash + 1));
}

constexpr auto is_package_file_name(std::string_view name) noexcept -> bool {
    constexpr auto kMarker = ".pkg.tar"sv;
    const auto marker      = name.rfind(kMarker);
    if (marker == std::string_view::npos || marker == 0) {
        return false;
    }
    const auto ending = name.substr(marker + kMarker.size());
    return ending.empty()
        || (ending.size() > 1 && ending.front() == '.'
            && std::ranges::all_of(ending.substr(1), [](char c) { return is_lower_or_digit(c) || c == 'Z'; }));
}

auto resolve_package_file(std::string_view given) noexcept -> std::optional<std::string> {
    const fs::path path{given};
    if (!path.is_absolute()) {
        return std::nullopt;
    }
    // links and ".." are followed first, so the file that was checked is the one pacman is given
    std::error_code ec{};
    const auto resolved = fs::canonical(path, ec);
    if (ec || !is_package_file_name(resolved.filename().native()) || !fs::is_regular_file(resolved, ec)) {
        return std::nullopt;
    }
    return resolved.native();
}

auto usage() noexcept -> std::optional<Args> {
    std::println(stderr, "usage: cachyos-pi-helper install <pkg>... | remove <pkg>... | upgrade\n"
                         "       cachyos-pi-helper pacman --sync|--upgrade|--database [options] -- <target>...");
    return std::nullopt;
}

auto refuse(std::string_view what, std::string_view value) noexcept -> std::optional<Args> {
    std::println(stderr, "cachyos-pi-helper: {} '{}'", what, value);
    return std::nullopt;
}

auto with_package_names(Args args, Arguments names) noexcept -> std::optional<Args> {
    if (names.empty()) {
        return usage();
    }
    args.emplace_back("--");
    for (const auto name : names) {
        if (!is_package_name(name)) {
            return refuse("invalid package name", name);
        }
        args.emplace_back(name);
    }
    return args;
}

auto paru_target(std::string_view operation, std::string_view target) noexcept -> std::optional<std::string> {
    if (operation == "--upgrade"sv) {
        return resolve_package_file(target);
    }
    const bool valid = operation == "--sync"sv ? is_repo_target(target) : is_package_name(target);
    return valid ? std::optional<std::string>{target} : std::nullopt;
}

auto pacman_for_paru(Arguments arguments) noexcept -> std::optional<Args> {
    if (arguments.size() < 3) {
        return usage();
    }
    const auto operation = arguments.front();
    if (!std::ranges::contains(kParuOperations, operation)) {
        return refuse("pacman operation is not allowed:", operation);
    }
    Args args{"pacman", std::string{operation}};

    const auto separator = std::ranges::find(arguments, "--"sv);
    for (const auto option : Arguments{arguments.begin() + 1, separator}) {
        if (option != "--noconfirm"sv && !std::ranges::contains(kParuOptions, std::pair{operation, option})) {
            return refuse("pacman option is not allowed here:", option);
        }
        args.emplace_back(option);
    }
    const Arguments targets{separator == arguments.end() ? separator : separator + 1, arguments.end()};
    if (targets.empty()) {
        return usage();
    }
    args.emplace_back("--");
    for (const auto given : targets) {
        auto target = paru_target(operation, given);
        if (!target) {
            return refuse("invalid target", given);
        }
        args.push_back(std::move(*target));
    }
    return args;
}

auto pacman_command(Arguments arguments) noexcept -> std::optional<Args> {
    if (arguments.empty()) {
        return usage();
    }
    const auto verb = arguments.front();
    const auto rest = arguments.subspan(1);
    if (verb == "install"sv) {
        return with_package_names({"pacman", "-S", "--noconfirm", "--needed"}, rest);
    }
    if (verb == "remove"sv) {
        return with_package_names({"pacman", "-Rs", "--noconfirm"}, rest);
    }
    if (verb == "upgrade"sv && rest.empty()) {
        return Args{"pacman", "-Syu", "--noconfirm"};
    }
    if (verb == "pacman"sv) {
        return pacman_for_paru(rest);
    }
    return usage();
}

// TODO(vnepogodin): also gucc function
auto c_strings(std::span<std::string> strings) noexcept -> std::vector<char*> {
    auto pointers = strings
        | std::ranges::views::transform([](std::string& text) { return text.data(); })
        | std::ranges::to<std::vector<char*>>();
    pointers.push_back(nullptr);
    return pointers;
}

auto run(Args& args) noexcept -> int {
    // TODO(vnepogodin): copy gucc code into here for now
    std::array environment{"PATH=/usr/bin"s, "LC_ALL=C.UTF-8"s};

    // NOTE: yenuenly we don't even need std::string here..
    std::string pacman{kPacman};
#ifdef PI_HELPER_TESTING
    if (const char* fake = std::getenv("CACHYOS_PI_HELPER_PACMAN"); fake != nullptr) {
        pacman = fake;
    }
#endif

    // pacman goes on when whoever reads its output is gone
    std::signal(SIGPIPE, SIG_IGN);
    // out of reach of the caller's terminal and of a signal to its process group
    setsid();

    execve(pacman.c_str(), c_strings(args).data(), c_strings(environment).data());
    std::println(stderr, "cachyos-pi-helper: cannot run pacman: {}", std::generic_category().message(errno));
    return kNotStarted;
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = std::span{argv, static_cast<std::size_t>(argc)}
        | std::ranges::views::drop(1)
        | std::ranges::to<std::vector<std::string_view>>();

    auto command = pacman_command(arguments);
    return command ? run(*command) : kRefused;
}
