// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

/// Colour themes that the user, or a tool that generates them (for example from the
/// wallpaper), keeps as JSON files in ~/.config/CachyOS/cachyos-pi/themes. The folder is
/// watched, so a theme that is rewritten takes effect at once.
///
/// A file holds a "name", an optional "description", and the colours in "dark" and
/// "light", or in "colors" for both. Colours have the names Look.qml uses; "window",
/// "text" and "accent" are required and the others are blended from them when left out.
/// An optional "hero" sets the banner on the start page.
class UserThemes : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// by theme id ("user:" and the file name without .json), each in the shape of Look.themes
    Q_PROPERTY(QVariantMap themes READ themes NOTIFY themesChanged FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)
    /// the folder as it is shown, with "~" for the home folder
    Q_PROPERTY(QString shownFolder READ shownFolder CONSTANT FINAL)

 public:
    explicit UserThemes(QObject* parent = nullptr);

    [[nodiscard]] QVariantMap themes() const { return m_themes; }
    [[nodiscard]] QString folder() const { return m_folder; }
    [[nodiscard]] QString shownFolder() const;

    /// One theme file, or nothing when it is not a theme.
    static std::optional<QVariantMap> parse(const QByteArray& json);

 signals:
    void themesChanged();

 private:
    void reload();

    QString m_folder;
    QVariantMap m_themes;
    QFileSystemWatcher m_watcher;
    /// collects the several change signals a single save causes
    QTimer m_settle;
};
