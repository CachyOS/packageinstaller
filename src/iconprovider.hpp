// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QCache>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <memory>

struct IconRequest;

/// `image://pkgicon/<pkgname>`: an icon for every package.
///
/// `image://pkgicon/flatpak/<app id>` is the same for Flatpak apps.
///
/// Lookup order: the Kernel Manager logo for the CachyOS kernels, AppStream cached icon, AppStream stock icon / icon theme,
/// the same again for shortened names ("foo-bar-bin" -> "foo-bar" -> "foo", which
/// covers AUR variants and split packages), and finally a generated tile so
/// nothing is ever blank.
///
/// A list asks for dozens of icons at once, and neither source is quick: the AppStream icons
/// are JPEG XL, milliseconds each to decode, and a name the icon theme does not have takes
/// about as long to rule out. So files are decoded on worker threads and kept in memory, and
/// the lookups, which need the GUI thread for the icon theme, run there a few at a time
/// between other events, remembering what the theme lacks.
class IconProvider : public QQuickAsyncImageProvider {
 public:
    IconProvider();
    ~IconProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// Icons of the packages, or of Flatpak apps, which are asked for as `flatpak/<app id>`.
    void setIndex(IconIndex index, bool flatpak = false);

 private:
    using Request = std::shared_ptr<IconRequest>;
    using Key     = std::pair<QString, int>;  ///< file, extent

    /// Works on the queue for a few milliseconds, then lets other events through.
    void drain();
    void resolve(const Request& request);
    /// Answers from the icon file, or returns false when there is none.
    bool fromFile(const Request& request, const QString& path, const QString& fallback);
    void decoded(const Key& key, const QImage& image);
    QImage themed(const IconRef& ref, const QString& name, int extent);
    static QImage generated(const QString& name, int extent);

    IconIndex m_index;
    IconIndex m_flatpak;
    QList<Request> m_queue;
    bool m_draining = false;
    QSet<QString> m_notInTheme;
    QCache<Key, QImage> m_decoded;
    QHash<Key, QList<Request>> m_waiting;  ///< requests waiting for the same file
    QThreadPool m_pool;
};
