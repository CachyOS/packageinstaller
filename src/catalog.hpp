// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "alpmdb.hpp"

#include <QHash>
#include <QtQml/qqmlregistration.h>

#include <expected>

class QIODevice;

/// Where to find an icon for a package name.
struct IconRef {
    QString path;   ///< cached AppStream icon on disk
    QString stock;  ///< icon theme name
};
using IconIndex = QHash<QString, IconRef>;

/// A shelf of the curated list on the home page.
struct CuratedSection {
    Q_GADGET
    QML_VALUE_TYPE(curatedSection)
    Q_PROPERTY(QString title MEMBER title FINAL)
    Q_PROPERTY(QStringList names MEMBER names FINAL)

 public:
    QString title;
    QStringList names;

    bool operator==(const CuratedSection&) const = default;
};

namespace AppStream {
class Component;
}

namespace catalog {

struct LoadResult {
    QList<Package> packages;
    QString dbPath;
    QStringList repos;
    IconIndex icons;
};

/// pkglist.yaml: the curated sections, and the companions installed together with an
/// entry (codecs, plugins), keyed by the entry's package name.
struct CuratedList {
    QList<CuratedSection> sections;
    QHash<QString, QStringList> companions;
};

/// The best icon AppStream has for a component.
IconRef iconOf(const AppStream::Component& component);
/// Up to eight screenshot addresses of a component.
QStringList screenshotUrls(const AppStream::Component& component);

/// Loads the pacman databases and enriches them with the system AppStream catalog
/// (archlinux-appstream-data and any other catalog below /usr/share/swcatalog).
std::expected<LoadResult, QString> load();

/// Parses pkglist.yaml: a list of `name:` + `packages:` sections, each entry "package [companion...]".
CuratedList loadCurated(QIODevice& device);

}  // namespace catalog
