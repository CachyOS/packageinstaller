// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"
#include "appimageimport.hpp"
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

        const appimage::Tracking wanted{.source = u"github:o/r"_s, .asset = 42, .variant = u"e-#-x#.appimage"_s, .release = {}};
        const auto stored = appimage::tracking(appimage::mainGroup(appimage::integrate(original, u"/a/E.AppImage"_s, {}, wanted)));
        QCOMPARE(stored.source, wanted.source);
        QCOMPARE(stored.asset, wanted.asset);
        QCOMPARE(stored.variant, wanted.variant);
        QVERIFY(appimage::integrate(original, u"/a/E.AppImage"_s, {}, {.source = u"github:o/r\nExec=x"_s, .asset = 1, .variant = {}, .release = {}}).isEmpty());
    }

    void releaseOfATakenOverFile() {
        const auto original = u"[Desktop Entry]\nName=Editor\nExec=editor\n"_s;
        const appimage::Tracking wanted{.source = u"github:o/r"_s, .asset = 0, .variant = u"e.appimage"_s, .release = u"v1.2"_s};
        const auto stored = appimage::tracking(appimage::mainGroup(appimage::integrate(original, u"/a/E.AppImage"_s, {}, wanted)));
        QCOMPARE(stored.release, wanted.release);
        QCOMPARE(stored.asset, 0);
        auto broken    = wanted;
        broken.release = u"v1\nExec=x"_s;
        QVERIFY(appimage::integrate(original, u"/a/E.AppImage"_s, {}, broken).isEmpty());
    }

    void shellyDatabase() {
        // as Shelly 3 writes it
        const QByteArray database = R"([
          {"name": "Editor-1.2-x86_64", "version": "1.2", "release_tag": "v1.2", "raw_update_info": "",
           "icon_name": "editor", "description": "Edits", "desktop_name": "Editor", "size_on_disk": 123,
           "environment_variables": [{"key": "A", "value": "1"}], "command_line_args": "",
           "path": "/home/u/.local/bin/Editor-1.2-x86_64.AppImage", "update_url": "", "update_type": "github",
           "repo_owner": "owner", "repo_name": "editor", "allow_prerelease": false},
          {"name": "Plain", "version": "Unknown", "update_type": "none", "repo_owner": null, "repo_name": null},
          {"name": "Numbered", "path": "/opt/n.AppImage", "version": "1.18.2", "release_tag": null, "update_type": 2, "repo_owner": "o", "repo_name": "n"},
          {"name": "Elsewhere", "path": "/opt/e.AppImage", "update_type": "gitlab", "repo_owner": "o", "repo_name": "e"},
          {"name": "Odd", "path": "/opt/o.AppImage", "update_type": "github", "repo_owner": "o/x", "repo_name": "../e"},
          {"name": "LocalSend", "path": "/opt/LocalSend-1.17.0.AppImage", "update_type": "github", "repo_owner": "localsend",
           "repo_name": "localsend", "raw_update_info": "gh-releases-zsync|LocalSend|localsend|latest|LocalSend-*-linux-x86-64.AppImage.zsync"},
          {"name": "Borrowed", "path": "/opt/b.AppImage", "update_type": "github", "repo_owner": "o", "repo_name": "b",
           "raw_update_info": "gh-releases-zsync|someone|else|latest|B-*-x86_64.AppImage"},
          {"name": "Relative", "path": "bin/r.AppImage"},
          {"path": "/opt/nameless.AppImage"},
          {"name": "../../.config/autostart/x"},
          {"name": ".."},
          "text", 7
        ])";
        const auto apps = appimage::parseShellyDatabase(database, u"/home/u/.local/bin"_s);
        QCOMPARE(apps.size(), 7);
        QCOMPARE(apps.at(0).name, u"Editor-1.2-x86_64"_s);
        QCOMPARE(apps.at(0).title, u"Editor"_s);
        QCOMPARE(apps.at(0).path, u"/home/u/.local/bin/Editor-1.2-x86_64.AppImage"_s);
        QCOMPARE(apps.at(0).icon, u"editor"_s);
        QCOMPARE(apps.at(0).release, u"v1.2"_s);
        QCOMPARE(apps.at(0).source->slug(), u"owner/editor"_s);
        QCOMPARE(appimage::shellyIconFiles(apps.at(0)),
            QStringList({u"icons/hicolor/scalable/apps/editor.svg"_s, u"icons/hicolor/256x256/apps/editor.png"_s}));
        QVERIFY(appimage::shellyIconFiles({.name = u"x"_s, .title = {}, .path = {}, .icon = u"../x"_s, .source = {}, .release = {}, .variant = {}}).isEmpty());
        QVERIFY(appimage::shellyIconFiles(apps.at(1)).isEmpty());
        // no path: where Shelly would look for it
        QCOMPARE(apps.at(1).title, u"Plain"_s);
        QVERIFY(apps.at(1).release.isEmpty());
        QCOMPARE(apps.at(1).path, u"/home/u/.local/bin/Plain.AppImage"_s);
        QVERIFY(!apps.at(1).source);
        QCOMPARE(apps.at(2).source->slug(), u"o/n"_s);
        QCOMPARE(apps.at(2).release, u"1.18.2"_s);
        QVERIFY(!apps.at(3).source);  // only GitHub is followed here
        QVERIFY(!apps.at(4).source);  // not a repository
        // the name of a file Shelly was given, else the pattern of the update information
        QCOMPARE(apps.at(0).variant, appimage::variant(u"Editor-2.0-x86_64.AppImage"_s));
        QCOMPARE(apps.at(5).variant, appimage::variant(u"LocalSend-1.18.2-linux-x86-64.AppImage"_s));
        QCOMPARE(apps.at(6).variant, u"b.appimage"_s);  // the information of another repository
        QVERIFY(apps.at(1).variant.isEmpty());          // not updated from GitHub

        QVERIFY(appimage::parseShellyDatabase("not json", u"/b"_s).isEmpty());
        QVERIFY(appimage::parseShellyDatabase("{}", u"/b"_s).isEmpty());
        QVERIFY(appimage::parseShellyDatabase(R"([{"name": "Plain"}])", {}).isEmpty());
    }

    void shellyDatabaseWithoutSomeApps() {
        const QByteArray database = R"([{"name": "A", "path": "/a", "size_on_disk": 9007199254740993, "extra": {"k": [1, 2]}},
                                        {"name": "B", "path": "/b"}, {"name": "C", "path": "/c"}])";
        const auto rest = appimage::withoutShellyApps(database, {u"B"_s, u"missing"_s});
        const auto left = QJsonDocument::fromJson(rest).array();
        QCOMPARE(left.size(), 2);
        // what this app does not know about is kept as it is
        QCOMPARE(left.at(0).toObject().value("extra"_L1).toObject().value("k"_L1).toArray().size(), 2);
        QCOMPARE(left.at(0).toObject().value("size_on_disk"_L1).toInteger(), 9007199254740993);
        QCOMPARE(left.at(1).toObject().value("name"_L1).toString(), u"C"_s);
        QCOMPARE(QJsonDocument::fromJson(appimage::withoutShellyApps(database, {u"A"_s, u"B"_s, u"C"_s})).array().size(), 0);
        QVERIFY(appimage::withoutShellyApps("not json", {u"A"_s}).isEmpty());
        QVERIFY(appimage::withoutShellyApps("{}", {u"A"_s}).isEmpty());
    }

    void sameReleases() {
        QVERIFY(appimage::sameRelease(u"1.18.2", u"v1.18.2"));
        QVERIFY(appimage::sameRelease(u"v2", u"v2"));
        QVERIFY(!appimage::sameRelease(u"1.17.0", u"v1.18.2"));
        QVERIFY(!appimage::sameRelease(u"", u""));
        QVERIFY(!appimage::sameRelease(u"", u"v"));
    }

    void shellyFolder() {
        QCOMPARE(appimage::shellyInstallDirectory(R"({"AppImageInstallPath": "/opt/appimages"})"), u"/opt/appimages"_s);
        QVERIFY(appimage::shellyInstallDirectory(R"({"AppImageInstallPath": null})").isEmpty());
        QVERIFY(appimage::shellyInstallDirectory("").isEmpty());
    }

    void gearLeverConfig() {
        // as Gear Lever 4.6 writes it, with the empty sections it leaves for files it only looked at
        const auto config = QByteArray(R"([DEFAULT]
fetch-updates-in-background = no

[app.de1cf8ffa34d272d3e7590b01dbee6dd.update_manager]

[app.d13b5beea46f0d7af318a54855af511e]
default_exec_arguments =
name = LocalSend
file_path = /home/u/AppImages/localsend.appimage

[app.f5fec74c5458597804beeb85e687bde6]
default_exec_arguments = %F
name = Neovim
file_path = /home/u/AppImages/neovim.appimage
website: https://neovim.io

[app.d13b5beea46f0d7af318a54855af511e.update_manager]
repo = localsend/localsend
repo_filename = LocalSend-*-linux-x86-64.AppImage
allow_prereleases = False
manager = GithubUpdater

# a comment
[app.0123456789abcdef0123456789abcdef]
name = Relative
file_path = AppImages/relative.appimage

[app.fedcba9876543210fedcba9876543210]
name = GitLab
FILE_PATH = /home/u/AppImages/gitlab.appimage
  continued = not a key

[app.fedcba9876543210fedcba9876543210.update_manager]
repo = owner/project
manager = GitlabUpdater

[app.00000000000000000000000000000001]
file_path = /home/u/AppImages/nightly.appimage

[app.00000000000000000000000000000001.update_manager]
repo = owner/nightly
repo_filename = Nightly-*.AppImage
allow_prereleases = True
manager = GithubUpdater
)");
        const auto apps = appimage::parseGearLeverConfig(config);
        QCOMPARE(apps.size(), 4);
        QCOMPARE(apps.at(0).path, u"/home/u/AppImages/localsend.appimage"_s);
        QVERIFY(apps.at(0).source);
        QCOMPARE(apps.at(0).source->slug(), u"localsend/localsend"_s);
        QVERIFY(apps.at(0).source->file.isEmpty());
        QCOMPARE(apps.at(0).variant, appimage::variant(u"LocalSend-1.18.2-linux-x86-64.AppImage"_s));
        // updates from the file itself or from elsewhere than GitHub are not carried over
        QCOMPARE(apps.at(1).path, u"/home/u/AppImages/neovim.appimage"_s);
        QVERIFY(!apps.at(1).source);
        QCOMPARE(apps.at(2).path, u"/home/u/AppImages/gitlab.appimage"_s);
        QVERIFY(!apps.at(2).source);
        // the newest release could be older than the pre-release it is on
        QCOMPARE(apps.at(3).path, u"/home/u/AppImages/nightly.appimage"_s);
        QVERIFY(!apps.at(3).source);

        QVERIFY(appimage::parseGearLeverConfig("").isEmpty());
        QVERIFY(appimage::parseGearLeverConfig("not an ini file\n[app.x]\nfile_path = /a\n").isEmpty());
    }

    void gearLeverEntries() {
        const auto path = u"/home/u/AppImages/localsend.appimage"_s;
        QHash<QString, QString> entry{{u"TryExec"_s, path}, {u"Icon"_s, u"/home/u/AppImages/.icons/localsend"_s},
            {u"X-AppImage-Name"_s, u"LocalSend"_s}, {u"Exec"_s, u"env DESKTOPINTEGRATION=1 "_s + path}};
        QCOMPARE(appimage::gearLeverIcon(entry, path), u"/home/u/AppImages/.icons/localsend"_s);
        QVERIFY(appimage::gearLeverIcon(entry, u"/home/u/AppImages/other.appimage"_s).isEmpty());
        // named after the app, in whatever letters it has
        auto named = entry;
        for (const auto& icon : {u"/home/u/AppImages/.icons/äpfel_(beta).png"_s, u"/home/u/AppImages/.icons/録画.svg"_s}) {
            named[u"Icon"_s] = icon;
            QCOMPARE(appimage::gearLeverIcon(named, path), icon);
        }

        // not copied by Gear Lever: no icon of its own next to the file
        auto other = entry;
        other[u"Icon"_s] = u"/usr/share/icons/hicolor/48x48/apps/app.png"_s;
        QVERIFY(appimage::gearLeverIcon(other, path).isEmpty());
        other[u"Icon"_s] = u"applications-other"_s;
        QVERIFY(appimage::gearLeverIcon(other, path).isEmpty());
        for (const auto& icon : {u"/home/u/AppImages/.icons/"_s, u"/home/u/AppImages/.icons/../x"_s, u"/home/u/AppImages/.icons/a/b"_s}) {
            other[u"Icon"_s] = icon;
            QVERIFY2(appimage::gearLeverIcon(other, path).isEmpty(), qPrintable(icon));
        }
        // written by someone else, or by this app
        other = entry;
        other.remove(u"X-AppImage-Name"_s);
        QVERIFY(appimage::gearLeverIcon(other, path).isEmpty());
        other = entry;
        other.insert(appimage::PATH_KEY, path);
        QVERIFY(appimage::gearLeverIcon(other, path).isEmpty());
    }

    void patternVariants() {
        const auto release = appimage::variant(u"App-2.0-x86_64.AppImage"_s);
        QCOMPARE(appimage::patternVariant(u"App-*-x86_64.AppImage"_s), release);
        QCOMPARE(appimage::patternVariant(u"App-v*-x86_64.AppImage"_s), appimage::variant(u"App-v2.0-x86_64.AppImage"_s));
        QCOMPARE(appimage::patternVariant(u"App-2.0-x86_64.AppImage"_s), release);
        QCOMPARE(appimage::patternVariant(u"App-*-x86_64.AppImage.zsync"_s), release);
        QVERIFY(appimage::patternVariant(u"App-*-lite-x86_64.AppImage"_s) != release);
        // the name of the app, not a version
        QVERIFY(appimage::patternVariant(u"*-x86_64.AppImage.zsync"_s).isEmpty());
        // which file it is cannot be told
        QVERIFY(appimage::patternVariant(u"App*.AppImage"_s).isEmpty());
        QVERIFY(appimage::patternVariant(u"App-*x86_64.AppImage"_s).isEmpty());
        QVERIFY(appimage::patternVariant(u"App-?.AppImage"_s).isEmpty());
        QVERIFY(appimage::patternVariant(u"App-[0-9].AppImage"_s).isEmpty());
        QVERIFY(appimage::patternVariant(u"*.AppImage"_s).isEmpty());
        QVERIFY(appimage::patternVariant(u"*"_s).isEmpty());
        QVERIFY(appimage::patternVariant({}).isEmpty());
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

    void releaseOrder_data() {
        QTest::addColumn<QString>("installed");
        QTest::addColumn<QString>("offered");
        QTest::addColumn<int>("order");
        QTest::newRow("same, with and without v") << u"1.2.0"_s << u"v1.2"_s << 0;
        QTest::newRow("capital V") << u"V1.2"_s << u"1.2"_s << 0;
        QTest::newRow("numbers, not text") << u"v1.9"_s << u"v1.10"_s << -1;
        QTest::newRow("moved back") << u"2.0"_s << u"1.9.9"_s << 1;
        QTest::newRow("rolling") << u"nightly"_s << u"nightly"_s << 2;
        QTest::newRow("pre-release") << u"1.2-rc1"_s << u"1.2"_s << 2;
        QTest::newRow("nothing remembered") << QString() << u"1.2"_s << 2;
    }

    void releaseOrder() {
        QFETCH(QString, installed);
        QFETCH(QString, offered);
        QFETCH(int, order);
        const auto result = appimage::compareReleases(installed, offered);
        QCOMPARE(result ? (std::is_lt(*result) ? -1 : std::is_gt(*result) ? 1 : 0) : 2, order);
        QCOMPARE(appimage::sameRelease(installed, offered), order == 0 || installed == u"nightly");
    }

    void variants() {
        QCOMPARE(appimage::variant(u"App-1.2.3-x86_64.AppImage"_s), appimage::variant(u"App-1.10-x86_64.AppImage"_s));
        QCOMPARE(appimage::variant(u"App-2024.05.01-x86_64.AppImage"_s), appimage::variant(u"app-7-x86_64.appimage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-lite-x86_64.AppImage"_s));
        QCOMPARE(appimage::variant(u"App_v1.2.3_amd64.AppImage"_s), appimage::variant(u"App_v2_amd64.AppImage"_s));
        QCOMPARE(appimage::variant(u"1.2-App.AppImage"_s), appimage::variant(u"3-App.AppImage"_s));

        QVERIFY(appimage::variant(u"App-1.2-qt5-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-qt6-x86_64.AppImage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s));
        QVERIFY(appimage::variant(u"App-1.2-x86_64-v2.AppImage"_s) != appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s));
        QCOMPARE(appimage::variant(u"App-1.2-x86_64-v3.AppImage"_s), appimage::variant(u"App-1.3-x86_64-v3.AppImage"_s));

        QCOMPARE(appimage::variant(u"App-1.2.3rc1-x86_64.AppImage"_s), appimage::variant(u"App-1.2.4-x86_64.AppImage"_s));
        QCOMPARE(appimage::variant(u"App-2.0.0-beta.1-x86_64.AppImage"_s), appimage::variant(u"App-2.0.0-x86_64.AppImage"_s));
        QVERIFY(appimage::variant(u"App-1.2-beta-x86_64.AppImage"_s) != appimage::variant(u"App-1.2-x86_64.AppImage"_s));

        const auto assets = appimage::parseRelease(releaseJson(u"v2"_s, {file(1, u"App-2.0-x86_64.AppImage"_s),
            file(2, u"App-2.0-lite-x86_64.AppImage"_s)}))->assets;
        QCOMPARE(appimage::choose(assets, appimage::variant(u"App-1.9-lite-x86_64.AppImage"_s)), 1);
        QCOMPARE(appimage::choose(assets, appimage::variant(u"App-1.9-x86_64.AppImage"_s)), 0);
        // renamed in the new release: not clear which of two it is
        QCOMPARE(appimage::choose(assets, u"other-#.appimage"_s), -1);
        QCOMPARE(appimage::choose(assets, QString()), -1);

        QCOMPARE(appimage::choose(assets.mid(0, 1), u"other-#.appimage"_s), -1);
        QCOMPARE(appimage::choose(assets.mid(0, 1), QString()), 0);
        QCOMPARE(appimage::choose({}, u"other-#.appimage"_s), -1);

        // a kind another program's records suggest: the only file there is continues it too
        QCOMPARE(appimage::choose(assets, appimage::variant(u"App-1.9-lite-x86_64.AppImage"_s), true), 1);
        QCOMPARE(appimage::choose(assets, u"other-#.appimage"_s, true), -1);
        QCOMPARE(appimage::choose({}, u"other-#.appimage"_s, true), -1);
        // as for a file Shelly downloaded and named after the app
        const auto localsend = appimage::parseRelease(releaseJson(u"v1.18.2"_s, {file(3, u"LocalSend-1.18.2-linux-x86-64.AppImage"_s)}))->assets;
        for (const auto& name : {u"LocalSend.AppImage"_s, u"LocalSend-v1.17.0.AppImage"_s}) {
            QCOMPARE(appimage::choose(localsend, appimage::variant(name)), -1);
            QCOMPARE(appimage::choose(localsend, appimage::variant(name), true), 0);
        }
    }
};

QTEST_APPLESS_MAIN(TestAppImage)
#include "test_appimage.moc"
