// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.hpp"

#include <AppStreamQt/component.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/screenshot.h>

#include <QDateTime>
#include <QDirListing>
#include <QFileInfo>
#include <QIODevice>

#include <ryml.hpp>

#include <mutex>
#include <optional>

using namespace Qt::StringLiterals;

namespace catalog {

IconRef iconOf(const AppStream::Component& component) {
    IconRef ref;
    uint best = 0;
    for (const auto& icon : component.icons()) {
        switch (icon.kind()) {
        case AppStream::Icon::KindCached:
        case AppStream::Icon::KindLocal: {
            const auto path = icon.url().toLocalFile();
            // prefer the largest cached icon; cards are drawn at up to 2x of 64px
            if (!path.isEmpty() && icon.width() * icon.scale() >= best && QFileInfo::exists(path)) {
                best     = icon.width() * icon.scale();
                ref.path = path;
            }
            break;
        }
        case AppStream::Icon::KindStock:
            ref.stock = icon.name();
            break;
        default:
            break;
        }
    }
    return ref;
}

QStringList screenshotUrls(const AppStream::Component& component) {
    QStringList urls;
    for (const auto& shot : component.screenshotsAll()) {
        if (shot.mediaKind() != AppStream::Screenshot::MediaKindImage) {
            continue;
        }
        QUrl best;
        uint bestWidth = 0;
        for (const auto& image : shot.images()) {
            // the largest thumbnail up to ~1250px is plenty; fall back to the source image
            if (best.isEmpty() || (image.width() > bestWidth && image.width() <= 1300)) {
                best      = image.url();
                bestWidth = image.width();
            }
        }
        if (best.isValid() && (best.scheme() == "https"_L1 || best.scheme() == "http"_L1)) {
            urls << best.toString();
        }
        if (urls.size() >= 8) {
            break;
        }
    }
    return urls;
}

}  // namespace catalog

namespace {

/// The AppStream fields of each package (in a partially filled Package) and its icon.
struct AppStreamData {
    QHash<QString, Package> apps;
    IconIndex icons;
};

/// Newest modification time of the system catalog directories. A catalog update renames
/// files into them, so this changes whenever the pool would read something new.
qint64 catalogStamp() {
    qint64 newest = 0;
    const auto touch = [&newest](const QFileInfo& info) {
        if (info.exists()) {
            newest = std::max(newest, info.lastModified().toMSecsSinceEpoch());
        }
    };
    for (const auto& root : {u"/usr/share/swcatalog"_s, u"/var/lib/swcatalog"_s, u"/var/cache/swcatalog"_s,
             u"/usr/share/app-info"_s, u"/var/cache/app-info"_s}) {
        touch(QFileInfo(root));
        for (const auto& entry : QDirListing(root, QDirListing::IteratorFlag::DirsOnly)) {
            touch(entry.fileInfo());
        }
    }
    return newest;
}

AppStreamData readAppStream() {
    AppStreamData data;

    AppStream::Pool pool;
    pool.setFlags(AppStream::Pool::FlagLoadOsCatalog);
    if (!pool.load()) {
        qWarning("AppStream: %s", qPrintable(pool.lastError()));
        return data;
    }

    for (const auto& component : pool.components()) {
        const auto kind = component.kind();
        if (kind != AppStream::Component::KindDesktopApp && kind != AppStream::Component::KindConsoleApp
            && kind != AppStream::Component::KindWebApp) {
            continue;
        }
        Package app;
        app.isApp           = kind != AppStream::Component::KindConsoleApp;
        app.title           = component.name();
        app.summary         = component.summary();
        app.longDescription = component.description();
        app.developer       = component.developer().name();
        app.url             = component.url(AppStream::Component::UrlKindHomepage).toString();
        app.categories      = component.categories();
        app.screenshots     = catalog::screenshotUrls(component);
        const auto icon     = catalog::iconOf(component);

        for (const auto& pkgname : component.packageNames()) {
            // A package can ship several components (e.g. a suite); keep the desktop app with an icon.
            if (data.apps.contains(pkgname) && (!data.icons.value(pkgname).path.isEmpty() || icon.path.isEmpty())) {
                continue;
            }
            data.apps.insert(pkgname, app);
            if (icon.path.isEmpty() && icon.stock.isEmpty()) {
                data.icons.remove(pkgname);
            } else {
                data.icons.insert(pkgname, icon);
            }
        }
    }
    return data;
}

}  // namespace

namespace catalog {

std::expected<LoadResult, QString> load() {
    auto databases = alpmdb::load();
    if (!databases) {
        return std::unexpected(databases.error());
    }

    // Reading the pool takes seconds, so it is reused until the catalog on disk changes.
    static std::mutex mutex;
    static std::optional<qint64> stamp;
    static AppStreamData cache;
    const std::scoped_lock lock(mutex);
    if (const auto now = catalogStamp(); stamp != now) {
        cache = readAppStream();
        stamp = now;
    }

    for (auto& pkg : databases->packages) {
        const auto found = cache.apps.constFind(pkg.name);
        if (found == cache.apps.cend()) {
            continue;
        }
        pkg.isApp = found->isApp;
        if (!found->title.isEmpty()) {
            pkg.title = found->title;
        }
        if (!found->summary.isEmpty()) {
            pkg.summary = found->summary;
        }
        if (pkg.url.isEmpty()) {
            pkg.url = found->url;
        }
        pkg.longDescription = found->longDescription;
        pkg.developer       = found->developer;
        pkg.categories      = found->categories;
        pkg.screenshots     = found->screenshots;
    }
    return LoadResult{.packages = std::move(databases->packages), .dbPath = databases->dbPath, .icons = cache.icons};
}

CuratedList loadCurated(QIODevice& device) {
    const auto yaml = device.readAll();
    const auto tree = ryml::parse_in_arena(ryml::csubstr(yaml.constData(), static_cast<size_t>(yaml.size())));
    const auto text = [](ryml::csubstr s) { return QString::fromUtf8(s.str, static_cast<qsizetype>(s.len)); };

    CuratedList list;
    const auto root = tree.crootref();
    if (!root.is_seq()) {
        return list;
    }
    for (const auto section : root.children()) {
        const auto title    = section.is_map() ? section.find_child("name") : ryml::ConstNodeRef{};
        const auto packages = section.is_map() ? section.find_child("packages") : ryml::ConstNodeRef{};
        if (title.invalid() || !title.has_val() || packages.invalid() || !packages.is_seq()) {
            continue;
        }
        CuratedSection out{.title = text(title.val()), .names = {}};
        for (const auto entry : packages.children()) {
            auto names = entry.has_val() ? text(entry.val()).split(u' ', Qt::SkipEmptyParts) : QStringList{};
            if (names.isEmpty()) {
                continue;
            }
            const auto name = names.takeFirst();
            out.names << name;
            if (!names.isEmpty()) {
                list.companions.insert(name, names);
            }
        }
        list.sections << out;
    }
    return list;
}

}  // namespace catalog
