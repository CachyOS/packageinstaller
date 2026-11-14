// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagesource.hpp"
#include "appimagedesktop.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

constexpr auto PREFIX = "github:"_L1;

bool validOwner(const QString& owner) {
    static const QRegularExpression pattern(u"^[A-Za-z0-9][A-Za-z0-9-]{0,38}$"_s);
    return pattern.match(owner).hasMatch();
}

bool validRepo(const QString& repo) {
    static const QRegularExpression pattern(u"^[A-Za-z0-9._-]{1,100}$"_s);
    return pattern.match(repo).hasMatch() && repo != "."_L1 && repo != ".."_L1;
}

/// Built for a processor this system does not have.
bool foreign(const QString& fileName) {
    static const QRegularExpression other(
        u"aarch64|arm64|armhf|armel|armv[5-8]|i[3-6]86|ia32|riscv|ppc64|s390x|loongarch|mips"
        // "arm" and "x86" as words of their own: not "charm", not "x86_64"
        "|(?<![a-z])arm(?![a-z])|x86(?![_-]?64)"_s,
        QRegularExpression::CaseInsensitiveOption);
    return other.match(fileName).hasMatch();
}

}  // namespace

namespace appimage {

std::optional<Source> parseSource(const QString& link) {
    auto text = link.trimmed();
    static const QRegularExpression bare(u"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$"_s);
    if (bare.match(text).hasMatch()) {
        text.prepend(u"https://github.com/"_s);
    } else if (!text.contains(u"://"_s)) {
        text.prepend(u"https://"_s);
    }
    const QUrl url(text, QUrl::StrictMode);
    const auto host = url.host().toLower();
    if (!url.isValid() || (url.scheme() != "https"_L1 && url.scheme() != "http"_L1)
        || (host != "github.com"_L1 && host != "www.github.com"_L1) || !url.userInfo().isEmpty()) {
        return std::nullopt;
    }
    const auto parts = url.path().split(u'/', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        return std::nullopt;
    }
    Source source{.owner = parts.at(0), .repo = parts.at(1), .file = {}};
    if (source.repo.endsWith(".git"_L1)) {
        source.repo.chop(4);
    }
    if (!validOwner(source.owner) || !validRepo(source.repo)) {
        return std::nullopt;
    }
    // .../releases/download/<tag>/<file>
    if (parts.size() == 6 && parts.at(2) == "releases"_L1 && parts.at(3) == "download"_L1) {
        source.file = parts.at(5);
    }
    return source;
}

QString sourceValue(const Source& source) {
    return PREFIX + source.slug();
}

std::optional<Source> sourceFromValue(const QString& value) {
    const auto parts = value.mid(PREFIX.size()).split(u'/');
    if (!value.startsWith(PREFIX) || parts.size() != 2 || !validOwner(parts.at(0)) || !validRepo(parts.at(1))) {
        return std::nullopt;
    }
    return Source{.owner = parts.at(0), .repo = parts.at(1), .file = {}};
}

QUrl latestReleaseUrl(const Source& source) {
    return QUrl(u"https://api.github.com/repos/%1/%2/releases/latest"_s.arg(source.owner, source.repo));
}

std::optional<Release> parseRelease(const QByteArray& answer) {
    const auto document = QJsonDocument::fromJson(answer);
    const auto object   = document.object();
    Release release{.tag = object.value("tag_name"_L1).toString().simplified().left(64), .assets = {}};
    if (release.tag.isEmpty() || !object.value("assets"_L1).isArray()) {
        return std::nullopt;
    }
    const auto assets = object.value("assets"_L1).toArray();
    for (const auto& value : assets) {
        const auto entry = value.toObject();
        Asset asset{
            .id     = entry.value("id"_L1).toInteger(),
            .name   = entry.value("name"_L1).toString(),
            .url    = QUrl(entry.value("browser_download_url"_L1).toString(), QUrl::StrictMode),
            .size   = entry.value("size"_L1).toInteger(),
            .sha256 = {},
        };
        const bool named = asset.name.endsWith(".appimage"_L1, Qt::CaseInsensitive) && asset.name == safeName(asset.name);
        // only ever a file of a release on github.com itself
        const bool located = asset.url.isValid() && asset.url.scheme() == "https"_L1 && asset.url.host() == "github.com"_L1
            && asset.url.userInfo().isEmpty() && asset.url.path().contains("/releases/download/"_L1);
        if (asset.id <= 0 || !named || !located || asset.size <= 0 || asset.size > MAX_DOWNLOAD_BYTES || foreign(asset.name)) {
            continue;
        }
        // GitHub lists a checksum for newer uploads. One that is there has to be usable:
        // a file is not taken without the check it should have had.
        if (const auto digest = entry.value("digest"_L1).toString(); !digest.isEmpty()) {
            static const QRegularExpression sha256(u"^sha256:([0-9a-fA-F]{64})$"_s);
            const auto match = sha256.match(digest);
            if (!match.hasMatch()) {
                continue;
            }
            asset.sha256 = QByteArray::fromHex(match.captured(1).toLatin1());
        }
        release.assets << asset;
    }
    return release;
}

QString variant(const QString& fileName) {
    auto name = fileName.toLower();
    // The processor is part of the kind, numbers and all: "x86_64" is not "x86_64-v3".
    // Its digits become capital letters, which a lower-cased name has none of.
    static const QRegularExpression processor(u"x86[_-]64(?:[_-]v[2-4])?|amd64|x64"_s);
    for (auto it = processor.globalMatch(name); it.hasNext();) {
        const auto match = it.next();
        for (auto i = match.capturedStart(); i < match.capturedEnd(); ++i) {
            if (name.at(i).isDigit()) {
                name[i] = QChar(u'A' + name.at(i).digitValue());
            }
        }
    }
    // A version is a number that stands alone, "1.2.3" or "v7". Digits inside a word stay:
    // "qt5" and "qt6" are different builds.
    static const QRegularExpression version(u"(?<![a-z0-9A-Z])v?\\d+(?:[._]\\d+)*(?![a-zA-Z0-9])"_s);
    return name.replace(version, u"#"_s);
}

qsizetype choose(const QList<Asset>& assets, const QString& variant) {
    qsizetype found = -1;
    int matches     = 0;
    for (qsizetype i = 0; i < assets.size(); ++i) {
        if (appimage::variant(assets.at(i).name) == variant) {
            found = i;
            ++matches;
        }
    }
    if (matches == 1) {
        return found;
    }
    return assets.size() == 1 ? 0 : -1;
}

}  // namespace appimage
