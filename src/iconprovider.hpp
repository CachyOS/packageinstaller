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
/// `image://pkgicon/flatpak/<app id>`, `appimage/<id>` and `cpak/<origin>` are the same for Flatpak apps, AppImages and cpak apps.
///
/// Lookup order: the Kernel Manager logo for the CachyOS kernels, AppStream cached icon, AppStream stock icon / icon theme,
/// the same again for shortened names ("foo-bar-bin" -> "foo-bar" -> "foo", which
/// covers AUR variants and split packages), and finally a generated tile so
/// nothing is ever blank.
///
/// A list asks for dozens of icons at once. Asking QIcon would search the
/// theme directories and render SVGs on the GUI thread, a few milliseconds per name.
class IconProvider : public QQuickAsyncImageProvider {
 public:
    IconProvider();
    ~IconProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// Icons of the packages, or of Flatpak apps, which are asked for as `flatpak/<app id>`.
    void setIndex(IconIndex index, bool flatpak = false);
    /// Icons of the AppImages the user added, asked for as `appimage/<id>`.
    void setAppImageIndex(IconIndex index);
    /// Icons of the cpak apps, asked for as `cpak/<origin>`.
    void setCpakIndex(IconIndex index);
    /// The icon theme's application icons by name, from icontheme::appIcons().
    void setThemeIndex(QHash<QString, QString> index);

 private:
    using Request = std::shared_ptr<IconRequest>;
    using Key     = std::pair<QString, int>;  ///< file, extent

    /// Works on the queue for a few milliseconds, then lets other events through.
    void drain();
    void resolve(const Request& request);
    /// Answers from the icon file, or returns false when there is none.
    bool fromFile(const Request& request, const QString& path, const QString& fallback);
    void decoded(const Key& key, const QImage& image);
    /// The theme's file for the stock icon of `ref`, or for `name`.
    [[nodiscard]] QString themed(const IconRef& ref, const QString& name) const;
    /// Answers with a letter tile, painted on a worker thread.
    void generate(const Request& request, const QString& name);
    static QImage generated(const QString& name, int extent);

    IconIndex m_index;
    IconIndex m_flatpak;
    IconIndex m_appImages;
    IconIndex m_cpak;
    QHash<QString, QString> m_theme;
    QList<Request> m_queue;
    bool m_draining = false;
    QCache<Key, QImage> m_decoded;
    QHash<Key, QList<Request>> m_waiting;  ///< requests waiting for the same file
    QThreadPool m_pool;
};
