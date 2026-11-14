// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatenotice.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

using namespace Qt::StringLiterals;

namespace {

constexpr auto NOTICE_URL = "https://iso-stats.cachyos.org/api/v2/last_update_notice"_L1;

QUrl noticeUrl() {
    // another server can be named for testing
    const auto custom = qEnvironmentVariable("CACHYOS_PI_UPDATE_NOTICE_URL");
    return QUrl(custom.isEmpty() ? QString(NOTICE_URL) : custom);
}

}  // namespace

namespace updatenotice {

std::optional<Message> parse(const QByteArray& answer) {
    if (answer.trimmed() == "No notice found") {
        return Message{};
    }
    const auto document = QJsonDocument::fromJson(answer);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const auto object = document.object();
    Message message{.id = object.value("id"_L1).toString(), .body = object.value("body"_L1).toString()};
    // line breaks that reach us still escaped
    message.body.replace("\\n"_L1, "\n"_L1);
    message.body = message.body.trimmed();
    // like pacman: an object without both parts is no notice
    if (message.id.isEmpty() || message.body.isEmpty()) {
        return Message{};
    }
    return message;
}

bool pending(const Message& message, const QByteArray& seenId) {
    // pacman compares the whole file with the id
    return !message.id.isEmpty() && message.id.toUtf8() != seenId;
}

bool applies(const QStringList& repos) {
    return std::ranges::any_of(repos, [](const QString& repo) { return repo.contains("cachyos"_L1); });
}

}  // namespace updatenotice

UpdateNotice::UpdateNotice(QObject* parent) : QObject(parent) {
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(10000);
    // only runs while a request does
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_reply->abort(); });
}

void UpdateNotice::setDatabase(const QString& dbPath, const QStringList& repos) {
    m_dbPath  = dbPath;
    m_applies = updatenotice::applies(repos);
    if (update()) {
        emit changed();
    }
}

void UpdateNotice::check(std::chrono::seconds maxAge) {
    if (!m_applies || m_dbPath.isEmpty() || m_reply != nullptr) {
        return;
    }
    if (m_answered.isValid() && m_answered.durationElapsed() < maxAge) {
        return;
    }
    QNetworkRequest request(noticeUrl());
    request.setHeader(QNetworkRequest::UserAgentHeader, u"cachyos-pi/" PI_VERSION ""_s);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply->bytesAvailable() > MAX_ANSWER) {
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateNotice::onFinished);
    m_deadline.start();
    emit changed();
}

void UpdateNotice::onFinished() {
    auto* reply = std::exchange(m_reply, nullptr);
    reply->deleteLater();
    m_deadline.stop();
    // an aborted request ends with an error
    auto message = reply->error() == QNetworkReply::NoError ? updatenotice::parse(reply->readAll()) : std::nullopt;
    m_failed     = !message.has_value();
    if (message) {
        m_message = *std::move(message);
        m_answered.start();
    }
    update();
    emit changed();
}

bool UpdateNotice::update() {
    QByteArray seen;
    // pacman writes the id of the accepted notice here
    if (QFile file(m_dbPath + u"/local/CACHY_UPDATE_NOTICE"_s); !m_dbPath.isEmpty() && file.open(QIODevice::ReadOnly)) {
        seen = file.read(4096);
    }
    const auto text = m_applies && updatenotice::pending(m_message, seen) ? m_message.body : QString();
    if (text == m_text) {
        return false;
    }
    m_text = text;
    return true;
}
