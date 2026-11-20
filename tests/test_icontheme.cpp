// SPDX-License-Identifier: GPL-3.0-or-later
#include "icontheme.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestIconTheme : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        write(u"icons/child/index.theme"_s, R"([Icon Theme]
Name=Child
Inherits=parent,hicolor
Directories=apps/16,apps/48,actions/48,legacy/32

[apps/16]
Size=16
Context=Applications
Type=Fixed

[apps/48]
Size=48
Context=Applications
Type=Scalable

[actions/48]
Size=48
Context=Actions
Type=Scalable

[legacy/32]
Size=32
Type=Fixed
)");
        write(u"icons/child/apps/16/editor.png"_s);
        write(u"icons/child/apps/48/editor.svg"_s);
        write(u"icons/child/apps/16/org.kde.kate.png"_s);
        write(u"icons/child/actions/48/go-up.svg"_s);
        write(u"icons/child/legacy/32/old.png"_s);
        write(u"icons/child/apps/48/notes.txt"_s);
        write(u"local/child/apps/48/later.svg"_s);

        write(u"icons/parent/index.theme"_s, R"([Icon Theme]
Inherits=child
Directories=256x256/apps

[256x256/apps]
Size=256
Type=Fixed
)");
        write(u"icons/parent/256x256/apps/editor.png"_s);
        write(u"icons/parent/256x256/apps/terminal.png"_s);

        write(u"icons/hicolor/index.theme"_s, "[Icon Theme]\nDirectories=48x48/apps,128x128/apps\n\n[48x48/apps]\nSize=48\n\n[128x128/apps]\nSize=128\n");
        write(u"icons/hicolor/48x48/apps/terminal.png"_s);
        write(u"icons/hicolor/48x48/apps/browser.png"_s);
        write(u"icons/hicolor/128x128/apps/browser.png"_s);

        write(u"pixmaps/browser.png"_s);
        write(u"pixmaps/legacy-tool.png"_s);

        m_icons = icontheme::appIcons({.name = u"child"_s,
            .searchPaths                     = {path(u"icons"_s), path(u"local"_s), path(u"missing"_s)},
            .fallbackPaths                   = {path(u"pixmaps"_s)}});
    }

    void lookup_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("file");
        QTest::newRow("scalable over a small bitmap") << u"editor"_s << u"icons/child/apps/48/editor.svg"_s;
        QTest::newRow("dots in the name") << u"org.kde.kate"_s << u"icons/child/apps/16/org.kde.kate.png"_s;
        QTest::newRow("second search path") << u"later"_s << u"local/child/apps/48/later.svg"_s;
        QTest::newRow("directory without a context") << u"old"_s << QString();
        QTest::newRow("from the inherited theme") << u"terminal"_s << u"icons/parent/256x256/apps/terminal.png"_s;
        QTest::newRow("largest bitmap of hicolor") << u"browser"_s << u"icons/hicolor/128x128/apps/browser.png"_s;
        QTest::newRow("fallback directory") << u"legacy-tool"_s << u"pixmaps/legacy-tool.png"_s;
        QTest::newRow("not an application icon") << u"go-up"_s << QString();
        QTest::newRow("not an image") << u"notes"_s << QString();
        QTest::newRow("unknown") << u"nothing"_s << QString();
    }

    void lookup() {
        QFETCH(QString, name);
        QFETCH(QString, file);
        QCOMPARE(m_icons.value(name), file.isEmpty() ? QString() : path(file));
    }

    void unknownTheme() {
        const auto icons = icontheme::appIcons({.name = u"nonexistent"_s, .searchPaths = {path(u"icons"_s)}, .fallbackPaths = {}});
        QCOMPARE(icons.value(u"terminal"_s), path(u"icons/hicolor/48x48/apps/terminal.png"_s));
        QVERIFY(!icons.contains(u"editor"_s));
    }

 private:
    QString path(const QString& relative) const { return m_root.filePath(relative); }

    void write(const QString& relative, const QByteArray& content = {}) {
        QVERIFY(QDir().mkpath(QFileInfo(path(relative)).absolutePath()));
        QFile file(path(relative));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QTemporaryDir m_root;
    QHash<QString, QString> m_icons;
};

QTEST_GUILESS_MAIN(TestIconTheme)
#include "test_icontheme.moc"
