// SPDX-License-Identifier: GPL-3.0-or-later
#include "paletteprovider.hpp"

#include <QTest>

using namespace Qt::StringLiterals;

class TestPalette : public QObject {
    Q_OBJECT

 private slots:
    void readsSections() {
        // Plain literals, not raw strings: moc treats a '#' inside a raw string as
        // the start of a preprocessor line and then drops the whole class.
        const auto palette = paletteprovider::parse(
            "{\"name\": \"Event Horizon\","
            " \"dark\": {\"window\": \"#000000\", \"accent\": \"#22d3b7\"},"
            " \"light\": {\"window\": \"#ffffff\", \"accent\": \"#0a9f88\"},"
            " \"hero\": {\"stops\": [\"#111111\", \"#222222\", \"#333333\"]}}");
        QVERIFY(palette.has_value());
        QCOMPARE(palette->value(u"dark"_s).toMap().value(u"accent"_s).toString(), u"#22d3b7"_s);
        QCOMPARE(palette->value(u"light"_s).toMap().value(u"accent"_s).toString(), u"#0a9f88"_s);
        QCOMPARE(palette->value(u"hero"_s).toMap().value(u"stops"_s).toList().size(), 3);
    }

    void unusable_data() {
        QTest::addColumn<QByteArray>("answer");
        // the shell writes these, but a half-written or foreign file must not theme the app
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("not json") << QByteArray("<html>502</html>");
        QTest::newRow("not an object") << QByteArray("[{\"dark\": {}}]");
        QTest::newRow("no dark") << QByteArray("{\"light\": {}, \"hero\": {\"stops\": [\"a\",\"b\",\"c\"]}}");
        QTest::newRow("no light") << QByteArray("{\"dark\": {}, \"hero\": {\"stops\": [\"a\",\"b\",\"c\"]}}");
        QTest::newRow("no hero") << QByteArray("{\"dark\": {}, \"light\": {}}");
        QTest::newRow("section not an object")
            << QByteArray("{\"dark\": [], \"light\": {}, \"hero\": {\"stops\": [\"a\",\"b\",\"c\"]}}");
        QTest::newRow("no gradient") << QByteArray("{\"dark\": {}, \"light\": {}, \"hero\": {}}");
        QTest::newRow("short gradient") << QByteArray("{\"dark\": {}, \"light\": {}, \"hero\": {\"stops\": [\"a\",\"b\"]}}");
        QTest::newRow("cut short") << QByteArray("{\"dark\": {}, \"light\": {}, \"hero\": {\"stops\": [\"a\",\"b\",\"c\"]}");
    }

    void unusable() {
        QFETCH(QByteArray, answer);
        QVERIFY(!paletteprovider::parse(answer).has_value());
    }
};

QTEST_MAIN(TestPalette)
#include "test_palette.moc"
