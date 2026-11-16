// SPDX-License-Identifier: GPL-3.0-or-later
#include "gitoutput.hpp"

#include <QTest>

using namespace Qt::StringLiterals;

using git::DiffRun;
using Kind = git::DiffRun::Kind;

class TestGitOutput : public QObject {
    Q_OBJECT

 private slots:
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
        const auto changes = git::readableDiff(diff);
        // a removed line that looks like a header stays what it is
        QCOMPARE(changes.text.split(u'\n'), (QStringList{u"PKGBUILD"_s, u"@@ -1,3 +1,3 @@"_s, u" pkgname=a"_s, u"-pkgver=1"_s,
                                                u"+pkgver=2"_s, u"a.install (new file)"_s, u"@@ -0,0 +1 @@"_s, u"+post_install() { :; }"_s,
                                                u"old.patch (removed)"_s, u"@@ -1 +0,0 @@"_s, u"--- a/x"_s, u"logo.png"_s,
                                                u"\\ binary file, not shown"_s, u"run.sh (mode changed)"_s}));
        QCOMPARE(changes.runs, (QList<DiffRun>{{1, 1, Kind::File}, {2, 1, Kind::Hunk}, {4, 1, Kind::Removed}, {5, 1, Kind::Added},
                                   {6, 1, Kind::File}, {7, 1, Kind::Hunk}, {8, 1, Kind::Added}, {9, 1, Kind::File}, {10, 1, Kind::Hunk},
                                   {11, 1, Kind::Removed}, {12, 1, Kind::File}, {14, 1, Kind::File}}));
        QCOMPARE(git::readableDiff(QString()).text, QString());
    }

    void runsOfOneKindAreJoined() {
        const auto changes = git::readableDiff(u"diff --git a/f b/f\n--- a/f\n+++ b/f\n@@ -1 +1,3 @@\n-a\n+b\n+c\n+d\n"_s);
        QCOMPARE(changes.runs, (QList<DiffRun>{{1, 1, Kind::File}, {2, 1, Kind::Hunk}, {3, 1, Kind::Removed}, {4, 3, Kind::Added}}));
    }

    void fileNames() {
        // the kind comes from where the line is, not from how the name starts
        const auto plus = git::readableDiff(u"diff --git a/+evil b/+evil\nnew file mode 100644\n--- /dev/null\n+++ b/+evil\n@@ -0,0 +1 @@\n+x\n"_s);
        QCOMPARE(plus.text.split(u'\n').first(), u"+evil (new file)"_s);
        QCOMPARE(plus.runs.first(), (DiffRun{1, 1, Kind::File}));
        // a name that contains " b/" is not cut there
        QCOMPARE(git::diffFile(u"a/x b/PKGBUILD b/x b/PKGBUILD"), u"x b/PKGBUILD"_s);
        QCOMPARE(git::diffFile(u"a/PKGBUILD b/PKGBUILD"), u"PKGBUILD"_s);
        QCOMPARE(git::diffFile(u"\"a/tab\\there\" \"b/tab\\there\""), u"tab\there"_s);
        QCOMPARE(git::diffFile(u"a/one b/other"), QString());
        QCOMPARE(git::diffFile(u"a/x b/x "), QString());
    }

    void unquoting() {
        QCOMPARE(git::unquoted(u"plain"), u"plain"_s);
        QCOMPARE(git::unquoted(u"\"a\\\"b\\\\c\""), u"a\"b\\c"_s);
        // octal escapes are bytes of UTF-8
        QCOMPARE(git::unquoted(u"\"\\303\\244.txt\""), u"ä.txt"_s);
        QCOMPARE(git::unquoted(u"\"ä\\n\""), u"ä\n"_s);
    }

    void grepOutput() {
        const auto output = u"c2:.SRCINFO:\tpkgver = 1.1\nc2:.SRCINFO:\tpkgrel = 1\nc2:.SRCINFO:pkgname = a\n"
                            "c1:.SRCINFO:\tpkgver = 1.0\nc1:.SRCINFO:\tpkgrel = 1\n"_s;
        const auto matches = git::grepByCommit(output);
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches.at(0).commit, u"c2"_s);
        QCOMPARE(matches.at(0).lines, u"\tpkgver = 1.1\n\tpkgrel = 1\npkgname = a\n"_s);
        QCOMPARE(matches.at(1).commit, u"c1"_s);
        QCOMPARE(matches.at(1).lines, u"\tpkgver = 1.0\n\tpkgrel = 1\n"_s);
        QVERIFY(git::grepByCommit(u"garbage\n"_s).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestGitOutput)
#include "test_gitoutput.moc"
