// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestHelper : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QVERIFY(QFile::copy(u"" TEST_DATA_DIR "/fake-pacman.sh"_s, m_dir.filePath(u"pacman"_s)));
        QFile package(m_dir.filePath(u"foo-1-1-x86_64.pkg.tar.zst"_s));
        QVERIFY(package.open(QIODevice::WriteOnly));
        m_package = QFileInfo(package).canonicalFilePath();
        QFile snapFile(m_dir.filePath(u"a.snap"_s));
        QVERIFY(snapFile.open(QIODevice::WriteOnly));
        m_snap = QFileInfo(snapFile).canonicalFilePath();
        // the script names itself, so it stands in for every program
        QVERIFY(QFile::copy(u"" TEST_DATA_DIR "/fake-pacman.sh"_s, m_dir.filePath(u"cachyos-rate-mirrors"_s)));
        QVERIFY(QFile::copy(u"" TEST_DATA_DIR "/fake-pacman.sh"_s, m_dir.filePath(u"snap"_s)));
        QVERIFY(QFile::copy(u"" TEST_DATA_DIR "/fake-pacman.sh"_s, m_dir.filePath(u"systemctl"_s)));
    }

    void runsPacman_data() {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<QString>("pacman");
        QTest::newRow("install") << QStringList{u"install"_s, u"foo"_s, u"bar"_s} << u"-S --noconfirm --needed -- foo bar"_s;
        QTest::newRow("remove") << QStringList{u"remove"_s, u"foo"_s} << u"-Rs --noconfirm -- foo"_s;
        QTest::newRow("upgrade") << QStringList{u"upgrade"_s} << u"-Syu --noconfirm"_s;
        QTest::newRow("paru sync") << QStringList{u"pacman"_s, u"--sync"_s, u"--needed"_s, u"--"_s, u"core/foo"_s}
                                   << u"--sync --needed -- core/foo"_s;
        QTest::newRow("paru upgrade") << QStringList{u"pacman"_s, u"--upgrade"_s, u"--noconfirm"_s, u"--"_s, m_package}
                                      << u"--upgrade --noconfirm -- "_s + m_package;
        QTest::newRow("paru database") << QStringList{u"pacman"_s, u"--database"_s, u"--asdeps"_s, u"--"_s, u"foo"_s}
                                       << u"--database --asdeps -- foo"_s;
    }

    void runsPacman() {
        QFETCH(QStringList, args);
        QFETCH(QString, pacman);
        const auto [exitCode, output] = run(args);
        QCOMPARE(exitCode, 0);
        QCOMPARE(output, u"fake pacman %1 sigpipe=ignored setsid=yes\n"_s.arg(pacman));
    }

    void runsSnap_data() {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<QString>("snap");
        QTest::newRow("install") << QStringList{u"snap"_s, u"install"_s, u"vlc"_s} << u"install -- vlc"_s;
        QTest::newRow("install file") << QStringList{u"snap"_s, u"install"_s, m_snap} << u"install "_s + m_snap;
        QTest::newRow("remove") << QStringList{u"snap"_s, u"remove"_s, u"vlc"_s} << u"remove -- vlc"_s;
        QTest::newRow("refresh all") << QStringList{u"snap"_s, u"refresh"_s} << u"refresh"_s;
        QTest::newRow("refresh one") << QStringList{u"snap"_s, u"refresh"_s, u"firefox"_s} << u"refresh -- firefox"_s;
    }

    void runsSnap() {
        QFETCH(QStringList, args);
        QFETCH(QString, snap);
        const auto [exitCode, output] = run(args);
        QCOMPARE(exitCode, 0);
        QCOMPARE(output, u"fake snap %1 sigpipe=ignored setsid=yes\n"_s.arg(snap));
    }

    void runsSnapdSetup_data() {
        QTest::addColumn<bool>("apparmorService");
        QTest::addColumn<QString>("systemctl");
        QTest::newRow("no apparmor service") << false << u"enable --now snapd.socket"_s;
        QTest::newRow("apparmor service") << true << u"enable --now snapd.socket snapd.apparmor.service"_s;
    }

    void runsSnapdSetup() {
        QFETCH(bool, apparmorService);
        QFETCH(QString, systemctl);
        const auto [exitCode, output] = run({u"snapd-setup"_s}, apparmorService);
        QCOMPARE(exitCode, 0);
        QCOMPARE(output, u"fake systemctl %1 sigpipe=ignored setsid=yes\n"_s.arg(systemctl));
    }

    void ranksMirrors() {
        const auto [exitCode, output] = run({u"rate-mirrors"_s});
        QCOMPARE(exitCode, 0);
        QCOMPARE(output, u"fake cachyos-rate-mirrors  sigpipe=ignored setsid=yes\n"_s);
        // the helper says what it starts; the GUI tells a refused authentication by its absence
        QCOMPARE(m_errors, u"cachyos-pi-helper: running cachyos-rate-mirrors\n"_s);
    }

    void rejectsBadInput() {
        for (const auto& args : {QStringList{}, QStringList{u"unlock"_s}, QStringList{u"install"_s, u"-x"_s},
                 QStringList{u"upgrade"_s, u"foo"_s}, QStringList{u"remove"_s},
                 QStringList{u"rate-mirrors"_s, u"--help"_s}, QStringList{u"rate-mirrors"_s, QString()},
                 QStringList{u"pacman"_s, u"--remove"_s, u"--"_s, u"foo"_s},
                 QStringList{u"pacman"_s, u"--sync"_s, u"--overwrite"_s, u"--"_s, u"foo"_s},
                 QStringList{u"pacman"_s, u"--upgrade"_s, u"--"_s, u"/etc/passwd"_s},
                 QStringList{u"snap"_s, u"remove"_s, u"--channel=bad"_s},
                 QStringList{u"snap"_s, u"install"_s, u"relative.snap"_s},
                 QStringList{u"snap"_s, u"install"_s, u"/tmp/not-a-snap.txt"_s},
                 QStringList{u"snap"_s, u"install"_s, m_dir.filePath(u"absent.snap"_s)},
                 QStringList{u"snap"_s, u"install"_s, u"vlc"_s, u"firefox"_s},
                 QStringList{u"snap"_s, u"set"_s, u"vlc"_s},
                 QStringList{u"snap"_s},
                 QStringList{u"snapd-setup"_s, u"foo"_s},
                 QStringList{u"snapd-setup"_s, u"--now"_s, u"evil"_s}}) {
            const auto [exitCode, output] = run(args);
            QCOMPARE(exitCode, 2);
            QVERIFY(output.isEmpty());
        }
    }

 private:
    std::pair<int, QString> run(const QStringList& args, bool apparmorService = false) const {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(u"CACHYOS_PI_HELPER_BINDIR"_s, m_dir.path());
        // force the AppArmor service-unit branch for the helper's test build, independently of the host
        env.insert(u"CACHYOS_PI_APPARMOR_PRESENT"_s, apparmorService ? u"1"_s : u"0"_s);
        process.setProcessEnvironment(env);
        process.start(u"" HELPER ""_s, args);
        if (!process.waitForFinished() || process.exitStatus() != QProcess::NormalExit) {
            return {-1, {}};
        }
        m_errors = QString::fromUtf8(process.readAllStandardError());
        return {process.exitCode(), QString::fromUtf8(process.readAllStandardOutput())};
    }

    QTemporaryDir m_dir;
    QString m_package;
    QString m_snap;
    mutable QString m_errors;  ///< what the last run wrote to stderr
};

QTEST_GUILESS_MAIN(TestHelper)
#include "test_helper.moc"
