// SPDX-License-Identifier: GPL-3.0-or-later
#include "iconprovider.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

#include <atomic>
#include <mutex>

using namespace Qt::StringLiterals;

class IconResponse;

/// One icon request, shared by the provider (GUI thread) and the response QML holds
/// (its image thread), which QML may delete at any time once it has cancelled.
struct IconRequest {
    QString id;
    int extent = 0;
    QString fallback;  ///< the name for the generated tile if the icon file is unreadable, GUI thread only
    std::atomic_bool cancelled = false;

    /// Answers once, on the response's thread. Does nothing if QML already deleted it.
    void finish(const QImage& image);

    std::mutex mutex;
    IconResponse* response = nullptr;  ///< guarded by mutex, cleared when the response goes
};

class IconResponse : public QQuickImageResponse {
 public:
    explicit IconResponse(std::shared_ptr<IconRequest> request) : m_request(std::move(request)) {
        m_request->response = this;
    }
    ~IconResponse() override {
        const std::scoped_lock lock(m_request->mutex);
        m_request->response = nullptr;
    }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }

    /// The item that asked is gone, as happens to a whole page with every letter typed.
    void cancel() override { m_request->cancelled = true; }

    void deliver(const QImage& image) {
        m_image = image;
        emit finished();
    }

 private:
    std::shared_ptr<IconRequest> m_request;
    QImage m_image;
};

void IconRequest::finish(const QImage& image) {
    const std::scoped_lock lock(mutex);
    if (auto* target = std::exchange(response, nullptr)) {
        // posted while the lock keeps it alive, and Qt drops the call if it is deleted before
        QMetaObject::invokeMethod(target, [target, image] { target->deliver(image); }, Qt::QueuedConnection);
    }
}

IconProvider::IconProvider() {
    // QML keeps what is on screen; this only spares decoding again when scrolling back.
    m_decoded.setMaxCost(16 * 1024 * 1024);
    // Leave cores to the GUI: a page of icons is small work.
    m_pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 4));
}

IconProvider::~IconProvider() {
    m_pool.clear();
    m_pool.waitForDone();
}

void IconProvider::setIndex(IconIndex index, bool flatpak) {
    (flatpak ? m_flatpak : m_index) = std::move(index);
    // A reload follows an install or removal, which may have brought icons to the theme.
    m_notInTheme.clear();
    m_decoded.clear();
}

void IconProvider::setAppImageIndex(IconIndex index) {
    m_appImages = std::move(index);
    m_decoded.clear();  // a newer file of the same app replaces its icon in place
}

QQuickImageResponse* IconProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    // Called on QML's image thread; the indexes and the icon theme belong to the GUI thread.
    auto request   = std::make_shared<IconRequest>(id, requestedSize.width() > 0 ? requestedSize.width() : 64, id);
    auto* response = new IconResponse(request);
    QMetaObject::invokeMethod(this, [this, request = std::move(request)] {
        m_queue << request;
        if (!std::exchange(m_draining, true)) {
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
        if (request->cancelled) {
            request->finish({});
        } else {
            resolve(request);
        }
    }
    m_draining = !m_queue.isEmpty();
    if (m_draining) {
        // Posted anew, so input and painting that arrived meanwhile go first.
        QMetaObject::invokeMethod(this, &IconProvider::drain, Qt::QueuedConnection);
    }
}

void IconProvider::resolve(const Request& request) {
    const auto& id   = request->id;
    const int extent = request->extent;
    // The CachyOS kernels and their modules carry no icon; give them the Kernel Manager logo
    // instead of a letter tile.
    if ((id == "linux-cachyos"_L1 || id.startsWith("linux-cachyos-"_L1))
        && fromFile(request, u":/qt/qml/CachyOS/PackageInstaller/icons/cachyos-kernel.png"_s, id)) {
        return;
    }

    // Flatpak apps have their own catalogue; an installed one also exports its icon to the theme.
    if (id.startsWith("flatpak/"_L1)) {
        const auto app = id.mid(8);
        const auto ref = m_flatpak.value(app);
        // "org.mozilla.firefox": the last part names the app
        const auto tile = app.section(u'.', -1);
        if (fromFile(request, ref.path, tile)) {
            return;
        }
        const auto image = themed(ref, app, extent);
        request->finish(image.isNull() ? generated(tile, extent) : image);
        return;
    }

    if (id.startsWith("appimage/"_L1)) {
        const auto app  = id.mid(9);
        const auto tile = app.section(u'.', -1);
        const auto ref  = m_appImages.value(app);
        if (fromFile(request, ref.path, tile)) {
            return;
        }
        const auto image = themed(ref, app, extent);
        request->finish(image.isNull() ? generated(tile, extent) : image);
        return;
    }

    // "gimp-help-de" and "firefox-esr-bin" have no icon of their own: walk up the
    // name ("gimp-help", "gimp") so related packages share the application's icon.
    for (QString name = id; !name.isEmpty(); name.truncate(qMax<qsizetype>(name.lastIndexOf(u'-'), 0))) {
        const auto ref = m_index.value(name);
        if (fromFile(request, ref.path, id)) {
            return;
        }
        if (const auto image = themed(ref, name, extent); !image.isNull()) {
            request->finish(image);
            return;
        }
    }
    request->finish(generated(id, extent));
}

bool IconProvider::fromFile(const Request& request, const QString& path, const QString& fallback) {
    if (path.isEmpty()) {
        return false;
    }
    const Key key{path, request->extent};
    if (const auto* cached = m_decoded.object(key)) {
        request->finish(*cached);
        return true;
    }
    if (!QFileInfo::exists(path)) {
        return false;
    }

    request->fallback = fallback;
    auto& waiting     = m_waiting[key];
    waiting << request;
    if (waiting.size() > 1) {
        return true;  // already being decoded
    }
    m_pool.start([this, key] {
        QImageReader reader(key.first);
        reader.setScaledSize(QSize(key.second, key.second));
        QMetaObject::invokeMethod(this, [this, key, image = reader.read()] { decoded(key, image); }, Qt::QueuedConnection);
    });
    return true;
}

void IconProvider::decoded(const Key& key, const QImage& image) {
    if (!image.isNull()) {
        m_decoded.insert(key, new QImage(image), qMax<qsizetype>(image.sizeInBytes(), 1));
    }
    for (const auto& request : m_waiting.take(key)) {
        request->finish(image.isNull() ? generated(request->fallback, key.second) : image);
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
        const auto pixmap = QIcon::fromTheme(themeName).pixmap(QSize(extent, extent), 1.0);  // extent is in pixels already
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
