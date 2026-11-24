// SPDX-License-Identifier: GPL-3.0-or-later
#include "userthemes.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcThemes, "cachyos.pi.themes")

constexpr auto PREFIX = "user:"_L1;

const QStringList PALETTE_KEYS = {
    u"window"_s, u"sidebar"_s, u"surface"_s, u"surfaceHover"_s, u"surfaceAlt"_s, u"border"_s,
    u"text"_s, u"textMuted"_s, u"accent"_s, u"accentHover"_s, u"accentSoft"_s, u"onAccent"_s,
    u"arch"_s, u"archSoft"_s, u"danger"_s, u"dangerSoft"_s, u"warning"_s, u"warningSoft"_s};
const QStringList HERO_KEYS = {u"text"_s, u"sub"_s, u"button"_s, u"buttonHover"_s, u"ink"_s};

bool isColour(const QJsonValue& value) {
    return value.isString() && QColor::isValidColorName(value.toString());
}

/// The known colours of a palette, or nothing when one of the required ones is missing.
std::optional<QVariantMap> readPalette(const QJsonObject& object) {
    QVariantMap palette;
    for (const auto& key : PALETTE_KEYS) {
        if (isColour(object.value(key))) {
            palette.insert(key, object.value(key).toString());
        }
    }
    if (!palette.contains(u"window"_s) || !palette.contains(u"text"_s) || !palette.contains(u"accent"_s)) {
        return std::nullopt;
    }
    return palette;
}

/// The banner colours, or nothing unless all of them are given.
std::optional<QVariantMap> readHero(const QJsonObject& object) {
    const auto stops = object.value(u"stops"_s).toArray();
    if (stops.size() != 3 || !std::ranges::all_of(stops, isColour)) {
        return std::nullopt;
    }
    QVariantMap hero{{u"stops"_s, stops.toVariantList()}};
    for (const auto& key : HERO_KEYS) {
        if (!isColour(object.value(key))) {
            return std::nullopt;
        }
        hero.insert(key, object.value(key).toString());
    }
    return hero;
}

}  // namespace

UserThemes::UserThemes(QObject* parent)
  : QObject(parent), m_folder(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/CachyOS/cachyos-pi/themes"_s) {
    // The folder must exist to be watched; a tool that writes a theme later finds it there.
    QDir().mkpath(m_folder);

    m_settle.setSingleShot(true);
    m_settle.setInterval(150);
    connect(&m_settle, &QTimer::timeout, this, &UserThemes::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    reload();
}

QString UserThemes::shownFolder() const {
    const auto home = QDir::homePath();
    return m_folder.startsWith(home + u'/') ? u"~"_s + m_folder.sliced(home.size()) : m_folder;
}

std::optional<QVariantMap> UserThemes::parse(const QByteArray& json) {
    const auto object = QJsonDocument::fromJson(json).object();
    const auto name   = object.value(u"name"_s).toString().trimmed();
    if (name.isEmpty()) {
        return std::nullopt;
    }

    std::optional<QVariantMap> dark;
    std::optional<QVariantMap> light;
    if (object.contains(u"colors"_s)) {
        dark  = readPalette(object.value(u"colors"_s).toObject());
        light = dark;
    } else {
        dark  = readPalette(object.value(u"dark"_s).toObject());
        light = readPalette(object.value(u"light"_s).toObject());
        // a theme for one kind of desktop only is shown on the other as well
        if (!dark) {
            dark = light;
        }
        if (!light) {
            light = dark;
        }
    }
    if (!dark) {
        return std::nullopt;
    }

    QVariantMap theme{
        {u"title"_s, name},
        {u"hint"_s, object.value(u"description"_s).toString()},
        {u"dark"_s, *dark},
        {u"light"_s, *light},
    };
    if (const auto hero = readHero(object.value(u"hero"_s).toObject())) {
        theme.insert(u"hero"_s, *hero);
    }
    return theme;
}

void UserThemes::reload() {
    // A file that is replaced (written to a new file and renamed) leaves the watch list.
    if (!m_watcher.files().isEmpty()) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (!m_watcher.directories().contains(m_folder)) {
        m_watcher.addPath(m_folder);
    }

    QVariantMap themes;
    const auto entries = QDir(m_folder).entryInfoList({u"*.json"_s}, QDir::Files | QDir::Readable, QDir::Name);
    for (const auto& entry : entries) {
        m_watcher.addPath(entry.filePath());
        QFile file(entry.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (auto theme = parse(file.readAll())) {
            themes.insert(PREFIX + entry.completeBaseName(), *theme);
        } else {
            qCWarning(lcThemes) << "not a colour theme:" << entry.filePath();
        }
    }

    if (themes != m_themes) {
        m_themes = std::move(themes);
        emit themesChanged();
    }
}
