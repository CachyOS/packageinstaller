// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"
#include "appimagesource.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject file(qint64 id, const QString& name, qint64 size = 1000) {
    return {{u"id"_s, id}, {u"name"_s, name}, {u"size"_s, size},
        {u"browser_download_url"_s, u"https://github.com/o/r/releases/download/v1/"_s + name}};
}

QByteArray releaseJson(const QString& tag, const QJsonArray& assets) {
    return QJsonDocument(QJsonObject{{u"tag_name"_s, tag}, {u"assets"_s, assets}}).toJson();
}

}  // namespace

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

    void tracking() {
        const auto original = u"[Desktop Entry]\nName=Editor\nExec=editor\nX-CachyOS-PI-Source=github:evil/evil\n"_s;
        const auto plain    = appimage::mainGroup(appimage::integrate(original, u"/a/E.AppImage"_s, {}));
        // what the file says about itself is not taken over
        QVERIFY(appimage::tracking(plain).source.isEmpty());

        const appimage::Tracking wanted{.source = u"github:o/r"_s, .asset = 42, .variant = u"e-#-x#.appimage"_s};
        const auto stored = appimage::tracking(appimage::mainGroup(appimage::integrate(original, u"/a/E.AppImage"_s, {}, wanted)));
        QCOMPARE(stored.source, wanted.source);
        QCOMPARE(stored.asset, wanted.asset);
        QCOMPARE(stored.variant, wanted.variant);
        QVERIFY(appimage::integrate(original, u"/a/E.AppImage"_s, {}, {.source = u"github:o/r\nExec=x"_s, .asset = 1, .variant = {}}).isEmpty());
    }

    void sources_data() {
        QTest::addColumn<QString>("link");
        QTest::addColumn<QString>("slug");
        QTest::addColumn<QString>("file");
        QTest::newRow("page") << u"https://github.com/owner/repo"_s << u"owner/repo"_s << QString();
        QTest::newRow("no scheme") << u" github.com/owner/repo/ "_s << u"owner/repo"_s << QString();
        QTest::newRow("bare") << u"owner/my.repo"_s << u"owner/my.repo"_s << QString();
        QTest::newRow("git") << u"https://github.com/owner/repo.git"_s << u"owner/repo"_s << QString();
        QTest::newRow("releases") << u"https://www.github.com/owner/repo/releases/tag/v1?x=1#top"_s << u"owner/repo"_s << QString();
        QTest::newRow("file") << u"https://github.com/owner/repo/releases/download/v1.2/App-1.2-x86_64.AppImage"_s << u"owner/repo"_s
                              << u"App-1.2-x86_64.AppImage"_s;
        QTest::newRow("other host") << u"https://gitlab.com/owner/repo"_s << QString() << QString();
        QTest::newRow("look-alike") << u"https://github.com.evil.example/owner/repo"_s << QString() << QString();
        QTest::newRow("user info") << u"https://github.com@evil.example/owner/repo"_s << QString() << QString();
        QTest::newRow("owner only") << u"https://github.com/owner"_s << QString() << QString();
        QTest::newRow("dots") << u"https://github.com/owner/.."_s << QString() << QString();
        QTest::newRow("odd owner") << u"https://github.com/ow%20ner/repo"_s << QString() << QString();
        QTest::newRow("file path") << u"/home/u/App.AppImage"_s << QString() << QString();
        QTest::newRow("empty") << QString() << QString() << QString();
    }

    void sources() {
        QFETCH(QString, link);
        QFETCH(QString, slug);
        QFETCH(QString, file);
        const auto source = appimage::parseSource(link);
        QCOMPARE(source.has_value(), !slug.isEmpty());
        if (source) {
            QCOMPARE(source->slug(), slug);
            QCOMPARE(source->file, file);
            QCOMPARE(appimage::latestReleaseUrl(*source).toString(), u"https://api.github.com/repos/"_s + slug + u"/releases/latest"_s);
            const auto back = appimage::sourceFromValue(appimage::sourceValue(*source));
            QVERIFY(back.has_value());
            QCOMPARE(back->slug(), slug);
        }
    }

    void sourceValues() {
        QVERIFY(!appimage::sourceFromValue(u"owner/repo"_s).has_value());
        QVERIFY(!appimage::sourceFromValue(u"github:owner"_s).has_value());
        QVERIFY(!appimage::sourceFromValue(u"github:owner/repo/more"_s).has_value());
        QVERIFY(!appimage::sourceFromValue(u"github:owner/../x"_s).has_value());
        QVERIFY(!appimage::sourceFromValue(QString()).has_value());
    }

    void releases() {
        auto hashed = file(7, u"App-1.2-x86_64.AppImage"_s, 2000);
        hashed.insert(u"digest"_s, u"sha256:"_s + QString(64, u'a'));
        auto elsewhere = file(8, u"Other-x86_64.AppImage"_s);
        elsewhere.insert(u"browser_download_url"_s, u"https://evil.example/o/r/releases/download/v1/Other-x86_64.AppImage"_s);
        auto plainHttp = file(9, u"Http-x86_64.AppImage"_s);
        plainHttp.insert(u"browser_download_url"_s, u"http://github.com/o/r/releases/download/v1/Http-x86_64.AppImage"_s);
        auto badDigest = file(10, u"App-1.2-lite-x86_64.AppImage"_s);
        badDigest.insert(u"digest"_s, u"sha256:abcd"_s);
        auto oddDigest = file(19, u"App-1.2-odd-x86_64.AppImage"_s);
        oddDigest.insert(u"digest"_s, u"sha256:"_s + QString(62, u'a') + u"zz"_s);
        auto otherDigest = file(20, u"App-1.2-md-x86_64.AppImage"_s);
        otherDigest.insert(u"digest"_s, u"md5:"_s + QString(32, u'a'));
        auto noDigest = file(21, u"App-1.2-plain-x86_64.AppImage"_s);
        noDigest.insert(u"digest"_s, QJsonValue());
        const auto release = appimage::parseRelease(releaseJson(u" v1.2 "_s, {
            hashed, elsewhere, plainHttp, badDigest, oddDigest, otherDigest, noDigest,
            file(22, u"App-1.2-arm.AppImage"_s), file(23, u"App-1.2-x86.AppImage"_s), file(24, u"App-linux-armel.AppImage"_s),
            file(11, u"App-1.2-aarch64.AppImage"_s), file(12, u"App-1.2-i686.AppImage"_s), file(13, u"App-1.2-arm64.appimage"_s),
            file(14, u"App-1.2-x86_64.AppImage.zsync"_s), file(15, u"app.tar.gz"_s), file(16, u"My App.AppImage"_s),
            file(17, u"Huge.AppImage"_s, appimage::MAX_DOWNLOAD_BYTES + 1), file(18, u"Empty.AppImage"_s, 0),
            file(0, u"NoId.AppImage"_s), QJsonValue(u"text"_s)}));
        QVERIFY(release.has_value());
        QCOMPARE(release->tag, u"v1.2"_s);
        QCOMPARE(release->assets.size(), 2);
        QCOMPARE(release->assets.at(0).id, 7);
        QCOMPARE(release->assets.at(0).name, u"App-1.2-x86_64.AppImage"_s);
        QCOMPARE(release->assets.at(0).size, 2000);
        QCOMPARE(release->assets.at(0).sha256, QByteArray(32, '\xaa'));
        QCOMPARE(release->assets.at(0).url.toString(), u"https://github.com/o/r/releases/download/v1/App-1.2-x86_64.AppImage"_s);
        // a checksum that cannot be used takes its file out; none at all is how older uploads are
        QCOMPARE(release->assets.at(1).id, 21);
        QVERIFY(release->assets.at(1).sha256.isEmpty());

        const auto named = appimage::parseRelease(releaseJson(u"v1"_s, {file(1, u"Charm-x86_64.AppImage"_s),
            file(2, u"App-x86-64.AppImage"_s), file(3, u"App.AppImage"_s)}));
        QCOMPARE(named->assets.size(), 3);

        const auto bare = appimage::parseRelease(releaseJson(u"v2"_s, {}));
        QVERIFY(bare.has_value());
        QVERIFY(bare->assets.isEmpty());
        QVERIFY(!appimage::parseRelease("{\"message\":\"Not Found\"}").has_value());
        QVERIFY(!appimage::parseRelease("[]").has_value());
        QVERIFY(!appimage::parseRelease("<html>").has_value());
        QVERIFY(!appimage::parseRelease(QByteArray()).has_value());
    }

    void variants() {
        QCOMPARE(appimage::variant(u"App-1.2.3-x86_64.AppImage"_s), appimage::variant(u"App-1.10-x86_64.AppImage"_s));
        QCOMPARE(appimage::variant(u"App-2024.05.01-x86_64.AppImage"_s), appimage::variant(u"app-7-x86_64.appimage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-lite-x86_64.AppImage"_s));
        QCOMPARE(appimage::variant(u"App_v1.2.3_amd64.AppImage"_s), appimage::variant(u"App_v2_amd64.AppImage"_s));
        QCOMPARE(appimage::variant(u"1.2-App.AppImage"_s), appimage::variant(u"3-App.AppImage"_s));
        // numbers that are no version stay
        QVERIFY(appimage::variant(u"App-1.2-qt5-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-qt6-x86_64.AppImage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64-v2.AppImage"_s) != appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s));
        QCOMPARE(appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s), appimage::variant(u"App-1.3-x86_64-v3.AppImage"_s));

        const auto assets = appimage::parseRelease(releaseJson(u"v2"_s, {file(1, u"App-2.0-x86_64.AppImage"_s),
            file(2, u"App-2.0-lite-x86_64.AppImage"_s)}))->assets;
        QCOMPARE(appimage::choose(assets, appimage::variant(u"App-1.9-lite-x86_64.AppImage"_s)), 1);
        QCOMPARE(appimage::choose(assets, appimage::variant(u"App-1.9-x86_64.AppImage"_s)), 0);
        // renamed in the new release: not clear which of two it is
        QCOMPARE(appimage::choose(assets, u"other-#.appimage"_s), -1);
        QCOMPARE(appimage::choose(assets, QString()), -1);
        // a single file is the one, whatever it is called now
        QCOMPARE(appimage::choose(assets.mid(0, 1), u"other-#.appimage"_s), 0);
        QCOMPARE(appimage::choose({}, u"other-#.appimage"_s), -1);
    }
};

QTEST_APPLESS_MAIN(TestAppImage)
#include "test_appimage.moc"
