// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestAppImage : public QObject {
    Q_OBJECT

 private slots:
    void quoting() {
        QCOMPARE(appimage::quoteExec(u"/home/u/Applications/Editor-1.2.AppImage"_s),
            u"/home/u/Applications/Editor-1.2.AppImage"_s);
        QCOMPARE(appimage::quoteExec(u"/home/my user/A.AppImage"_s), u"\"/home/my user/A.AppImage\""_s);
        QCOMPARE(appimage::quoteExec(u"/h/a$b`c\"d\\e%f"_s), u"\"/h/a\\\\$b\\\\`c\\\\\"d\\\\\\\\e%%f\""_s);
    }

    void program() {
        const auto app = u"/a/App.AppImage"_s;
        QCOMPARE(appimage::replaceProgram(u"editor %U"_s, app), u"/a/App.AppImage %U"_s);
        QCOMPARE(appimage::replaceProgram(u"editor"_s, app), app);
        QCOMPARE(appimage::replaceProgram(u"\"my editor\" --new %f"_s, app), u"/a/App.AppImage --new %f"_s);
        QCOMPARE(appimage::replaceProgram(u"env A=1 B=two editor --x"_s, app), u"/a/App.AppImage --x"_s);
        QCOMPARE(appimage::replaceProgram(u"  AppRun   %F  "_s, app), u"/a/App.AppImage %F"_s);
    }

    void integration() {
        const auto original = 
            u"# written by a tool\r\n"
            "[Desktop Entry]\r\n"
            "Type=Application\n"
            "Name=Editor\n"
            "Name[de]=Editor DE\n"
            "Exec=editor %U\n"
            "Exec[de]=/usr/bin/evil\n"
            "Exec=/usr/bin/second\n"
            "TryExec=editor\n"
            "Icon=editor\n"
            "DBusActivatable=true\n"
            "NoDisplay=true\n"
            "X-CachyOS-PI-AppImage=/etc/passwd\n"
            "Actions=New;Ghost;New;\n"
            "\n"
            "[Desktop Action New]\n"
            "Name=New\n"
            "Icon=document-new\n"
            "Exec=editor --new\n"
            "X-Odd=1\n"
            "[Desktop Action Unlisted]\n"
            "Name=Unlisted\n"
            "Exec=/usr/bin/evil\n"
            "[Desktop Entry]\n"
            "Exec=/usr/bin/evil\n"_s;
        const auto entry = appimage::integrate(original, u"/h/Applications/E.AppImage"_s, u"/h/icons/e.png"_s);
        QCOMPARE(entry, 
            u"[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Editor\n"
            "Name[de]=Editor DE\n"
            "Exec=/h/Applications/E.AppImage %U\n"
            "Icon=/h/icons/e.png\n"
            "Actions=New;\n"
            "X-CachyOS-PI-AppImage=/h/Applications/E.AppImage\n"
            "\n"
            "[Desktop Action New]\n"
            "Name=New\n"
            "Icon=document-new\n"
            "Exec=/h/Applications/E.AppImage --new\n"_s);
        const auto keys = appimage::mainGroup(entry);
        QCOMPARE(keys.value(u"X-CachyOS-PI-AppImage"_s), u"/h/Applications/E.AppImage"_s);
        QCOMPARE(keys.value(u"Name"_s), u"Editor"_s);
    }

    void unusableEntries() {
        const auto path = u"/a/A.AppImage"_s;
        QVERIFY(appimage::integrate(u"Name=X\nExec=x\n"_s, path, {}).isEmpty());                    // no group
        QVERIFY(appimage::integrate(u"[Desktop Entry]\nExec=x\n"_s, path, {}).isEmpty());           // no name
        QVERIFY(appimage::integrate(u"[Desktop Entry]\nName=X\n"_s, path, {}).isEmpty());           // nothing to start
        QVERIFY(appimage::integrate(u"[Desktop Entry]\nType=Link\nName=X\nExec=x\n"_s, path, {}).isEmpty());
        // keys of other groups do not count for the main group
        QVERIFY(appimage::integrate(u"[Other]\nName=X\nExec=x\n[Desktop Entry]\nType=Application\n"_s, path, {}).isEmpty());
        // too large once it is written out
        const auto huge = u"[Desktop Entry]\nName=X\nExec=x\nComment="_s + QString(300 * 1024, u'c') + u'\n';
        QVERIFY(appimage::integrate(huge, path, {}).isEmpty());
    }

    void typeIsAlwaysWritten() {
        // a missing type is supplied, a second one cannot change it
        const auto missing = appimage::integrate(u"[Desktop Entry]\nName=X\nExec=x\nIcon=x\n"_s, u"/a/A.AppImage"_s, {});
        QVERIFY(missing.startsWith(u"[Desktop Entry]\nType=Application\n"_s));
        QVERIFY(missing.contains(u"Icon=x\n"_s));  // the icon name is kept when no file was found
        const auto twice = appimage::integrate(u"[Desktop Entry]\nType=Application\nType=Link\nName=X\nExec=x\n"_s, u"/a/A.AppImage"_s, {});
        QCOMPARE(twice.count(u"Type="_s), 1);
        QVERIFY(!twice.contains(u"Link"_s));
    }

    void commandLines() {
        QCOMPARE(appimage::commandLine(u"/a/App.AppImage --gui %U"_s), (QStringList{u"/a/App.AppImage"_s, u"--gui"_s}));
        QCOMPARE(appimage::commandLine(u"\"/my apps/A.AppImage\" \"two words\" %f"_s),
            (QStringList{u"/my apps/A.AppImage"_s, u"two words"_s}));
        QCOMPARE(appimage::commandLine(u"app --rate=50%% --name=%c"_s), (QStringList{u"app"_s, u"--rate=50%"_s, u"--name="_s}));
        // what quoteExec() writes comes back unchanged
        const auto odd = u"/h/a$b`c\"d\\e%f g"_s;
        QCOMPARE(appimage::commandLine(appimage::quoteExec(odd) + u" %U"_s), QStringList{odd});
        QVERIFY(appimage::commandLine(QString()).isEmpty());
    }

    void desktopNames() {
        QCOMPARE(appimage::safeDesktopName(u"org.example.App.desktop"_s), u"org.example.App.desktop"_s);
        QCOMPARE(appimage::safeDesktopName(u"x"_s), u"x.desktop"_s);
        QCOMPARE(appimage::safeDesktopName(u".desktop"_s), u"app.desktop"_s);
        const auto longName = appimage::safeDesktopName(QString(300, u'a') + u".desktop"_s);
        QVERIFY(longName.endsWith(u".desktop"_s));
        QCOMPARE(longName.size(), 108);
    }

    void managedPaths() {
        const auto dir = u"/home/u/Applications"_s;
        QVERIFY(appimage::isManagedPath(dir + u"/Editor-1.2.AppImage"_s, dir));
        QVERIFY(!appimage::isManagedPath(dir + u"/../Documents/important"_s, dir));
        QVERIFY(!appimage::isManagedPath(dir + u"/sub/Editor.AppImage"_s, dir));
        QVERIFY(!appimage::isManagedPath(dir + u"/"_s, dir));
        QVERIFY(!appimage::isManagedPath(dir + u"/.."_s, dir));
        QVERIFY(!appimage::isManagedPath(dir + u"/my file.AppImage"_s, dir));
        QVERIFY(!appimage::isManagedPath(u"/home/u/ApplicationsX/a.AppImage"_s, dir));
        QVERIFY(!appimage::isManagedPath(u"/etc/passwd"_s, dir));
    }

    void names() {
        QCOMPARE(appimage::safeName(u"My App (1).AppImage"_s), u"My_App__1_.AppImage"_s);
        QCOMPARE(appimage::safeName(u"../../.bashrc"_s), u"_.._.bashrc"_s);
        QCOMPARE(appimage::safeName(u"-rf"_s), u"rf"_s);
        QVERIFY(!appimage::safeName(u"a/b"_s).contains(u'/'));
    }

    void fileCheck() {
        QTemporaryDir dir;
        const auto write = [&dir](const char* name, const QByteArray& content) {
            QFile file(dir.filePath(QLatin1StringView(name)));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(content);
            }
            return file.fileName();
        };
        QVERIFY(appimage::isAppImage(write("a", QByteArray("\x7f" "ELF\x02\x01\x01\x00" "AI\x02" "rest", 15))));
        QVERIFY(!appimage::isAppImage(write("b", QByteArray("\x7f" "ELF\x02\x01\x01\x00\x00\x00\x00" "rest", 15))));  // plain program
        QVERIFY(!appimage::isAppImage(write("c", QByteArray("\x7f" "ELF\x02\x01\x01\x00" "AI\x01" "rest", 15))));     // type 1
        QVERIFY(!appimage::isAppImage(write("d", QByteArray("#!/bin/sh\n"))));
        QVERIFY(!appimage::isAppImage(dir.filePath(u"missing"_s)));
    }
};

QTEST_APPLESS_MAIN(TestAppImage)
#include "test_appimage.moc"
