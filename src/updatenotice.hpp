// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <optional>

class QNetworkReply;

namespace updatenotice {

struct Message {
    QString id;
    QString body;
};

/// Reads the answer of the notice server: a JSON object with "id" and "body", or a plain
/// sentence when there is no notice (an empty message). Nothing for any other answer.
std::optional<Message> parse(const QByteArray& answer);

/// Whether `message` still has to be shown. `seenId` is what pacman stored when the last
/// notice was accepted.
bool pending(const Message& message, const QByteArray& seenId);

/// pacman only asks on systems with a repository that has "cachyos" in its name.
bool applies(const QStringList& repos);

}  // namespace updatenotice

/// The CachyOS update notice: a short text the distribution publishes when a system
/// update needs attention, for example a manual step.
///
/// pacman asks for it before a full upgrade, shows it once and remembers its id in its
/// database directory. The upgrade started here runs with --noconfirm, where pacman
/// accepts the notice by itself, so it is fetched and shown here before that.
///
/// pacman asks the server again on its own: a notice published after the look taken
/// here is still accepted unseen.
class UpdateNotice : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// The notice that was not accepted yet; empty when there is none.
    Q_PROPERTY(QString text READ text NOTIFY changed FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY changed FINAL)
    /// The last request gave no usable answer, so a new notice may exist unseen.
    Q_PROPERTY(bool failed READ failed NOTIFY changed FINAL)

 public:
    explicit UpdateNotice(QObject* parent = nullptr);

    QString text() const { return m_text; }
    bool checking() const { return m_reply != nullptr; }
    bool failed() const { return m_failed; }

    /// Where pacman keeps its databases, and the repositories that are set up. Also
    /// looks again at what pacman stored as seen.
    void setDatabase(const QString& dbPath, const QStringList& repos);
    /// Asks the server, unless its last answer is younger than `maxAge`. A failed
    /// request keeps what is known.
    void check(std::chrono::seconds maxAge = std::chrono::seconds::zero());

    /// More than any notice needs; a longer answer counts as a failure, never cut short.
    static constexpr qint64 MAX_ANSWER = 64 * 1024;

 signals:
    void changed();

 private:
    void onFinished();
    /// Returns whether the text changed.
    bool update();

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    QTimer m_deadline;       ///< ends a request that takes too long
    QElapsedTimer m_answered;  ///< since the last usable answer
    updatenotice::Message m_message;
    QString m_dbPath;
    QString m_text;
    bool m_applies = false;
    bool m_failed  = false;
};
