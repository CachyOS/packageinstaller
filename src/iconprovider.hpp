// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QQuickImageProvider>

/// `image://pkgicon/<pkgname>`: an icon for every package.
///
/// `image://pkgicon/flatpak/<app id>` is the same for Flatpak apps.
///
/// Lookup order: the Kernel Manager logo for the CachyOS kernels, AppStream cached icon, AppStream stock icon / icon theme,
/// the same again for shortened names ("foo-bar-bin" -> "foo-bar" -> "foo", which
/// covers AUR variants and split packages), and finally a generated tile so
/// nothing is ever blank.
class IconProvider : public QQuickImageProvider {
 public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) { }

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;

    void setIndex(IconIndex index) { m_index = std::move(index); }
    /// Icons of Flatpak apps, asked for as `flatpak/<app id>`.
    void setFlatpakIndex(IconIndex index) { m_flatpak = std::move(index); }

 private:
    [[nodiscard]] QPixmap lookup(const IconIndex& index, const QString& name, int extent) const;
    [[nodiscard]] static QPixmap generated(const QString& name, int extent);

    IconIndex m_index;
    IconIndex m_flatpak;
};
