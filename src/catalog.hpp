// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "alpmdb.hpp"

#include <QHash>

/// Where to find an icon for a package name.
struct IconRef {
    QString path;   ///< cached AppStream icon on disk
    QString stock;  ///< icon theme name
};
using IconIndex = QHash<QString, IconRef>;

struct LoadResult {
    QList<Package> packages;
    IconIndex icons;
    QString dbPath;
    QString error;
};

namespace AppStream {
class Component;
}

namespace catalog {

/// The best icon AppStream has for a component.
IconRef iconOf(const AppStream::Component& component);
/// Up to eight screenshot addresses of a component.
QStringList screenshotUrls(const AppStream::Component& component);

/// Loads the pacman databases and enriches them with the system AppStream catalog
/// (archlinux-appstream-data and any other catalog below /usr/share/swcatalog).
LoadResult load();

}  // namespace catalog
