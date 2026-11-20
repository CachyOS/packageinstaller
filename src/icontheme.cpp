// SPDX-License-Identifier: GPL-3.0-or-later
#include "icontheme.hpp"

#include <cstdint>
#include <limits>

#include <QDirListing>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace {

using Icons = QHash<QString, QString>;

const QStringList kImages{u"*.png"_s, u"*.svg"_s, u"*.svgz"_s};
constexpr auto kListing = QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks;

struct Found {
    std::int32_t rank{-1};
    QString path;
};

auto rank(const QSettings& index, const QString& dir) -> std::int32_t {
    // svg takes prio over regular images, overwise we scale down
    if (index.value(dir + u"/Type"_s).toString() == "Scalable"_L1) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return index.value(dir + u"/Size"_s).toInt() * qMax(1, index.value(dir + u"/Scale"_s, 1).toInt());
}

bool holdsApps(const QSettings& index, const QString& dir) {
    const auto context = index.value(dir + u"/Context"_s).toString();
    return context.isEmpty() ? dir.split(u'/').contains("apps"_L1) : context == "Applications"_L1;
}

void addTheme(const icontheme::Theme& theme, const QString& name, QSet<QString>& seen, Icons& icons) {
    // take own icons first, then the ones it inherits in order
    if (name.isEmpty() || seen.contains(name)) {
        return;
    }
    seen.insert(name);
    QString indexFile;

    // TODO(vnepogodin): refactor that to backend after
    for (const auto& base : theme.searchPaths) {
        if (const auto file = base + u'/' + name + u"/index.theme"_s; QFileInfo::exists(file)) {
            indexFile = file;
            break;
        }
    }
    if (indexFile.isEmpty()) {
        return;
    }

    // index through icon best sizes
    const QSettings index(indexFile, QSettings::IniFormat);

    QHash<QString, Found> found;
    const auto dirs = index.value(u"Icon Theme/Directories"_s).toStringList() + index.value(u"Icon Theme/ScaledDirectories"_s).toStringList();
    for (const auto& dir : dirs) {
        if (!holdsApps(index, dir)) {
            continue;
        }
        const int32_t dirRank = rank(index, dir);

        // TODO(vnepogodin): what's these nested rloops..

        // go over several search paths
        for (const auto& base : theme.searchPaths) {
            for (const auto& entry : QDirListing(base + u'/' + name + u'/' + dir, kImages, kListing)) {
                // "org.kde.kate.svg" is named "org.kde.kate"
                auto& best = found[entry.completeBaseName()];
                if (dirRank > best.rank) {
                    best = {.rank = dirRank, .path = entry.filePath()};
                }
            }
        }
    }

    // get children and parent icons
    for (const auto& [icon, best] : found.asKeyValueRange()) {
        icons.tryEmplace(icon, best.path);
    }
    for (const auto& parent : index.value(u"Icon Theme/Inherits"_s).toStringList()) {
        addTheme(theme, parent.trimmed(), seen, icons);
    }
}

}  // namespace

namespace icontheme {

auto appIcons(const Theme& theme) -> QHash<QString, QString> {
    Icons icons;
    QSet<QString> seen;

    // what else..
    addTheme(theme, theme.name, seen, icons);
    addTheme(theme, u"hicolor"_s, seen, icons);

    for (const auto& dir : theme.fallbackPaths) {
        for (const auto& entry : QDirListing(dir, kImages, kListing)) {
            icons.tryEmplace(entry.completeBaseName(), entry.filePath());
        }
    }
    return icons;
}

}  // namespace icontheme
