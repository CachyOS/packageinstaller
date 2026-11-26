// SPDX-License-Identifier: GPL-3.0-or-later
#include "userthemes.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestUserThemes : public QObject {
    Q_OBJECT

    QTemporaryDir m_config;

    static QVariantMap palette(const QVariantMap& theme, const QString& kind) { return theme.value(kind).toMap(); }

    void write(const QString& name, const QByteArray& json) {
        QFile file(QDir(m_config.path()).filePath(u"CachyOS/cachyos-pi/themes/"_s + name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
    }

 private slots:
    void initTestCase() {
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.path()));
    }

    void darkAndLight() {
        const auto theme = UserThemes::parse("{\"name\": \" Mine \", \"description\": \"hint\","
                                             " \"dark\": {\"window\": \"#101010\", \"text\": \"#eeeeee\", \"accent\": \"#ff8800\", \"border\": \"#333333\"},"
                                             " \"light\": {\"window\": \"#f0f0f0\", \"text\": \"#111111\", \"accent\": \"#aa5500\"}}");
        QVERIFY(theme.has_value());
        QCOMPARE(theme->value(u"title"_s), u"Mine"_s);
        QCOMPARE(theme->value(u"hint"_s), u"hint"_s);
        QCOMPARE(palette(*theme, u"dark"_s).value(u"border"_s), u"#333333"_s);
        QCOMPARE(palette(*theme, u"light"_s).value(u"window"_s), u"#f0f0f0"_s);
        // left out colours are blended in Look.qml, not here
        QVERIFY(!palette(*theme, u"light"_s).contains(u"border"_s));
        QVERIFY(!theme->contains(u"hero"_s));
    }

    void onePaletteForBoth() {
        const auto colors = UserThemes::parse("{\"name\": \"a\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"}}");
        QVERIFY(colors.has_value());
        QCOMPARE(palette(*colors, u"dark"_s), palette(*colors, u"light"_s));

        const auto darkOnly = UserThemes::parse("{\"name\": \"a\", \"dark\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"}}");
        QVERIFY(darkOnly.has_value());
        QCOMPARE(palette(*darkOnly, u"light"_s).value(u"window"_s), u"#000"_s);
    }

    void unknownAndInvalidColoursAreDropped() {
        const auto theme = UserThemes::parse("{\"name\": \"a\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\","
                                             " \"surface\": \"not a colour\", \"sidebar\": 5, \"somethingElse\": \"#123456\"}}");
        QVERIFY(theme.has_value());
        const auto colours = palette(*theme, u"dark"_s);
        QVERIFY(!colours.contains(u"surface"_s));
        QVERIFY(!colours.contains(u"sidebar"_s));
        QVERIFY(!colours.contains(u"somethingElse"_s));
    }

    void hero() {
        const auto whole = UserThemes::parse("{\"name\": \"a\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"},"
                                             " \"hero\": {\"stops\": [\"#111\", \"#222\", \"#333\"], \"text\": \"#fff\", \"sub\": \"#eee\", \"button\": \"red\", \"buttonHover\": \"pink\", \"ink\": \"#000\"}}");
        QVERIFY(whole.has_value());
        QCOMPARE(whole->value(u"hero"_s).toMap().value(u"stops"_s).toList().size(), 3);

        // a banner with a colour missing is left to Look.qml
        const auto partial = UserThemes::parse("{\"name\": \"a\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"},"
                                               " \"hero\": {\"stops\": [\"#111\", \"#222\"], \"text\": \"#fff\"}}");
        QVERIFY(partial.has_value());
        QVERIFY(!partial->contains(u"hero"_s));
    }

    void notThemes_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not json") << QByteArray("window=#000");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("no name") << QByteArray("{\"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"}}");
        QTest::newRow("blank name") << QByteArray("{\"name\": \" \", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"}}");
        QTest::newRow("no palette") << QByteArray("{\"name\": \"a\"}");
        QTest::newRow("no accent") << QByteArray("{\"name\": \"a\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\"}}");
        QTest::newRow("bad window") << QByteArray("{\"name\": \"a\", \"colors\": {\"window\": \"nope\", \"text\": \"#fff\", \"accent\": \"red\"}}");
    }

    void notThemes() {
        QFETCH(QByteArray, json);
        QVERIFY(!UserThemes::parse(json).has_value());
    }

    void folderIsWatched() {
        UserThemes themes;
        QCOMPARE(themes.folder(), QDir(m_config.path()).filePath(u"CachyOS/cachyos-pi/themes"_s));
        QVERIFY(QDir(themes.folder()).exists());
        // the test folder is not in the home folder
        QCOMPARE(themes.shownFolder(), themes.folder());
        QVERIFY(themes.themes().isEmpty());

        QSignalSpy changed(&themes, &UserThemes::themesChanged);
        write(u"wall.json"_s, "{\"name\": \"Wall\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"red\"}}");
        QVERIFY(changed.wait());
        QCOMPARE(themes.themes().keys(), QStringList{u"user:wall"_s});

        // rewritten in place
        write(u"wall.json"_s, "{\"name\": \"Wall\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"blue\"}}");
        QVERIFY(changed.wait());
        QCOMPARE(palette(themes.themes().value(u"user:wall"_s).toMap(), u"dark"_s).value(u"accent"_s), u"blue"_s);

        // replaced by a renamed file, as tools that write atomically do
        write(u"wall.json.tmp"_s, "{\"name\": \"Wall\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"green\"}}");
        QDir folder(themes.folder());
        QVERIFY(folder.remove(u"wall.json"_s));
        QVERIFY(folder.rename(u"wall.json.tmp"_s, u"wall.json"_s));
        QVERIFY(changed.wait());
        QCOMPARE(palette(themes.themes().value(u"user:wall"_s).toMap(), u"dark"_s).value(u"accent"_s), u"green"_s);

        // and it keeps being watched after that
        write(u"wall.json"_s, "{\"name\": \"Wall\", \"colors\": {\"window\": \"#000\", \"text\": \"#fff\", \"accent\": \"teal\"}}");
        QVERIFY(changed.wait());
        QCOMPARE(palette(themes.themes().value(u"user:wall"_s).toMap(), u"dark"_s).value(u"accent"_s), u"teal"_s);

        QVERIFY(folder.remove(u"wall.json"_s));
        QVERIFY(changed.wait());
        QVERIFY(themes.themes().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUserThemes)
#include "test_userthemes.moc"
