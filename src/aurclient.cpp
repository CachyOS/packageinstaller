// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurclient.hpp"
#include "packagemodel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

namespace {

constexpr auto AUR_HOST = "https://aur.archlinux.org";

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("cachyos-pi/" PI_VERSION));
    req.setTransferTimeout(20000);
    return req;
}

QStringList toStringList(const QJsonValue& value) {
    QStringList out;
    for (const auto& item : value.toArray()) {
        out << item.toString();
    }
    return out;
}

}  // namespace

AurClient::AurClient(const PackageModel* packages, QObject* parent) : QObject(parent), m_packages(packages) { }

void AurClient::setError(const QString& error) {
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

QVariantMap AurClient::toMap(const QJsonObject& object) const {
    const auto name     = object.value(QLatin1String("Name")).toString();
    const auto* local   = m_packages->find(name);
    const auto version  = object.value(QLatin1String("Version")).toString();
    const bool installed = local != nullptr && local->installed();
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("pkgbase"), object.value(QLatin1String("PackageBase")).toString()},
        {QStringLiteral("version"), version},
        {QStringLiteral("description"), object.value(QLatin1String("Description")).toString()},
        {QStringLiteral("url"), object.value(QLatin1String("URL")).toString()},
        {QStringLiteral("maintainer"), object.value(QLatin1String("Maintainer")).toString()},
        {QStringLiteral("votes"), object.value(QLatin1String("NumVotes")).toInt()},
        {QStringLiteral("popularity"), object.value(QLatin1String("Popularity")).toDouble()},
        {QStringLiteral("outOfDate"), !object.value(QLatin1String("OutOfDate")).isNull()},
        {QStringLiteral("lastModified"), object.value(QLatin1String("LastModified")).toInteger()},
        {QStringLiteral("licenses"), toStringList(object.value(QLatin1String("License")))},
        {QStringLiteral("depends"), toStringList(object.value(QLatin1String("Depends")))},
        {QStringLiteral("makeDepends"), toStringList(object.value(QLatin1String("MakeDepends")))},
        {QStringLiteral("installed"), installed},
        {QStringLiteral("installedVersion"), installed ? local->installedVersion : QString()},
        {QStringLiteral("upgradable"), installed && alpmdb::vercmp(local->installedVersion, version) < 0},
        // a package of the same name in the repositories should be installed from there instead
        {QStringLiteral("inRepos"), local != nullptr && !local->foreign()},
    };
}

void AurClient::search(const QString& query) {
    const auto term = query.trimmed();
    const int generation = ++m_generation;
    setError({});
    if (term.size() < 2) {
        m_results.clear();
        emit resultsChanged();
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        return;
    }

    QUrl url(QStringLiteral("%1/rpc/v5/search/%2").arg(QLatin1String(AUR_HOST), QString::fromLatin1(QUrl::toPercentEncoding(term))));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("by"), QStringLiteral("name-desc"));
    url.setQuery(urlQuery);

    m_searching = true;
    emit searchingChanged();

    auto* reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) {
            return;  // superseded by a newer search
        }
        m_searching = false;
        emit searchingChanged();

        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError && json.isEmpty()) {
            setError(tr("Could not reach the AUR: %1").arg(reply->errorString()));
            return;
        }
        if (json.value(QLatin1String("type")).toString() == QLatin1String("error")) {
            setError(json.value(QLatin1String("error")).toString());
            m_results.clear();
            emit resultsChanged();
            return;
        }

        QList<QVariantMap> maps;
        for (const auto& item : json.value(QLatin1String("results")).toArray()) {
            maps << toMap(item.toObject());
        }
        std::ranges::sort(maps, [](const QVariantMap& a, const QVariantMap& b) {
            return a.value(QStringLiteral("popularity")).toDouble() > b.value(QStringLiteral("popularity")).toDouble();
        });
        m_results.clear();
        for (const auto& map : std::as_const(maps)) {
            m_results << map;
        }
        emit resultsChanged();
    });
}

void AurClient::loadDetails(const QString& name, const QString& pkgbase) {
    const int generation = ++m_detailsGeneration;
    m_details.clear();
    m_pkgbuild.clear();
    emit detailsChanged();
    emit pkgbuildChanged();

    const auto finishOne = [this] {
        --m_pending;
        emit loadingDetailsChanged();
    };

    QUrl infoUrl(QStringLiteral("%1/rpc/v5/info").arg(QLatin1String(AUR_HOST)));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("arg[]"), QString::fromLatin1(QUrl::toPercentEncoding(name)));
    infoUrl.setQuery(infoQuery);

    QUrl pkgbuildUrl(QStringLiteral("%1/cgit/aur.git/plain/PKGBUILD").arg(QLatin1String(AUR_HOST)));
    QUrlQuery pkgbuildQuery;
    pkgbuildQuery.addQueryItem(QStringLiteral("h"), QString::fromLatin1(QUrl::toPercentEncoding(pkgbase)));
    pkgbuildUrl.setQuery(pkgbuildQuery);

    m_pending += 2;
    emit loadingDetailsChanged();

    auto* info = m_network.get(request(infoUrl));
    connect(info, &QNetworkReply::finished, this, [this, info, generation, finishOne] {
        info->deleteLater();
        finishOne();
        if (generation != m_detailsGeneration) {
            return;
        }
        const auto results = QJsonDocument::fromJson(info->readAll()).object().value(QLatin1String("results")).toArray();
        if (!results.isEmpty()) {
            m_details = toMap(results.first().toObject());
            emit detailsChanged();
        }
    });

    auto* pkgbuild = m_network.get(request(pkgbuildUrl));
    connect(pkgbuild, &QNetworkReply::finished, this, [this, pkgbuild, generation, finishOne] {
        pkgbuild->deleteLater();
        finishOne();
        if (generation != m_detailsGeneration) {
            return;
        }
        if (pkgbuild->error() == QNetworkReply::NoError) {
            m_pkgbuild = QString::fromUtf8(pkgbuild->readAll());
            emit pkgbuildChanged();
        }
    });
}

void AurClient::refreshInstalled() {
    bool changed = false;
    for (auto& item : m_results) {
        auto map            = item.toMap();
        const auto* local   = m_packages->find(map.value(QStringLiteral("name")).toString());
        const bool installed = local != nullptr && local->installed();
        if (map.value(QStringLiteral("installed")).toBool() != installed) {
            map.insert(QStringLiteral("installed"), installed);
            map.insert(QStringLiteral("installedVersion"), installed ? local->installedVersion : QString());
            item    = map;
            changed = true;
        }
    }
    if (changed) {
        emit resultsChanged();
    }
    if (!m_details.isEmpty()) {
        const auto* local = m_packages->find(m_details.value(QStringLiteral("name")).toString());
        m_details.insert(QStringLiteral("installed"), local != nullptr && local->installed());
        emit detailsChanged();
    }
}
