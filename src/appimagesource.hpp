// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

/// AppImages that come from the releases of a GitHub repository: reading the link the
/// user gave and the answer of the GitHub API. Text handling only, nothing is fetched here.
namespace appimage {

/// No release file larger than this is downloaded.
inline constexpr qint64 MAX_DOWNLOAD_BYTES = 4LL * 1024 * 1024 * 1024;

/// A repository on github.com.
struct Source {
    QString owner;
    QString repo;
    QString file;  ///< the release file the link named, if it named one

    /// "owner/repo"
    QString slug() const { return owner + u'/' + repo; }
};

/// Reads "https://github.com/owner/repo", any page or release file below it, or just
/// "owner/repo". Nothing for anything else.
std::optional<Source> parseSource(const QString& link);

/// What a menu entry stores for `source`, and back. Nothing for a value that is not one.
QString sourceValue(const Source& source);
std::optional<Source> sourceFromValue(const QString& value);

/// Where the API tells about the newest release.
QUrl latestReleaseUrl(const Source& source);

/// One AppImage file of a release.
struct Asset {
    qint64 id = 0;  ///< GitHub's number for this very upload
    QString name;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;  ///< empty when GitHub lists none
};

struct Release {
    QString tag;
    QList<Asset> assets;  ///< the AppImage files for this kind of computer only
};

/// Reads the answer for one release. Files that are no AppImage, are built for another
/// processor or have a name or address that is not usable are left out, so `assets` may
/// be empty. Nothing when the answer does not describe a release.
std::optional<Release> parseRelease(const QByteArray& answer);

/// A file name without its version: "App-1.2.3-x86_64.AppImage" and "App-1.10-x86_64.AppImage"
/// give the same text. Tells the files of one release apart across releases.
QString variant(const QString& fileName);

/// The file of `assets` that continues `variant`: the one of that kind, else the only
/// one there is. -1 when that is not clear.
qsizetype choose(const QList<Asset>& assets, const QString& variant);

}  // namespace appimage
