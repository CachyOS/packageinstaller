// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimageimport.hpp"
#include "appimagedesktop.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr int GITHUB = 2;  ///< place of "github" among Shelly's update types

bool fromGitHub(const QJsonValue& updateType) {
    return updateType.isString() ? updateType.toString() == "github"_L1 : updateType.toInt() == GITHUB;
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
            .release        = record.value("release_tag"_L1).toString()};
        // a file that was added by hand has no release; Shelly then goes by the version
        if (const auto version = record.value("version"_L1).toString(); app.release.isEmpty() && version != "Unknown"_L1) {
            app.release = version;
        }
        if (app.title.isEmpty()) {
            app.title = app.name;
        }
        if (app.path.isEmpty() && !installDirectory.isEmpty()) {
            app.path = installDirectory + u'/' + app.name + u".AppImage"_s;
        }
        if (app.name.isEmpty() || !app.path.startsWith(u'/')) {
            continue;
        }
        if (fromGitHub(record.value("update_type"_L1))) {
            const Source source{.owner = record.value("repo_owner"_L1).toString(), .repo = record.value("repo_name"_L1).toString(), .file = {}};
            app.source = sourceFromValue(sourceValue(source));
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

}  // namespace appimage
