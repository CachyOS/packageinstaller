// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapclient.hpp"

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// A `snap` stand-in: it prints the documented tables and can fail `refresh --list` on demand.
constexpr auto STUB =
    R"(#!/bin/bash
 case "$1" in
   find)
     query="${!#}"
     if [ "$query" = "missing" ]; then
       echo "error: no matching snaps" >&2
       exit 1
     fi
     printf '%s\n' \
       'Name  Version  Developer  Notes  Summary' \
       'vlc   3.0.4   videolan✓   -   The ultimate media player.'
     ;;
   list)
     printf '%s\n' \
       'Name  Version  Rev  Tracking  Publisher  Notes' \
       'firefox  120.0.1-1  3504  latest/stable  mozilla✓  -' \
       'alacritty 0.8.0 46 latest/stable snapcrafters classic'
     ;;
   refresh)
     if [ -n "$SNAP_STUB_MARKER" ] && [ -f "$SNAP_STUB_MARKER" ]; then
       exit 1
     fi
     printf '%s\n' \
       'Name  Version  Rev  Size  Publisher  Notes' \
       'firefox  129.0-2  4698  282MB  mozilla✓  -'
     ;;
   *) exit 1 ;;
 esac
 )";

struct StoreReply {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
};

struct StoreRequest {
    QString path;
    QMap<QString, QString> headers;
};

/// Small local HTTP endpoint for the store; records what the client asked for.
class StoreServer : public QTcpServer {
 public:
    using Responder = std::function<StoreReply(const QString& path)>;

    StoreServer() { listen(QHostAddress::LocalHost, 0); }

    QUrl url() const { return QUrl(u"http://127.0.0.1:%1/v2/snaps/find"_s.arg(serverPort())); }

    void respondWith(Responder responder) { m_responder = std::move(responder); }

    qsizetype requestCount() const { return m_requests.size(); }
    const StoreRequest& request(qsizetype index) const { return m_requests.at(index); }

 private:
    void incomingConnection(qintptr descriptor) override {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            socket->deleteLater();
            return;
        }
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            *buffer += socket->readAll();
            const auto end = buffer->indexOf("\r\n\r\n");
            if (end < 0) {
                return;
            }
            const auto lines = QString::fromLatin1(buffer->first(end)).split(u"\r\n"_s);
            StoreRequest received;
            if (!lines.isEmpty()) {
                const auto parts = lines.first().split(u' ');
                if (parts.size() > 1) {
                    received.path = parts.at(1);
                }
            }
            for (const auto& line : lines) {
                const auto colon = line.indexOf(u':');
                if (colon > 0) {
                    received.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
                }
            }
            m_requests << received;
            socket->disconnect(this);

            const auto reply   = m_responder ? m_responder(received.path) : StoreReply{};
            const auto reason  = reply.status == 200 ? "OK" : "Internal Server Error";
            const auto payload = "HTTP/1.1 " + QByteArray::number(reply.status) + " " + reason + "\r\n"
                + "Content-Type: application/json\r\n"
                + "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n"
                + "Connection: close\r\n\r\n" + reply.body;
            const auto write = [socket, payload] {
                if (socket->state() != QAbstractSocket::UnconnectedState) {
                    socket->write(payload);
                    socket->flush();
                }
                socket->disconnectFromHost();
            };
            if (reply.delayMs > 0) {
                QTimer::singleShot(reply.delayMs, socket, write);
            } else {
                write();
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    QList<StoreRequest> m_requests;
    Responder m_responder;
};

QString queryOf(const QString& path) {
    const auto mark = path.indexOf(u'?');
    return mark < 0 ? QString() : QUrlQuery(path.mid(mark + 1)).queryItemValue(u"q"_s);
}

QByteArray resultJson(const QString& name, const QString& publisher = u"VideoLAN"_s,
    const QString& summary = u"The ultimate media player"_s) {
    QJsonObject item;
    item.insert(u"name"_s, name);
    item.insert(u"revision"_s, QJsonObject{{u"version"_s, u"3.0.24-rc1"_s}});
    item.insert(u"summary"_s, summary);
    if (!publisher.isEmpty()) {
        item.insert(u"publisher"_s, QJsonObject{{u"display-name"_s, publisher}, {u"validation"_s, u"verified"_s}});
    }
    QJsonObject root;
    root.insert(u"results"_s, QJsonArray{item});
    return QJsonDocument(root).toJson();
}

}  // namespace

