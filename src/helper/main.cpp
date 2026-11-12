// SPDX-License-Identifier: GPL-3.0-or-later
//
// cachyos-pi-helper: the only part of the package installer that runs as root.
// It is started through pkexec and accepts a fixed set of verbs, which it maps
// to pacman invocations. Package names are validated and never reach a shell.
//
// The "pacman" verb exists for paru, which is started with this helper in place of
// sudo. It accepts only the three operations paru uses to install what it built, so
// that one authentication covers a whole build instead of one per step.

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr auto PACMAN = "/usr/bin/pacman";

bool validName(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.front() == '-' || name.front() == '.') {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '.'
            || c == '_' || c == '+' || c == '-';
    });
}

/// "name" or "repo/name", as paru names packages from the repositories.
bool validTarget(std::string_view target) {
    const auto slash = target.find('/');
    if (slash == std::string_view::npos) {
        return validName(target);
    }
    return validName(target.substr(0, slash)) && validName(target.substr(slash + 1));
}

/// An absolute path to a built package, without ".." and pointing at a regular file.
bool validPackageFile(const char* path) {
    const std::string_view text = path;
    if (text.empty() || text.size() > 4096 || text.front() != '/' || text.find("/../") != std::string_view::npos
        || text.find(".pkg.tar") == std::string_view::npos) {
        return false;
    }
    struct stat info {};
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

int usage() {
    std::fputs("usage: cachyos-pi-helper install <pkg>... | remove <pkg>... | upgrade\n"
               "       cachyos-pi-helper pacman --sync|--upgrade|--database [options] -- <target>...\n",
        stderr);
    return 2;
}

/// The invocations paru makes: install dependencies from the repositories, install the
/// packages it built, and set their install reason. Anything else is refused.
int pacmanForParu(int argc, char** argv, std::vector<std::string>& args) {
    if (argc < 5) {
        return usage();
    }
    const std::string_view operation = argv[2];
    if (operation != "--sync" && operation != "--upgrade" && operation != "--database") {
        std::fprintf(stderr, "cachyos-pi-helper: pacman operation '%s' is not allowed\n", argv[2]);
        return 2;
    }
    args.emplace_back(operation);

    int i = 3;
    for (; i < argc && std::string_view(argv[i]) != "--"; ++i) {
        const std::string_view option = argv[i];
        const bool allowed = option == "--noconfirm" || (operation == "--sync" && option == "--needed")
            || (operation == "--database" && (option == "--asdeps" || option == "--asexplicit"));
        if (!allowed) {
            std::fprintf(stderr, "cachyos-pi-helper: pacman option '%s' is not allowed here\n", argv[i]);
            return 2;
        }
        args.emplace_back(option);
    }
    if (i >= argc - 1) {
        return usage();  // no "--", or nothing after it
    }
    args.emplace_back("--");
    for (++i; i < argc; ++i) {
        const bool valid = operation == "--upgrade" ? validPackageFile(argv[i])
            : operation == "--sync" ? validTarget(argv[i]) : validName(argv[i]);
        if (!valid) {
            std::fprintf(stderr, "cachyos-pi-helper: invalid target '%s'\n", argv[i]);
            return 2;
        }
        args.emplace_back(argv[i]);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    const std::string_view verb = argv[1];
    std::vector<std::string> args{"pacman"};

    if (verb == "install") {
        args.insert(args.end(), {"-S", "--noconfirm", "--needed", "--"});
    } else if (verb == "remove") {
        args.insert(args.end(), {"-Rs", "--noconfirm", "--"});
    } else if (verb == "upgrade") {
        if (argc != 2) {
            return usage();
        }
        args.insert(args.end(), {"-Syu", "--noconfirm"});
    } else if (verb == "pacman") {
        if (const int status = pacmanForParu(argc, argv, args); status != 0) {
            return status;
        }
    } else {
        return usage();
    }

    if (verb != "upgrade" && verb != "pacman") {
        if (argc < 3) {
            return usage();
        }
        for (int i = 2; i < argc; ++i) {
            if (!validName(argv[i])) {
                std::fprintf(stderr, "cachyos-pi-helper: invalid package name '%s'\n", argv[i]);
                return 2;
            }
            args.emplace_back(argv[i]);
        }
    }

    std::vector<char*> execArgs;
    execArgs.reserve(args.size() + 1);
    for (auto& arg : args) {
        execArgs.push_back(arg.data());
    }
    execArgs.push_back(nullptr);

    char path[]  = "PATH=/usr/bin";
    char lang[]  = "LC_ALL=C.UTF-8";
    char* envp[] = {path, lang, nullptr};
    execve(PACMAN, execArgs.data(), envp);
    std::perror("cachyos-pi-helper: cannot run pacman");
    return 127;
}
