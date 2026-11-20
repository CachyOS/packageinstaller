// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimageimport.hpp"
#include "appimagedesktop.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int GITHUB = 2;  ///< place of "github" among Shelly's update types

bool fromGitHub(const QJsonValue& updateType) {
    return updateType.isString() ? updateType.toString() == "github"_L1 : updateType.toInt() == GITHUB;
}

/// A name that stays in the folder it is put into: whatever the other program called the
/// app, as long as it is no way out of the folder.
bool plainFileName(QStringView name) {
    return !name.isEmpty() && !name.contains(u'/') && !name.contains(QChar::Null) && name != "."_L1 && name != ".."_L1;
}

/// The file name pattern of the update information an AppImage carries,
/// "gh-releases-zsync|<owner>|<repo>|<tag>|<pattern>", when it is for `source`.
QString releasePattern(const QString& updateInfo, const appimage::Source& source) {
    const auto fields = updateInfo.trimmed().split(u'|');
    if (fields.size() != 5 || fields.at(0).compare("gh-releases-zsync"_L1, Qt::CaseInsensitive) != 0
        || fields.at(1).compare(source.owner, Qt::CaseInsensitive) != 0 || fields.at(2).compare(source.repo, Qt::CaseInsensitive) != 0) {
        return {};
    }
    return fields.at(4);
}

/// A boolean as Python's configparser reads one.
bool configBool(const QString& value) {
    static const QStringList yes{u"1"_s, u"yes"_s, u"true"_s, u"on"_s};
    return yes.contains(value.toLower());
}

}  // namespace

