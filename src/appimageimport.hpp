// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "appimagesource.hpp"

/// The AppImages other programs keep, so that they can be moved over to this app.
/// Text handling only, no file system access.
///
/// Shelly 3 records them as a JSON list in ~/.config/shelly/appimage-metadata-v2.db and
/// puts the files into ~/.local/bin unless its configuration names another folder.
///
/// Gear Lever copies them into a folder of its own (~/AppImages unless set otherwise),
/// their icons into ".icons" inside it, and writes a menu entry that names the file in
/// TryExec. Its gearlever.conf has a section for each app it copied, named after the
/// MD5 sum of the path, and another one for where the app is updated from.
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
    /// Which file of a release it is, from the file name pattern of the update information the
    /// AppImage carries, else from the name of the file. A guess either way: Shelly names a file
    /// it downloaded after the app, "<name>.AppImage", or "<name>-<version>.AppImage" once it
    /// updated it. Empty when it is not updated from GitHub.
    QString variant;
};

/// The folder Shelly's configuration (config.json) names for AppImages; empty when it
/// names none.
QString shellyInstallDirectory(const QByteArray& config);

/// The records of Shelly's list. A record without a path is taken to be
/// "<name>.AppImage" in `installDirectory`, as Shelly does. Records without a name or
/// with a path that is not absolute are left out, and so are those without a path whose
/// name would lead out of `installDirectory`.
QList<ShellyApp> parseShellyDatabase(const QByteArray& database, const QString& installDirectory);

/// The icon files Shelly made for `app`, below the user's data folder. None for a name
/// that is not a plain file name.
QStringList shellyIconFiles(const ShellyApp& app);

/// Shelly's list without the records called `names`; everything else stays as it is.
/// Nothing when the list cannot be read.
QByteArray withoutShellyApps(const QByteArray& database, const QStringList& names);

/// Gear Lever's configuration: of the Flatpak below the home folder, of an installation
/// from a package below the user's configuration folder.
inline constexpr QLatin1StringView GEARLEVER_FLATPAK_CONFIG{".var/app/it.mijorus.gearlever/config/gearlever.conf"};
inline constexpr QLatin1StringView GEARLEVER_CONFIG{"gearlever.conf"};

/// One app of gearlever.conf.
struct GearLeverApp {
    QString path;  ///< the AppImage file
    std::optional<Source> source;  ///< the repository when Gear Lever updates it from GitHub
    QString variant;  ///< which file of a release it is, from the file name pattern Gear Lever looks for
};

/// The apps of gearlever.conf that name an absolute path. Whether Gear Lever still has
/// them is for their menu entries to tell: the configuration keeps what it once saw.
/// Where an app is updated from is left out when Gear Lever follows its pre-releases.
QList<GearLeverApp> parseGearLeverConfig(const QByteArray& config);

/// The key Gear Lever writes into its menu entries, with the name the file gives itself.
inline constexpr QLatin1StringView GEARLEVER_NAME_KEY{"X-AppImage-Name"};

/// The icon Gear Lever copied for the AppImage `path` when `entry` (the keys of a menu
/// entry's main group) is the one Gear Lever wrote for that file; empty when it is not.
/// Gear Lever lists AppImages it did not copy too, which have no icon of its own.
QString gearLeverIcon(const QHash<QString, QString>& entry, const QString& path);

/// The variant() of the release files a Gear Lever file name pattern such as
/// "App-*-x86_64.AppImage" matches; empty when the pattern does not say which file it is.
QString patternVariant(const QString& pattern);

}  // namespace appimage
