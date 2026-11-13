// SPDX-License-Identifier: GPL-3.0-or-later
#include "iconprovider.hpp"

#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

/// One icon request. Lives until QML has taken the image.
class IconResponse : public QQuickImageResponse {
 public:
    explicit IconResponse(QString fallback) : m_fallback(std::move(fallback)) { }

    [[nodiscard]] QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    /// Answers on the thread QML asked from: by then it listens, whichever thread got here first.
    void finish(const QImage& image) {
        QMetaObject::invokeMethod(this, [this, image] {
            m_image = image;
            emit finished();
        }, Qt::QueuedConnection);
    }

    /// The item that asked is gone, as happens to a whole page with every letter typed.
    void cancel() override { m_cancelled.storeRelaxed(1); }
    [[nodiscard]] bool cancelled() const { return m_cancelled.loadRelaxed() != 0; }

    /// The name for the generated tile when the icon file turns out to be unreadable.
    [[nodiscard]] const QString& fallback() const { return m_fallback; }
    void setFallback(const QString& fallback) { m_fallback = fallback; }

 private:
    QImage m_image;
    QString m_fallback;
    QAtomicInt m_cancelled;
};

IconProvider::IconProvider() {
    m_decoded.setMaxCost(64 * 1024 * 1024);
    // Leave cores to the GUI: a page of icons is small work.
    m_pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 4));
}

IconProvider::~IconProvider() {
    m_pool.clear();
    m_pool.waitForDone();
}

void IconProvider::setIndex(IconIndex index) {
    m_index = std::move(index);
    // A reload follows an install or removal, which may have brought icons to the theme.
    m_notInTheme.clear();
    m_decoded.clear();
}

void IconProvider::setFlatpakIndex(IconIndex index) {
    m_flatpak = std::move(index);
    m_notInTheme.clear();
    m_decoded.clear();
}

QQuickImageResponse* IconProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    const int extent = requestedSize.width() > 0 ? requestedSize.width() : 64;
    // Called on QML's image thread; the indexes and the icon theme belong to the GUI thread.
    auto* response = new IconResponse(id);
    QMetaObject::invokeMethod(this, [this, request = Request{response, id, extent}] {
        m_queue << request;
        if (!m_draining) {
            m_draining = true;
            QMetaObject::invokeMethod(this, &IconProvider::drain, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
    return response;
}

void IconProvider::drain() {
    QElapsedTimer spent;
    spent.start();
    while (!m_queue.isEmpty() && spent.elapsed() < 4) {
        const auto request = m_queue.takeFirst();
        if (request.response.isNull()) {
            continue;
        }
        if (request.response->cancelled()) {
            request.response->finish({});
            continue;
        }
        resolve(request.response, request.id, request.extent);
    }
    m_draining = !m_queue.isEmpty();
    if (m_draining) {
        // Posted anew, so input and painting that arrived meanwhile go first.
        QMetaObject::invokeMethod(this, &IconProvider::drain, Qt::QueuedConnection);
    }
}

void IconProvider::resolve(const QPointer<IconResponse>& response, const QString& id, int extent) {
    // The CachyOS kernels and their modules carry no icon; give them the Kernel Manager logo
    // instead of a letter tile.
    if (id == QLatin1String("linux-cachyos") || id.startsWith(QLatin1String("linux-cachyos-"))) {
        if (fromFile(response, QStringLiteral(":/qt/qml/CachyOS/PackageInstaller/icons/cachyos-kernel.png"), id, extent)) {
            return;
        }
    }

    // Flatpak apps have their own catalogue; an installed one also exports its icon to the theme.
    if (id.startsWith(QLatin1String("flatpak/"))) {
        const auto app = id.mid(8);
        const auto ref = m_flatpak.value(app);
        // "org.mozilla.firefox": the last part names the app
        const auto tile = app.section(QLatin1Char('.'), -1);
        if (fromFile(response, ref.path, tile, extent)) {
            return;
        }
        const auto image = themed(ref, app, extent);
        response->finish(image.isNull() ? generated(tile, extent) : image);
        return;
    }

    // "gimp-help-de" and "firefox-esr-bin" have no icon of their own: walk up the
    // name ("gimp-help", "gimp") so related packages share the application's icon.
    for (QString name = id; !name.isEmpty(); name.truncate(qMax<qsizetype>(name.lastIndexOf(QLatin1Char('-')), 0))) {
        const auto ref = m_index.value(name);
        if (fromFile(response, ref.path, id, extent)) {
            return;
        }
        const auto image = themed(ref, name, extent);
        if (!image.isNull()) {
            response->finish(image);
            return;
        }
    }
    response->finish(generated(id, extent));
}

bool IconProvider::fromFile(const QPointer<IconResponse>& response, const QString& path, const QString& fallback, int extent) {
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return false;
    }
    const auto key = QString::number(extent) + QLatin1Char(':') + path;
    if (const auto* cached = m_decoded.object(key)) {
        response->finish(*cached);
        return true;
    }

    response->setFallback(fallback);
    auto& waiting = m_waiting[key];
    waiting << response;
    if (waiting.size() > 1) {
        return true;  // already being decoded
    }
    m_pool.start([this, key, path, extent] {
        QImageReader reader(path);
        reader.setScaledSize(QSize(extent, extent));
        const auto image = reader.read();
        QMetaObject::invokeMethod(this, [this, key, image] { decoded(key, image); }, Qt::QueuedConnection);
    });
    return true;
}

void IconProvider::decoded(const QString& key, const QImage& image) {
    if (!image.isNull()) {
        m_decoded.insert(key, new QImage(image), qMax<qsizetype>(image.sizeInBytes(), 1));
    }
    const int extent = key.section(QLatin1Char(':'), 0, 0).toInt();
    const auto waiting = m_waiting.take(key);
    for (const auto& response : waiting) {
        if (!response.isNull()) {
            response->finish(image.isNull() ? generated(response->fallback(), extent) : image);
        }
    }
}

QImage IconProvider::themed(const IconRef& ref, const QString& name, int extent) {
    for (const auto& themeName : {ref.stock, name}) {
        if (themeName.isEmpty() || m_notInTheme.contains(themeName)) {
            continue;
        }
        if (!QIcon::hasThemeIcon(themeName)) {
            m_notInTheme.insert(themeName);
            continue;
        }
        const auto pixmap = QIcon::fromTheme(themeName).pixmap(extent, extent);
        if (!pixmap.isNull()) {
            return pixmap.toImage();
        }
    }
    return {};
}

QImage IconProvider::generated(const QString& name, int extent) {
    QImage image(extent, extent, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const auto hue = static_cast<int>(qHash(name) % 360);
    QLinearGradient gradient(0, 0, extent, extent);
    gradient.setColorAt(0, QColor::fromHsl(hue, 150, 120));
    gradient.setColorAt(1, QColor::fromHsl((hue + 40) % 360, 160, 85));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, extent, extent), extent * 0.24, extent * 0.24);
    painter.fillPath(path, gradient);

    auto font = painter.font();
    font.setPixelSize(static_cast<int>(extent * 0.5));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(image.rect(), Qt::AlignCenter, name.left(1).toUpper());
    return image;
}
