// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace icontheme {

/// where the icon theme lives
struct Theme {
    QString name;
    QStringList searchPaths;
    QStringList fallbackPaths;
};

/// The application icons of @p theme by icon name.
[[nodiscard]] auto appIcons(const Theme& theme) -> QHash<QString, QString>;

}  // namespace icontheme
