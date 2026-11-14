// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatenotice.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

class TestUpdateNotice : public QObject {
    Q_OBJECT

 private slots:
    void answers() {
        const auto notice = updatenotice::parse(json(u"2130f114-3924"_s, u"Remove foo first.\nThen update."_s));
        QVERIFY(notice.has_value());
        QCOMPARE(notice->id, u"2130f114-3924"_s);
        QCOMPARE(notice->body, u"Remove foo first.\nThen update."_s);
        // line breaks that arrive still escaped
        QCOMPARE(updatenotice::parse(json(u"1"_s, u"one\\ntwo\n"_s))->body, u"one\ntwo"_s);
        // other keys and their order do not matter
        QCOMPARE(updatenotice::parse("{\"created\":5,\"body\":\"text\",\"id\":\"7\"}")->id, u"7"_s);
        // nothing is cut off
        QCOMPARE(updatenotice::parse(json(u"1"_s, QString(20000, u'x')))->body.size(), 20000);
    }

    void noNotice_data() {
        QTest::addColumn<QByteArray>("answer");
        QTest::newRow("sentence") << QByteArray("No notice found");
        QTest::newRow("sentence and line break") << QByteArray("No notice found\n");
        QTest::newRow("empty object") << QByteArray("{}");
        QTest::newRow("no body") << QByteArray("{\"id\":\"1\"}");
        QTest::newRow("no id") << QByteArray("{\"body\":\"text\"}");
        QTest::newRow("blank body") << json(u"1"_s, u"  "_s);
        QTest::newRow("wrong types") << QByteArray("{\"id\":5,\"body\":[\"text\"]}");
    }

    void noNotice() {
        QFETCH(QByteArray, answer);
        const auto notice = updatenotice::parse(answer);
        QVERIFY(notice.has_value());
        QVERIFY(notice->id.isEmpty());
    }

    void unusableAnswers() {
        // these must not be taken for "there is no notice"
        QVERIFY(!updatenotice::parse("").has_value());
        QVERIFY(!updatenotice::parse("<html>502</html>").has_value());
        QVERIFY(!updatenotice::parse("{\"id\":\"1\",\"body\":\"cut o").has_value());
        QVERIFY(!updatenotice::parse("[{\"id\":\"1\",\"body\":\"text\"}]").has_value());
    }

    void seen() {
        const updatenotice::Message notice{.id = u"2130f114"_s, .body = u"text"_s};
        QVERIFY(updatenotice::pending(notice, {}));
        QVERIFY(updatenotice::pending(notice, "another"));
        QVERIFY(!updatenotice::pending(notice, "2130f114"));
        // pacman compares the whole file
        QVERIFY(updatenotice::pending(notice, "2130f114\n"));
        QVERIFY(!updatenotice::pending({}, {}));
    }

    void repositories() {
        QVERIFY(updatenotice::applies({u"core"_s, u"cachyos-v3"_s}));
        QVERIFY(updatenotice::applies({u"my-cachyos"_s}));
        QVERIFY(!updatenotice::applies({u"core"_s, u"extra"_s}));
        QVERIFY(!updatenotice::applies({}));
    }

 private:
    static QByteArray json(const QString& id, const QString& body) {
        return QJsonDocument(QJsonObject{{u"id"_s, id}, {u"body"_s, body}}).toJson(QJsonDocument::Compact);
    }
};

QTEST_APPLESS_MAIN(TestUpdateNotice)
#include "test_updatenotice.moc"