namespace appimage {

QString shellyInstallDirectory(const QByteArray& config) {
    return QJsonDocument::fromJson(config).object().value("AppImageInstallPath"_L1).toString();
}

QList<ShellyApp> parseShellyDatabase(const QByteArray& database, const QString& installDirectory) {
    QList<ShellyApp> apps;
    const auto records = QJsonDocument::fromJson(database).array();
    for (const auto& value : records) {
        const auto record = value.toObject();
        ShellyApp app{.name = record.value("name"_L1).toString(),
            .title          = record.value("desktop_name"_L1).toString(),
            .path           = record.value("path"_L1).toString(),
            .icon           = record.value("icon_name"_L1).toString(),
            .source         = {},
            .release        = record.value("release_tag"_L1).toString(),
            .variant        = {}};
        // a file that was added by hand has no release; Shelly then goes by the version
        if (const auto version = record.value("version"_L1).toString(); app.release.isEmpty() && version != "Unknown"_L1) {
            app.release = version;
        }
        if (app.title.isEmpty()) {
            app.title = app.name;
        }
        if (app.path.isEmpty() && !installDirectory.isEmpty() && plainFileName(app.name)) {
            app.path = installDirectory + u'/' + app.name + u".AppImage"_s;
        }
        if (app.name.isEmpty() || !app.path.startsWith(u'/')) {
            continue;
        }
        if (fromGitHub(record.value("update_type"_L1))) {
            const Source source{.owner = record.value("repo_owner"_L1).toString(), .repo = record.value("repo_name"_L1).toString(), .file = {}};
            app.source = sourceFromValue(sourceValue(source));
            // Shelly keeps the name of a file it was given, but names one it downloaded after the
            // app: the pattern the AppImage carries says better which file of a release it is
            if (app.source) {
                app.variant = patternVariant(releasePattern(record.value("raw_update_info"_L1).toString(), *app.source));
                if (app.variant.isEmpty()) {
                    app.variant = variant(app.path.sliced(app.path.lastIndexOf(u'/') + 1));
                }
            }
        }
        apps << app;
    }
    return apps;
}

QStringList shellyIconFiles(const ShellyApp& app) {
    if (app.icon.isEmpty() || app.icon != safeName(app.icon)) {
        return {};
    }
    return {u"icons/hicolor/scalable/apps/"_s + app.icon + u".svg"_s, u"icons/hicolor/256x256/apps/"_s + app.icon + u".png"_s};
}

QByteArray withoutShellyApps(const QByteArray& database, const QStringList& names) {
    const auto document = QJsonDocument::fromJson(database);
    if (!document.isArray()) {
        return {};
    }
    QJsonArray kept;
    const auto records = document.array();
    for (const auto& record : records) {
        if (!names.contains(record.toObject().value("name"_L1).toString())) {
            kept << record;
        }
    }
    return QJsonDocument(kept).toJson();
}

QList<GearLeverApp> parseGearLeverConfig(const QByteArray& config) {
    // As Python's configparser writes it: "[section]", then "key = value" with the key in
    // lower case. Comments start with "#" or ";", an indented line continues a value.
    QHash<QString, QHash<QString, QString>> sections;
    QStringList order;
    QString section;
    for (const auto line : QString::fromUtf8(config).tokenize(u'\n')) {
        const auto text = line.trimmed();
        if (text.isEmpty() || line.front().isSpace() || text.startsWith(u'#') || text.startsWith(u';')) {
            continue;
        }
        if (text.startsWith(u'[') && text.endsWith(u']')) {
            section = text.sliced(1, text.size() - 2).toString();
            if (!sections.contains(section)) {
                order << section;
                sections.insert(section, {});
            }
            continue;
        }
        // the first "=" or ":" ends the key
        const auto equals    = text.indexOf(u'=');
        const auto colon     = text.indexOf(u':');
        const auto separator = equals < 0 ? colon : colon < 0 ? equals : std::min(equals, colon);
        if (separator > 0 && !section.isEmpty()) {
            sections[section].insert(text.first(separator).trimmed().toString().toLower(), text.sliced(separator + 1).trimmed().toString());
        }
    }

    static const QRegularExpression appSection(u"^app\\.[0-9a-f]{32}$"_s);
    QList<GearLeverApp> apps;
    for (const auto& name : std::as_const(order)) {
        if (!appSection.match(name).hasMatch()) {
            continue;
        }
        const auto& keys = sections[name];
        GearLeverApp app{.path = keys.value(u"file_path"_s), .source = {}, .variant = {}};
        if (!app.path.startsWith(u'/')) {
            continue;
        }
        // The newest release is all that is looked at here. An app that follows pre-releases
        // could be on one that cannot be put in order with it, and be offered a downgrade.
        const auto update = sections.value(name + u".update_manager"_s);
        if (update.value(u"manager"_s) == "GithubUpdater"_L1 && !configBool(update.value(u"allow_prereleases"_s))) {
            app.source = parseSource(update.value(u"repo"_s));
            if (app.source) {
                app.source->file.clear();
                app.variant = patternVariant(update.value(u"repo_filename"_s));
            }
        }
        apps << app;
    }
    return apps;
}

QString gearLeverIcon(const QHash<QString, QString>& entry, const QString& path) {
    if (entry.contains(PATH_KEY) || !entry.contains(GEARLEVER_NAME_KEY) || entry.value(u"TryExec"_s) != path || !path.startsWith(u'/')) {
        return {};
    }
    // Gear Lever puts the icon into ".icons" in the folder of the file, named after the app
    // in any letters: only its place is checked
    const auto icon   = entry.value(u"Icon"_s);
    const auto folder = path.first(path.lastIndexOf(u'/')) + u"/.icons/"_s;
    return icon.startsWith(folder) && plainFileName(QStringView(icon).sliced(folder.size())) ? icon : QString();
}

QString patternVariant(const QString& given) {
    // Gear Lever may name the .zsync file next to the AppImage; it downloads the AppImage
    auto pattern = given;
    if (pattern.endsWith(".zsync"_L1, Qt::CaseInsensitive)) {
        pattern.chop(6);
    }
    // "?" and "[...]" are rare in these patterns and not worth guessing at, and a "*" at the
    // start stands for the name of the app rather than for a version
    if (pattern.contains(u'?') || pattern.contains(u'[') || pattern.startsWith(u'*')) {
        return {};
    }
    // a "*" that stands alone, or after a "v", is where the version goes; inside a word it could be anything
    const auto word = [&pattern](qsizetype at) { return at >= 0 && at < pattern.size() && pattern[at].isLetterOrNumber(); };
    QString name;
    for (qsizetype at = 0; at < pattern.size(); ++at) {
        if (pattern[at] != u'*') {
            name += pattern[at];
            continue;
        }
        const bool afterV = at > 0 && pattern[at - 1].toLower() == u'v' && !word(at - 2);
        if ((word(at - 1) && !afterV) || word(at + 1)) {
            return {};
        }
        name += u'0';
    }
    // "*.AppImage" names no file in particular
    if (const auto rest = QString(pattern).remove(u'*'); rest.isEmpty() || rest.compare(".appimage"_L1, Qt::CaseInsensitive) == 0) {
        return {};
    }
    return variant(name);
}

}  // namespace appimage