class TestSnapClient : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        const auto stub = m_dir->filePath(u"snap"_s);
        QFile file(stub);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(STUB);
        file.close();
        QVERIFY(QFile::setPermissions(stub, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther));
        m_marker = m_dir->filePath(u"fail-updates"_s);
        qputenv("SNAP_STUB_MARKER", m_marker.toUtf8());
        // The client finds the stub through QStandardPaths::findExecutable("snap").
        qputenv("PATH", (m_dir->path() + u":"_s + QString::fromLocal8Bit(qgetenv("PATH"))).toLocal8Bit());
    }

    void cleanup() { qunsetenv("CACHYOS_PI_SNAP_STORE_URL"); }

    void loadsAndSearches() {
        StoreServer store;
        QVERIFY(store.isListening());
        store.respondWith([](const QString& path) -> StoreReply {
            const auto query = queryOf(path);
            if (query == u"missing"_s) {
                return {500, "{}"};
            }
            return {200, resultJson(query, u"videolan✓"_s, u"The ultimate media player."_s)};
        });
        qputenv("CACHYOS_PI_SNAP_STORE_URL", store.url().toString().toUtf8());

        SnapClient client;
        QSignalSpy changed(&client, &SnapClient::changed);

        client.refresh();
        QTRY_VERIFY(changed.count() >= 1);
        QVERIFY(client.available());
        QVERIFY(client.daemonRunning());
        QVERIFY(!client.loading());
        QCOMPARE(client.installed().size(), 2);
        QCOMPARE(client.installed().at(0).name, u"firefox"_s);
        QCOMPARE(client.installed().at(0).version, u"120.0.1-1"_s);
        QCOMPARE(client.installed().at(0).revision, u"3504"_s);
        QCOMPARE(client.installed().at(0).channel, u"latest/stable"_s);
        QCOMPARE(client.updateCount(), 1);
        QVERIFY(client.installed().at(0).upgradable);
        QVERIFY(!client.installed().at(1).upgradable);
        QVERIFY(client.app(u"firefox"_s).installed);

        QSignalSpy results(&client, &SnapClient::resultsChanged);
        client.search(u"vlc"_s);
        QTRY_VERIFY(results.count() >= 1);
        QCOMPARE(client.results().size(), 1);
        QCOMPARE(client.results().at(0).name, u"vlc"_s);
        QCOMPARE(client.results().at(0).publisher, u"videolan✓"_s);
        QCOMPARE(client.results().at(0).summary, u"The ultimate media player."_s);
        QVERIFY(client.searchError().isEmpty());

        // a failing store lookup clears the results and exposes the error
        client.search(u"missing"_s);
        QTRY_VERIFY(results.count() >= 2);
        QVERIFY(client.results().isEmpty());
        QVERIFY(!client.searchError().isEmpty());

        // a successful search clears the error again
        client.search(u"vlc"_s);
        QTRY_VERIFY(results.count() >= 3);
        QVERIFY(client.searchError().isEmpty());

        // a failing `refresh --list` means "no updates", not an error
        QFile marker(m_marker);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("1");
        marker.close();
        client.refresh();
        QTRY_VERIFY(changed.count() >= 2);
        QCOMPARE(client.updateCount(), 0);
        QVERIFY(client.searchError().isEmpty());
        QCOMPARE(client.results().size(), 1);  // refresh re-ran the current query
    }

    void storeSearch() {
        StoreServer store;
        QVERIFY(store.isListening());
        store.respondWith([](const QString&) -> StoreReply {
            return {200, R"({"results":[{"name":"vlc","revision":{"version":"3.0.24-rc1"},"summary":"The ultimate media player","publisher":{"display-name":"VideoLAN","validation":"verified"}}]})"};
        });
        qputenv("CACHYOS_PI_SNAP_STORE_URL", store.url().toString().toUtf8());

        SnapClient client;
        QSignalSpy results(&client, &SnapClient::resultsChanged);
        client.search(u"vlc"_s);
        QTRY_VERIFY(results.count() >= 1);

        QCOMPARE(client.results().size(), 1);
        const auto item = client.results().first();
        QCOMPARE(item.name, u"vlc"_s);
        QCOMPARE(item.version, u"3.0.24-rc1"_s);
        QCOMPARE(item.summary, u"The ultimate media player"_s);
        QCOMPARE(item.publisher, u"VideoLAN"_s);
        QVERIFY(client.searchError().isEmpty());

        QCOMPARE(store.requestCount(), 1);
        QVERIFY(store.request(0).path.contains(u"q=vlc"_s));
        QCOMPARE(store.request(0).headers.value(u"snap-device-series"_s), u"16"_s);
    }

    void staleStoreReply() {
        StoreServer store;
        QVERIFY(store.isListening());
        store.respondWith([](const QString& path) -> StoreReply {
            const auto query = queryOf(path);
            return {200, resultJson(query), query == u"vlc"_s ? 500 : 0};
        });
        qputenv("CACHYOS_PI_SNAP_STORE_URL", store.url().toString().toUtf8());

        SnapClient client;
        QSignalSpy results(&client, &SnapClient::resultsChanged);
        client.search(u"vlc"_s);
        QTRY_VERIFY(store.requestCount() >= 1);  // the first request is out, its reply is still delayed
        client.search(u"firefox"_s);
        QTRY_VERIFY(results.count() >= 1);
        QCOMPARE(store.requestCount(), 2);
        QCOMPARE(client.results().size(), 1);
        QCOMPARE(client.results().first().name, u"firefox"_s);

        // the delayed first reply must not overwrite the newer one when it arrives
        QTest::qWait(700);
        QCOMPARE(client.results().size(), 1);
        QCOMPARE(client.results().first().name, u"firefox"_s);
    }

    void storeFailure() {
        StoreServer store;
        QVERIFY(store.isListening());
        store.respondWith([](const QString&) -> StoreReply { return {500, "{}"}; });
        qputenv("CACHYOS_PI_SNAP_STORE_URL", store.url().toString().toUtf8());

        SnapClient client;
        QSignalSpy results(&client, &SnapClient::resultsChanged);
        client.search(u"vlc"_s);
        QTRY_VERIFY(results.count() >= 1);
        QVERIFY(client.results().isEmpty());
        QVERIFY(!client.searchError().isEmpty());
    }

    void malformedStoreItem() {
        StoreServer store;
        QVERIFY(store.isListening());
        store.respondWith([](const QString&) -> StoreReply { return {200, R"({"results":[{"name":"odd"}]})"}; });
        qputenv("CACHYOS_PI_SNAP_STORE_URL", store.url().toString().toUtf8());

        SnapClient client;
        QSignalSpy results(&client, &SnapClient::resultsChanged);
        client.search(u"vlc"_s);
        QTRY_VERIFY(results.count() >= 1);
        QCOMPARE(client.results().size(), 1);
        QCOMPARE(client.results().first().name, u"odd"_s);
        QVERIFY(client.results().first().version.isEmpty());
        QVERIFY(client.results().first().publisher.isEmpty());
        QVERIFY(client.searchError().isEmpty());
    }

 private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_marker;
};

// QFutureWatcher signals need a QCoreApplication to deliver their posted callouts.
QTEST_GUILESS_MAIN(TestSnapClient)
#include "test_snapclient.moc"
