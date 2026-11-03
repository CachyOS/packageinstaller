// SPDX-License-Identifier: GPL-3.0-or-later
//
// cachyos-pi-helper: the only part of the package installer that runs as root.
// It is started through pkexec and accepts a fixed set of verbs, which it maps
// to pacman invocations. Package names are validated and never reach a shell.

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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

int usage() {
    std::fputs("usage: cachyos-pi-helper install <pkg>... | remove <pkg>... | upgrade\n", stderr);
    return 2;
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
    } else {
        return usage();
    }

    if (verb != "upgrade") {
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
