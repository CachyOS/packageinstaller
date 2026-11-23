// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpakclient.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestCpak : public QObject {
    Q_OBJECT

 private slots:
    void catalog() {
        QTemporaryDir icons;
        const auto json   = QByteArrayLiteral(R"({"schema":1,"release":"v2","packages":[
            {"origin":"github.com/a/one","name":"One","description":"First app","icon_svg":"<svg/>",
             "permissions":[{"name":"Network","detail":"internet"},{"name":"GPU"}],
             "available_version":"1.2","installed":true,"installed_version":"1.0","installable":true},
            {"origin":"github.com/a/two","available_version":"3","installed":false,"installable":false,"icon_png":"iVBORw0="},
            {"name":"No origin"}]})");
        const auto result = cpak::parseCatalog(json, icons.path());
        QCOMPARE(result.items.size(), 2);
        const auto& one = result.items.at(0);
        QCOMPARE(one.title, u"One"_s);
        QCOMPARE(one.version, u"1.2"_s);
        QCOMPARE(one.installedVersion, u"1.0"_s);
        QVERIFY(one.installed && one.installable && one.available);
        QCOMPARE(one.permissions, (QStringList{u"Network: internet"_s, u"GPU"_s}));
        QVERIFY(one.haystack.contains(u"first app"_s));
        QVERIFY(QFile::exists(result.icons.value(u"github.com/a/one"_s).path));
        // no name: the repository stands in; a PNG icon is stored decoded
        QCOMPARE(result.items.at(1).title, u"two"_s);
        QVERIFY(result.icons.value(u"github.com/a/two"_s).path.endsWith(u".png"_s));
        QVERIFY(!result.items.at(1).installable);
    }

    void notTheCatalog() {
        QVERIFY(cpak::parseCatalog("", {}).items.isEmpty());
        QVERIFY(cpak::parseCatalog("[]", {}).items.isEmpty());
        QVERIFY(cpak::parseCatalog("Error: no network", {}).items.isEmpty());
    }

    void updates() {
        const auto found = cpak::parseUpdates(R"([
            {"origin":"github.com/a/one","status":"available","old_version":"1","new_version":"2"},
            {"origin":"github.com/a/two","status":"pinned","new_version":"2"},
            {"origin":"github.com/a/three","status":"failed"}])");
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.value(u"github.com/a/one"_s), u"2"_s);
        QVERIFY(cpak::parseUpdates("").isEmpty());
    }

    void installed() {
        const auto found = cpak::parseInstalled(R"([
            {"origin":"github.com/a/one","name":"One","version":"main"},
            {"origin":"flatpak/flathub/pm.mirko.atoms","name":"pm.mirko.Atoms","version":"stable"},
            {"origin":"flatpak/flathub/org.gnome.platform","name":"org.gnome.Platform","version":"50",
             "pulled_in":true,"pulled_in_by":"flatpak/flathub/pm.mirko.atoms"},
            {"name":"no origin"}])");
        QCOMPARE(found.size(), 2);
        QVERIFY(found.at(0).flatpakId.isEmpty());
        QCOMPARE(found.at(1).flatpakId, u"pm.mirko.Atoms"_s);
        QCOMPARE(found.at(1).installedVersion, u"stable"_s);
        QVERIFY(found.at(1).installed && !found.at(1).available);

        // the catalog's own entry stays as it is; what it does not list is added
        cpak::Catalog catalog;
        CpakItem one;
        one.origin    = u"github.com/a/one"_s;
        one.available = true;
        catalog.items << one;
        cpak::mergeInstalled(catalog, found);
        QCOMPARE(catalog.items.size(), 2);
        QVERIFY(catalog.items.at(0).available);
        QCOMPARE(catalog.items.at(1).origin, u"flatpak/flathub/pm.mirko.atoms"_s);
        QVERIFY(cpak::parseInstalled("not json").isEmpty());
    }

    void pastedQueries_data() {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("searched");
        QTest::newRow("command") << u"cpak install github.com/atomsdevs/atoms"_s << u"github.com/atomsdevs/atoms"_s;
        QTest::newRow("spaces and case") << u"  CPAK   Install   github.com/a/b  "_s << u"github.com/a/b"_s;
        QTest::newRow("flags") << u"cpak install --yes github.com/a/b"_s << u"github.com/a/b"_s;
        QTest::newRow("flag with value") << u"cpak install --branch main github.com/a/b"_s << u"github.com/a/b"_s;
        QTest::newRow("run with arguments") << u"cpak run github.com/a/b app ./file"_s << u"github.com/a/b"_s;
        QTest::newRow("web address") << u"https://github.com/a/b"_s << u"github.com/a/b"_s;
        QTest::newRow("trailing slash") << u"https://www.github.com/a/b/"_s << u"github.com/a/b"_s;
        QTest::newRow("clone address") << u"https://github.com/a/b.git"_s << u"github.com/a/b"_s;
        QTest::newRow("ssh address") << u"git@github.com:a/b.git"_s << u"github.com/a/b"_s;
        QTest::newRow("bare repository") << u"github.com/a/b"_s << u"github.com/a/b"_s;
        // ordinary searches are not touched
        QTest::newRow("name") << u"bottles"_s << u"bottles"_s;
        QTest::newRow("two words") << u"windows wine"_s << u"windows wine"_s;
        QTest::newRow("the word cpak") << u"cpak"_s << u"cpak"_s;
        QTest::newRow("cpak with a word") << u"cpak sandbox"_s << u"cpak sandbox"_s;
        QTest::newRow("name with a dot") << u"node.js"_s << u"node.js"_s;
        QTest::newRow("command only") << u"cpak install"_s << u"cpak install"_s;
        QTest::newRow("empty") << u"   "_s << u""_s;
    }

    void pastedQueries() {
        QFETCH(QString, typed);
        QFETCH(QString, searched);
        QCOMPARE(CpakClient::normalizeQuery(typed), searched);
    }

    void shortVersions() {
        QCOMPARE(CpakClient::shortVersion(u"1148813a88d4342d26b0994953d380f7c97b369e"_s), u"1148813a"_s);
        // names and numbers stay as they are, whatever their length
        for (const auto& version : {u"main"_s, u"stable"_s, u"67.7"_s, u"v1.0.0-rc1"_s, u"deadbeef"_s, u""_s}) {
            QCOMPARE(CpakClient::shortVersion(version), version);
        }
        // a long word that is not a commit ID
        QCOMPARE(CpakClient::shortVersion(u"release-candidate-for-the-next-year"_s), u"release-candidate-for-the-next-year"_s);
    }

    void arguments() {
        QCOMPARE(CpakClient::installFlatpakArguments(u"org.gimp.GIMP"_s),
            (QStringList{u"install"_s, u"--flatpak"_s, u"--yes"_s, u"org.gimp.GIMP"_s}));
    }
};

QTEST_APPLESS_MAIN(TestCpak)
#include "test_cpak.moc"
