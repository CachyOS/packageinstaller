// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pi/file_utils.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <optional>

/// The file at @p path, when it cannot be read or holds more than @p limit bytes.
inline auto readCapped(const QString& path, qint64 limit, pi::file_utils::Links links) -> std::optional<QByteArray> {
    const auto bytes = pi::file_utils::read_whole_file(QFile::encodeName(path), static_cast<std::size_t>(limit), links);
    return bytes ? std::optional{QByteArray::fromStdString(*bytes)} : std::nullopt;
}
