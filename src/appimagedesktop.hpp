// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

/// Text handling for AppImage integration: no file system access apart from isAppImage().
namespace appimage {

/// Key naming the managed AppImage file in a desktop entry written by integrate().
inline constexpr QLatin1StringView PATH_KEY{"X-CachyOS-PI-AppImage"};
/// Keys of a file that came from the releases of a repository, see appimagesource.hpp.
inline constexpr QLatin1StringView SOURCE_KEY{"X-CachyOS-PI-Source"};
inline constexpr QLatin1StringView ASSET_KEY{"X-CachyOS-PI-Asset"};
inline constexpr QLatin1StringView VARIANT_KEY{"X-CachyOS-PI-Variant"};
inline constexpr QLatin1StringView RELEASE_KEY{"X-CachyOS-PI-Release"};
/// The project page of the app, as the user entered it.
inline constexpr QLatin1StringView WEBSITE_KEY{"X-CachyOS-PI-Website"};
/// "true" when updates may be pre-releases.
inline constexpr QLatin1StringView PRERELEASES_KEY{"X-CachyOS-PI-Prereleases"};
/// No desktop entry is read or written beyond this size.
inline constexpr qint64 MAX_ENTRY_BYTES = 256 * 1024;

/// Where a managed AppImage was downloaded from, so that a newer release can be found.
struct Tracking {
    QString source;   ///< the repository, as sourceValue() writes it; empty for a file added by hand
    qint64 asset = 0; ///< the upload that is installed; 0 when only `release` is known
    QString variant;  ///< which file of a release it is, see variant()
    QString release;  ///< the release it is from; may lack the "v" of a tag
};

/// What the user can set for one app. The first two end up in its Exec= line, so the menu
/// and "Open" start it the same way.
struct Options {
    QStringList environment;  ///< "NAME=value", in the order they are set
    QString arguments;  ///< as written after the program in Exec=
    QString website;    ///< an http(s) address, or empty
    bool prereleases = false;  ///< newer pre-releases count as updates

    friend bool operator==(const Options&, const Options&) = default;
};

enum class Problem { None, Name, Duplicate, Value, Arguments, Website };

/// What is wrong with `options`, if anything.
Problem problem(const Options& options);

/// The environment and arguments of an Exec= value and the website of a main group.
Options readOptions(const QHash<QString, QString>& mainGroup);

/// The name and value of a "NAME=value" word; nothing for any other word.
std::optional<std::pair<QString, QString>> assignment(const QString& word);

/// `desktopFile` with the environment and arguments of its Exec= line and its website
/// changed to `options`. The program stays and so does every other line. Empty when `options` are not valid or
/// the file has no Exec= line in its main group.
QString withOptions(const QString& desktopFile, const Options& options);

/// The keys of the [Desktop Entry] group of a desktop file; other groups are ignored and
/// the first value of a repeated key counts.
QHash<QString, QString> mainGroup(const QString& desktopFile);

/// The program path as it has to be written into an Exec= line.
QString quoteExec(const QString& path);

/// One Exec= value with its program replaced by `program` (already quoted); the arguments
/// and field codes stay. "env A=1 app %U" loses its assignments along with the program name.
QString replaceProgram(const QString& exec, const QString& program);

/// The words of an Exec= value as a program would receive them: quotes and escapes
/// resolved, field codes such as %U dropped. The program is the first entry.
QStringList commandLine(const QString& exec);

/// A new desktop entry made from the one found inside an AppImage. It starts
/// `appImagePath`, shows `iconPath` (the original icon name when empty) and carries the
/// mark this app recognises its own entries by, along with `tracking` when it has a source.
///
/// The original is not trusted, so nothing is copied through as it is: only keys that are
/// known to be harmless are taken over, each once, and only the actions the main group
/// lists. Returns an empty string when the original is not a usable application entry or
/// the result would be larger than MAX_ENTRY_BYTES.
QString integrate(const QString& desktopFile, const QString& appImagePath, const QString& iconPath, const Tracking& tracking = {});

/// What integrate() stored about the source of a file; without a source when there is none.
Tracking tracking(const QHash<QString, QString>& mainGroup);

/// A file name that is safe to use as it is: letters, digits and ". _ + -" only.
QString safeName(const QString& name);

/// The name for a menu entry made from the name a desktop file has inside an AppImage;
/// always ends in ".desktop".
QString safeDesktopName(const QString& name);

/// True when `path` names a file directly inside `directory` under a name safeName()
/// would produce: the only kind of file this app copies there, deletes or starts.
bool isManagedPath(const QString& path, const QString& directory);

/// True for a type 2 AppImage (an ELF file with the AppImage mark in its header).
bool isAppImage(const QString& path);

}  // namespace appimage
