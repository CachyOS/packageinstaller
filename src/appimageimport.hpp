// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "appimagesource.hpp"

/// The AppImages Shelly 3 keeps, so that they can be moved over to this app. Shelly
/// records them as a JSON list in ~/.config/shelly/appimage-metadata-v2.db and puts the
/// files into ~/.local/bin unless its configuration names another folder.
/// Text handling only, no file system access.
namespace appimage {

/// Shelly's list and its configuration, below the user's configuration folder.
inline constexpr QLatin1StringView SHELLY_DATABASE{"shelly/appimage-metadata-v2.db"};
inline constexpr QLatin1StringView SHELLY_CONFIG{"shelly/config.json"};

/// One record of Shelly's list.
struct ShellyApp {
    QString name;     ///< what Shelly calls it; the key of the record
    QString title;    ///< the name the app shows, for display
    QString path;     ///< the AppImage file
    QString icon;     ///< name of the icon Shelly put into the user's icon theme
    std::optional<Source> source;  ///< the repository when Shelly updates it from GitHub
    QString release;  ///< the release Shelly installed, else the version the file gives; may lack the "v" of a tag
};

/// The folder Shelly's configuration (config.json) names for AppImages; empty when it
/// names none.
QString shellyInstallDirectory(const QByteArray& config);

/// The records of Shelly's list. A record without a path is taken to be
/// "<name>.AppImage" in `installDirectory`, as Shelly does. Records without a name or
/// with a path that is not absolute are left out.
QList<ShellyApp> parseShellyDatabase(const QByteArray& database, const QString& installDirectory);

/// The icon files Shelly made for `app`, below the user's data folder. None for a name
/// that is not a plain file name.
QStringList shellyIconFiles(const ShellyApp& app);

/// Shelly's list without the records called `names`; everything else stays as it is.
/// Nothing when the list cannot be read.
QByteArray withoutShellyApps(const QByteArray& database, const QStringList& names);

}  // namespace appimage
