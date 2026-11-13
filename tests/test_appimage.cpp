// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"

#include <QTemporaryDir>
#include <QTest>

class TestAppImage : public QObject {
    Q_OBJECT

 private slots:
    void quoting() {
        QCOMPARE(appimage::quoteExec(QStringLiteral("/home/u/Applications/Editor-1.2.AppImage")),
            QStringLiteral("/home/u/Applications/Editor-1.2.AppImage"));
        QCOMPARE(appimage::quoteExec(QStringLiteral("/home/my user/A.AppImage")), QStringLiteral("\"/home/my user/A.AppImage\""));
        QCOMPARE(appimage::quoteExec(QStringLiteral("/h/a$b`c\"d\\e%f")), QStringLiteral("\"/h/a\\\\$b\\\\`c\\\\\"d\\\\\\\\e%%f\""));
    }

    void program() {
        const auto app = QStringLiteral("/a/App.AppImage");
        QCOMPARE(appimage::replaceProgram(QStringLiteral("editor %U"), app), QStringLiteral("/a/App.AppImage %U"));
        QCOMPARE(appimage::replaceProgram(QStringLiteral("editor"), app), app);
        QCOMPARE(appimage::replaceProgram(QStringLiteral("\"my editor\" --new %f"), app), QStringLiteral("/a/App.AppImage --new %f"));
        QCOMPARE(appimage::replaceProgram(QStringLiteral("env A=1 B=two editor --x"), app), QStringLiteral("/a/App.AppImage --x"));
        QCOMPARE(appimage::replaceProgram(QStringLiteral("  AppRun   %F  "), app), QStringLiteral("/a/App.AppImage %F"));
    }

    void integration() {
        const auto original = QStringLiteral(
            "# written by a tool\r\n"
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
            "Exec=/usr/bin/evil\n");
        const auto entry = appimage::integrate(original, QStringLiteral("/h/Applications/E.AppImage"), QStringLiteral("/h/icons/e.png"));
        QCOMPARE(entry, QStringLiteral(
            "[Desktop Entry]\n"
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
            "Exec=/h/Applications/E.AppImage --new\n"));
        const auto keys = appimage::mainGroup(entry);
        QCOMPARE(keys.value(QStringLiteral("X-CachyOS-PI-AppImage")), QStringLiteral("/h/Applications/E.AppImage"));
        QCOMPARE(keys.value(QStringLiteral("Name")), QStringLiteral("Editor"));
    }

    void unusableEntries() {
        const auto path = QStringLiteral("/a/A.AppImage");
        QVERIFY(appimage::integrate(QStringLiteral("Name=X\nExec=x\n"), path, {}).isEmpty());                    // no group
        QVERIFY(appimage::integrate(QStringLiteral("[Desktop Entry]\nExec=x\n"), path, {}).isEmpty());           // no name
        QVERIFY(appimage::integrate(QStringLiteral("[Desktop Entry]\nName=X\n"), path, {}).isEmpty());           // nothing to start
        QVERIFY(appimage::integrate(QStringLiteral("[Desktop Entry]\nType=Link\nName=X\nExec=x\n"), path, {}).isEmpty());
        // keys of other groups do not count for the main group
        QVERIFY(appimage::integrate(QStringLiteral("[Other]\nName=X\nExec=x\n[Desktop Entry]\nType=Application\n"), path, {}).isEmpty());
        // too large once it is written out
        const auto huge = QStringLiteral("[Desktop Entry]\nName=X\nExec=x\nComment=") + QString(300 * 1024, QLatin1Char('c')) + QLatin1Char('\n');
        QVERIFY(appimage::integrate(huge, path, {}).isEmpty());
    }

    void typeIsAlwaysWritten() {
        // a missing type is supplied, a second one cannot change it
        const auto missing = appimage::integrate(QStringLiteral("[Desktop Entry]\nName=X\nExec=x\nIcon=x\n"), QStringLiteral("/a/A.AppImage"), {});
        QVERIFY(missing.startsWith(QStringLiteral("[Desktop Entry]\nType=Application\n")));
        QVERIFY(missing.contains(QStringLiteral("Icon=x\n")));  // the icon name is kept when no file was found
        const auto twice = appimage::integrate(QStringLiteral("[Desktop Entry]\nType=Application\nType=Link\nName=X\nExec=x\n"), QStringLiteral("/a/A.AppImage"), {});
        QCOMPARE(twice.count(QStringLiteral("Type=")), 1);
        QVERIFY(!twice.contains(QStringLiteral("Link")));
    }

