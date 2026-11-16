// SPDX-License-Identifier: GPL-3.0-or-later
#include "paletteprovider.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <utility>

using namespace Qt::StringLiterals;

namespace paletteprovider {

std::optional<QVariantMap> parse(const QByteArray& answer) {
    const auto document = QJsonDocument::fromJson(answer);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const auto root = document.object();

    QVariantMap palette;
    for (const auto& section : {u"dark"_s, u"light"_s, u"hero"_s}) {
        const auto value = root.value(section);
        if (!value.isObject()) {
            return std::nullopt;
        }
        palette.insert(section, value.toVariant());
    }
    // the banner draws its three-stop gradient by index
    if (root.value("hero"_L1).toObject().value("stops"_L1).toArray().size() < 3) {
        return std::nullopt;
    }
    return palette;
}

}  // namespace paletteprovider

PaletteProvider::PaletteProvider(QObject* parent)
    : QObject(parent),
      m_path(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                 + u"/CachyOS/cachyos-pi-colors.json"_s) {
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &PaletteProvider::scheduleReload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &PaletteProvider::scheduleReload);
    m_reload_timer.setSingleShot(true);
    m_reload_timer.setInterval(120);
    connect(&m_reload_timer, &QTimer::timeout, this, &PaletteProvider::reload);
    reload();
}

void PaletteProvider::scheduleReload() {
    m_reload_timer.start();
}

void PaletteProvider::reload() {
    QVariantMap next;
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= paletteprovider::MAX_FILE) {
        if (const auto parsed = paletteprovider::parse(file.readAll())) {
            next = *parsed;
        }
    }
    // an atomic replace drops the file watch; add back what exists now
    watch();
    if (next == m_palette) {
        return;
    }
    m_palette = std::move(next);
    emit changed();
}

void PaletteProvider::watch() {
    // Watch the parents too: the shell creates the directory together with the file,
    // and a directory watch then hands over to a file watch.
    const auto file = QFileInfo(m_path);
    const auto dir = file.absolutePath();
    const auto parent = QFileInfo(dir).absolutePath();
    for (const auto& candidate : {parent, dir}) {
        if (QFileInfo::exists(candidate) && !m_watcher.directories().contains(candidate)) {
            m_watcher.addPath(candidate);
        }
    }
    if (file.exists() && !m_watcher.files().contains(m_path)) {
        m_watcher.addPath(m_path);
    }
}
