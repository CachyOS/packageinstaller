// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace paletteprovider {

/// A rendered palette is a few kilobytes; a larger answer is ignored rather than cut short.
constexpr qint64 MAX_FILE = 1024 * 1024;

/// Reads the palette Event Horizon renders from the wallpaper: a JSON object with
/// "dark", "light" and "hero" members, each an object of colour names. Nothing for
/// any other answer.
std::optional<QVariantMap> parse(const QByteArray& answer);

}  // namespace paletteprovider

/// The wallpaper palette Event Horizon derives for this app.
///
/// The shell's colour engine writes `CachyOS/cachyos-pi-colors.json` next to the
/// settings file. It is kept apart from that file so a render never rewrites the
/// theme and mode chosen here. The palette is read again when the file changes;
/// while there is none, the "wallpaper" theme is not offered.
class PaletteProvider : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(WallpaperPalette)
    QML_SINGLETON
    /// "dark", "light" and "hero", empty while no palette was written.
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY changed FINAL)
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)

 public:
    explicit PaletteProvider(QObject* parent = nullptr);

    QVariantMap palette() const { return m_palette; }
    bool available() const { return !m_palette.isEmpty(); }

 signals:
    void changed();

 private:
    void reload();
    void scheduleReload();
    void watch();

    QString m_path;
    QVariantMap m_palette;
    QFileSystemWatcher m_watcher;
    /// The shell truncates and rewrites the file rather than replacing it, so the
    /// first change notice can see a half-written answer. Read once the writes settle.
    QTimer m_reload_timer;
};
