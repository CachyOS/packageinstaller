// SPDX-License-Identifier: GPL-3.0-or-later
#include "iconprovider.hpp"

#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

QPixmap IconProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) {
    const int extent = requestedSize.width() > 0 ? requestedSize.width() : 64;
    if (size != nullptr) {
        *size = QSize(extent, extent);
    }

    // The CachyOS kernels and their modules carry no icon; give them the Kernel Manager logo
    // instead of a letter tile.
    if (id == QLatin1String("linux-cachyos") || id.startsWith(QLatin1String("linux-cachyos-"))) {
        QImageReader reader(QStringLiteral(":/qt/qml/CachyOS/PackageInstaller/icons/cachyos-kernel.png"));
        reader.setScaledSize(QSize(extent, extent));
        const auto image = reader.read();
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
    }

    // "gimp-help-de" and "firefox-esr-bin" have no icon of their own: walk up the
    // name ("gimp-help", "gimp") so related packages share the application's icon.
    for (QString name = id; !name.isEmpty(); name.truncate(qMax<qsizetype>(name.lastIndexOf(QLatin1Char('-')), 0))) {
        const auto pixmap = lookup(name, extent);
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }
    return generated(id, extent);
}

QPixmap IconProvider::lookup(const QString& name, int extent) const {
    const auto ref = m_index.value(name);
    if (!ref.path.isEmpty()) {
        QImageReader reader(ref.path);
        reader.setScaledSize(QSize(extent, extent));
        const auto image = reader.read();
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
    }
    for (const auto& themeName : {ref.stock, name}) {
        if (!themeName.isEmpty() && QIcon::hasThemeIcon(themeName)) {
            const auto pixmap = QIcon::fromTheme(themeName).pixmap(extent, extent);
            if (!pixmap.isNull()) {
                return pixmap;
            }
        }
    }
    return {};
}

QPixmap IconProvider::generated(const QString& name, int extent) {
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);

    const auto hue = static_cast<int>(qHash(name) % 360);
    QLinearGradient gradient(0, 0, extent, extent);
    gradient.setColorAt(0, QColor::fromHsl(hue, 150, 120));
    gradient.setColorAt(1, QColor::fromHsl((hue + 40) % 360, 160, 85));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, extent, extent), extent * 0.24, extent * 0.24);
    painter.fillPath(path, gradient);

    auto font = painter.font();
    font.setPixelSize(static_cast<int>(extent * 0.5));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1).toUpper());
    return pixmap;
}