    void commandLines() {
        QCOMPARE(appimage::commandLine(QStringLiteral("/a/App.AppImage --gui %U")), (QStringList{QStringLiteral("/a/App.AppImage"), QStringLiteral("--gui")}));
        QCOMPARE(appimage::commandLine(QStringLiteral("\"/my apps/A.AppImage\" \"two words\" %f")),
            (QStringList{QStringLiteral("/my apps/A.AppImage"), QStringLiteral("two words")}));
        QCOMPARE(appimage::commandLine(QStringLiteral("app --rate=50%% --name=%c")), (QStringList{QStringLiteral("app"), QStringLiteral("--rate=50%"), QStringLiteral("--name=")}));
        // what quoteExec() writes comes back unchanged
        const auto odd = QStringLiteral("/h/a$b`c\"d\\e%f g");
        QCOMPARE(appimage::commandLine(appimage::quoteExec(odd) + QStringLiteral(" %U")), QStringList{odd});
        QVERIFY(appimage::commandLine(QString()).isEmpty());
    }

    void desktopNames() {
        QCOMPARE(appimage::safeDesktopName(QStringLiteral("org.example.App.desktop")), QStringLiteral("org.example.App.desktop"));
        QCOMPARE(appimage::safeDesktopName(QStringLiteral("x")), QStringLiteral("x.desktop"));
        QCOMPARE(appimage::safeDesktopName(QStringLiteral(".desktop")), QStringLiteral("app.desktop"));
        const auto longName = appimage::safeDesktopName(QString(300, QLatin1Char('a')) + QStringLiteral(".desktop"));
        QVERIFY(longName.endsWith(QStringLiteral(".desktop")));
        QCOMPARE(longName.size(), 108);
    }

    void managedPaths() {
        const auto dir = QStringLiteral("/home/u/Applications");
        QVERIFY(appimage::isManagedPath(dir + QStringLiteral("/Editor-1.2.AppImage"), dir));
        QVERIFY(!appimage::isManagedPath(dir + QStringLiteral("/../Documents/important"), dir));
        QVERIFY(!appimage::isManagedPath(dir + QStringLiteral("/sub/Editor.AppImage"), dir));
        QVERIFY(!appimage::isManagedPath(dir + QStringLiteral("/"), dir));
        QVERIFY(!appimage::isManagedPath(dir + QStringLiteral("/.."), dir));
        QVERIFY(!appimage::isManagedPath(dir + QStringLiteral("/my file.AppImage"), dir));
        QVERIFY(!appimage::isManagedPath(QStringLiteral("/home/u/ApplicationsX/a.AppImage"), dir));
        QVERIFY(!appimage::isManagedPath(QStringLiteral("/etc/passwd"), dir));
    }

    void names() {
        QCOMPARE(appimage::safeName(QStringLiteral("My App (1).AppImage")), QStringLiteral("My_App__1_.AppImage"));
        QCOMPARE(appimage::safeName(QStringLiteral("../../.bashrc")), QStringLiteral("_.._.bashrc"));
        QCOMPARE(appimage::safeName(QStringLiteral("-rf")), QStringLiteral("rf"));
        QVERIFY(!appimage::safeName(QStringLiteral("a/b")).contains(QLatin1Char('/')));
    }

    void fileCheck() {
        QTemporaryDir dir;
        const auto write = [&dir](const char* name, const QByteArray& content) {
            QFile file(dir.filePath(QLatin1String(name)));
            file.open(QIODevice::WriteOnly);
            file.write(content);
            return file.fileName();
        };
        QVERIFY(appimage::isAppImage(write("a", QByteArray("\x7f" "ELF\x02\x01\x01\x00" "AI\x02" "rest", 15))));
        QVERIFY(!appimage::isAppImage(write("b", QByteArray("\x7f" "ELF\x02\x01\x01\x00\x00\x00\x00" "rest", 15))));  // plain program
        QVERIFY(!appimage::isAppImage(write("c", QByteArray("\x7f" "ELF\x02\x01\x01\x00" "AI\x01" "rest", 15))));     // type 1
        QVERIFY(!appimage::isAppImage(write("d", QByteArray("#!/bin/sh\n"))));
        QVERIFY(!appimage::isAppImage(dir.filePath(QStringLiteral("missing"))));
    }
};

QTEST_APPLESS_MAIN(TestAppImage)
#include "test_appimage.moc"
