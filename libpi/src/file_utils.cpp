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

#include "pi/file_utils.hpp"

#include <cerrno>   // for errno
#include <cstdio>   // for fopen, fread, feof, ferror, fclose, fileno
#include <cstring>  // for strerror

#include <fcntl.h>     // for open, O_*
#include <sys/stat.h>  // for fstat
#include <unistd.h>    // for close, read

#include <algorithm>     // for min, max
#include <filesystem>    // for create_directories, copy_file
#include <fstream>       // for ifstream, ofstream
#include <memory>        // for unique_ptr
#include <string>        // for getline
#include <system_error>  // for error_code

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

struct FdCloser {
    int fd;
    ~FdCloser() { ::close(fd); }
};

}  // namespace

namespace pi::file_utils {

auto read_whole_file(std::string_view filepath) noexcept -> std::string {
    const std::string path{filepath};
    const std::unique_ptr<std::FILE, FileCloser> file{std::fopen(path.c_str(), "rb")};
    if (!file) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }

    std::string content{};
    const auto read_at = [&content, stream = file.get()](std::size_t offset, std::size_t count) noexcept {
        std::size_t got{};
        content.resize_and_overwrite(offset + count, [&](char* buf, std::size_t) noexcept {
            got = std::fread(buf + offset, sizeof(char), count, stream);
            return offset + got;
        });
        return got;
    };

    struct stat st{};
    const auto size = ::fstat(::fileno(file.get()), &st) == 0 && st.st_size > 0
        ? static_cast<std::size_t>(st.st_size)
        : 0;

    // fast path
    const auto got = size > 0 ? read_at(0, size) : 0;
    // procfs reports size 0 and sysfs reports a page
    if (size == 0 || (got < size && std::feof(file.get()) == 0 && std::ferror(file.get()) == 0)) {
        constexpr std::size_t kChunk = 4096;
        while (read_at(content.size(), kChunk) > 0) { }
    }
    if (std::ferror(file.get()) != 0) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }
    return content;
}

auto read_first_line(std::string_view filepath) noexcept -> std::string {
    std::string line{};
    if (std::ifstream file{std::string{filepath}}; file) {
        std::getline(file, line);
    }
    return line;
}

auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool {
    std::ofstream file{fs::path{filepath}};
    if (!file.is_open()) {
        spdlog::error("[WRITE_TO_FILE] '{}' open failed: {}", filepath, std::strerror(errno));
        return false;
    }
    file << data;
    return true;
}

auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool {
    std::ofstream file{fs::path{filepath}, std::ios::out | std::ios::trunc};
    if (!file.is_open()) {
        return false;
    }
    file << data;
    return true;
}

auto write_file(std::string_view filepath, std::string_view data) noexcept -> Result<void> {
    if (!create_file_for_overwrite(filepath, data)) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to write {}: {}"), filepath, std::strerror(errno));
    }
    return {};
}

auto create_directories(std::string_view path) noexcept -> Result<void> {
    std::error_code err{};
    fs::create_directories(path, err);
    if (err) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to create {}: {}"), path, err.message());
    }
    return {};
}

auto copy_file(std::string_view from, std::string_view to) noexcept -> Result<void> {
    std::error_code err{};
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, err);
    if (err) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to copy {} to {}: {}"), from, to, err.message());
    }
    return {};
}

auto read_whole_file(std::string_view filepath, std::size_t max_size, Links links) noexcept -> Result<std::string> {
    // TODO(vnepogodin): that should be also part of gucc and shared system lib

    const std::string path{filepath};
    // O_NONBLOCK keeps a FIFO from hanging the open, regular files ignore it
    const auto flags = O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK | (links == Links::Refuse ? O_NOFOLLOW : 0);
    const auto fd    = ::open(path.c_str(), flags);
    if (fd < 0) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to open {}: {}"), filepath, std::strerror(errno));
    }
    const FdCloser closer{fd};

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to stat {}: {}"), filepath, std::strerror(errno));
    }
    if (!S_ISREG(st.st_mode)) {
        return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("{} is not a regular file"), filepath);
    }
    const auto too_large = [&] {
        return make_error(ErrorCode::InvalidArgument, FMT_COMPILE("{} is larger than {} bytes"), filepath, max_size);
    };
    if (static_cast<std::size_t>(st.st_size) > max_size) {
        return too_large();
    }

    // the file can still grow after fstat
    std::string content{};
    auto count = static_cast<std::size_t>(st.st_size) + 1;
    ::ssize_t got{};
    do {
        const auto offset = content.size();
        content.resize_and_overwrite(offset + count, [&](char* buf, std::size_t) noexcept {
            got = ::read(fd, buf + offset, count);
            return offset + static_cast<std::size_t>(std::max<::ssize_t>(got, 0));
        });
        if (content.size() > max_size) {
            return too_large();
        }
        count = std::min<std::size_t>(4096, max_size - content.size()) + 1;
    } while (got > 0);

    if (got < 0) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to read {}: {}"), filepath, std::strerror(errno));
    }
    return content;
}

}  // namespace pi::file_utils
