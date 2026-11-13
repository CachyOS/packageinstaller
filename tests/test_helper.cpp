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

    void rejectsBadInput() {
        for (const auto& args : {QStringList{}, QStringList{u"unlock"_s}, QStringList{u"install"_s, u"-x"_s},
                 QStringList{u"upgrade"_s, u"foo"_s}, QStringList{u"remove"_s},
                 QStringList{u"pacman"_s, u"--remove"_s, u"--"_s, u"foo"_s},
                 QStringList{u"pacman"_s, u"--sync"_s, u"--overwrite"_s, u"--"_s, u"foo"_s},
                 QStringList{u"pacman"_s, u"--upgrade"_s, u"--"_s, u"/etc/passwd"_s}}) {
            const auto [exitCode, output] = run(args);
            QCOMPARE(exitCode, 2);
            QVERIFY(output.isEmpty());
        }
    }

 private:
    std::pair<int, QString> run(const QStringList& args) const {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(u"CACHYOS_PI_HELPER_PACMAN"_s, m_dir.filePath(u"pacman"_s));
        process.setProcessEnvironment(env);
        process.start(u"" HELPER ""_s, args);
        if (!process.waitForFinished() || process.exitStatus() != QProcess::NormalExit) {
            return {-1, {}};
        }
        return {process.exitCode(), QString::fromUtf8(process.readAllStandardOutput())};
    }

    QTemporaryDir m_dir;
    QString m_package;
};

QTEST_GUILESS_MAIN(TestHelper)
#include "test_helper.moc"
