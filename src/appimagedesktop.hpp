// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

/// Text handling for AppImage integration: no file system access apart from isAppImage().
namespace appimage {

/// Key naming the managed AppImage file in a desktop entry written by integrate().
inline constexpr auto PATH_KEY = "X-CachyOS-PI-AppImage";
/// No desktop entry is read or written beyond this size.
inline constexpr qint64 MAX_ENTRY_BYTES = 256 * 1024;

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
/// mark this app recognises its own entries by.
///
/// The original is not trusted, so nothing is copied through as it is: only keys that are
/// known to be harmless are taken over, each once, and only the actions the main group
/// lists. Returns an empty string when the original is not a usable application entry or
/// the result would be larger than MAX_ENTRY_BYTES.
QString integrate(const QString& desktopFile, const QString& appImagePath, const QString& iconPath);

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
