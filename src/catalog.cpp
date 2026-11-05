// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.hpp"

#include <AppStreamQt/component.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/screenshot.h>

#include <QFileInfo>

namespace {

struct AppInfo {
    QString title;
    QString summary;
    QString description;
    QString developer;
    QString homepage;
    QStringList categories;
    QStringList screenshots;
    IconRef icon;
    bool desktopApp = false;
};

IconRef pickIcon(const AppStream::Component& component) {
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

QStringList pickScreenshots(const AppStream::Component& component) {
    QStringList urls;
    for (const auto& shot : component.screenshotsAll()) {
        if (shot.mediaKind() != AppStream::Screenshot::MediaKindImage) {
            continue;
        }
        QUrl best;
        uint bestWidth = 0;
        for (const auto& image : shot.images()) {
            // the largest thumbnail up to ~1250px is plenty; fall back to the source image
            const bool better = best.isEmpty() || (image.width() > bestWidth && image.width() <= 1300);
            if (better) {
                best      = image.url();
                bestWidth = image.width();
            }
        }
        if (best.isValid() && (best.scheme() == QLatin1String("https") || best.scheme() == QLatin1String("http"))) {
            urls << best.toString();
        }
        if (urls.size() >= 8) {
            break;
        }
    }
    return urls;
}

QHash<QString, AppInfo> loadAppStream() {
    QHash<QString, AppInfo> apps;

    AppStream::Pool pool;
    pool.setFlags(AppStream::Pool::FlagLoadOsCatalog);
    if (!pool.load()) {
        qWarning("AppStream: %s", qPrintable(pool.lastError()));
        return apps;
    }

    for (const auto& component : pool.components()) {
        const auto kind = component.kind();
        if (kind != AppStream::Component::KindDesktopApp && kind != AppStream::Component::KindConsoleApp
            && kind != AppStream::Component::KindWebApp) {
            continue;
        }
        AppInfo info;
        info.desktopApp  = kind != AppStream::Component::KindConsoleApp;
        info.title       = component.name();
        info.summary     = component.summary();
        info.description = component.description();
        info.developer   = component.developer().name();
        info.homepage    = component.url(AppStream::Component::UrlKindHomepage).toString();
        info.categories  = component.categories();
        info.screenshots = pickScreenshots(component);
        info.icon        = pickIcon(component);

        for (const auto& pkgname : component.packageNames()) {
            // A package can ship several components (e.g. a suite); keep the desktop app with an icon.
            const auto existing = apps.constFind(pkgname);
            if (existing != apps.cend() && (!existing->icon.path.isEmpty() || info.icon.path.isEmpty())) {
                continue;
            }
            apps.insert(pkgname, info);
        }
    }
    return apps;
}

}  // namespace

namespace catalog {

LoadResult load() {
    LoadResult result;
    result.packages = alpmdb::load(&result.dbPath, &result.error);

    const auto apps = loadAppStream();
    for (auto it = apps.cbegin(); it != apps.cend(); ++it) {
        if (!it->icon.path.isEmpty() || !it->icon.stock.isEmpty()) {
            result.icons.insert(it.key(), it->icon);
        }
    }

    for (auto& pkg : result.packages) {
        const auto found = apps.constFind(pkg.name);
        if (found == apps.cend()) {
            continue;
        }
        pkg.isApp = found->desktopApp;
        if (!found->title.isEmpty()) {
            pkg.title = found->title;
        }
        if (!found->summary.isEmpty()) {
            pkg.summary = found->summary;
        }
        if (pkg.url.isEmpty()) {
            pkg.url = found->homepage;
        }
        pkg.longDescription = found->description;
        pkg.developer       = found->developer;
        pkg.categories      = found->categories;
        pkg.screenshots     = found->screenshots;
    }
    return result;
}

}  // namespace catalog
