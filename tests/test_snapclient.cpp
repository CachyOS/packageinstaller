// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapclient.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

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

    void loadsAndSearches() {
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

        // a failing `find` clears the results and exposes the error
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

 private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_marker;
};

// QFutureWatcher signals need a QCoreApplication to deliver their posted callouts.
QTEST_GUILESS_MAIN(TestSnapClient)
#include "test_snapclient.moc"
