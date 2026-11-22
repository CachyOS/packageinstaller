// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapcli.hpp"

#include <QTest>

using namespace Qt::StringLiterals;

class TestSnapCli : public QObject {
    Q_OBJECT

 private slots:
    void find() {
        const auto apps = snapcli::parseFind({
            u"Name  Version  Developer  Notes  Summary"_s,
            u"vlc   3.0.4   videolan✓   -   The ultimate media player."_s,
            u"powershell 7.4 powershell✓ classic Microsoft PowerShell"_s,
            u"vlc 3.0.4 videolan✓"_s,  // 3 columns: too few to be a row
        });
        QCOMPARE(apps.size(), 2);
        QCOMPARE(apps[0].name, u"vlc"_s);
        QCOMPARE(apps[0].version, u"3.0.4"_s);
        QCOMPARE(apps[0].publisher, u"videolan✓"_s);
        QCOMPARE(apps[0].notes, u"-"_s);
        QCOMPARE(apps[0].summary, u"The ultimate media player."_s);
        QCOMPARE(apps[1].name, u"powershell"_s);
        QCOMPARE(apps[1].publisher, u"powershell✓"_s);
        QCOMPARE(apps[1].notes, u"classic"_s);
        QCOMPARE(apps[1].summary, u"Microsoft PowerShell"_s);
    }

    void list() {
        const auto snaps = snapcli::parseList({
            u"Name  Version  Rev  Tracking  Publisher  Notes"_s,
            u"firefox  120.0.1-1  3504  latest/stable  mozilla✓  -"_s,
            u"alacritty 0.8.0 46 latest/stable snapcrafters classic"_s,
            u"firefox 120.0.1-1 3504"_s,  // too few columns
        });
        QCOMPARE(snaps.size(), 2);
        QCOMPARE(snaps[0].name, u"firefox"_s);
        QCOMPARE(snaps[0].version, u"120.0.1-1"_s);
        QCOMPARE(snaps[0].revision, u"3504"_s);
        QCOMPARE(snaps[0].channel, u"latest/stable"_s);
        QCOMPARE(snaps[0].publisher, u"mozilla✓"_s);
        QCOMPARE(snaps[0].notes, u"-"_s);
        QCOMPARE(snaps[1].name, u"alacritty"_s);
        QCOMPARE(snaps[1].publisher, u"snapcrafters"_s);
        QCOMPARE(snaps[1].notes, u"classic"_s);
    }

    void updates() {
        const auto snaps = snapcli::parseUpdates({
            u"Name  Version  Rev  Size  Publisher  Notes"_s,
            u"core  16-2.45.1  9584  canonical✓  core"_s,     // no Size column
            u"firefox  129.0-2  4698  282MB  mozilla✓  -"_s,  // Size column present
        });
        QCOMPARE(snaps.size(), 2);
        QCOMPARE(snaps[0].name, u"core"_s);
        QCOMPARE(snaps[0].version, u"16-2.45.1"_s);
        QCOMPARE(snaps[0].revision, u"9584"_s);
        // With no Size column, publisher/notes are columns 4 and 5.
        QCOMPARE(snaps[0].publisher, u"canonical✓"_s);
        QCOMPARE(snaps[0].notes, u"core"_s);
        QCOMPARE(snaps[1].name, u"firefox"_s);
        QCOMPARE(snaps[1].version, u"129.0-2"_s);
        QCOMPARE(snaps[1].revision, u"4698"_s);
        // With a Size column, publisher/notes are still anchored from the right.
        QCOMPARE(snaps[1].publisher, u"mozilla✓"_s);
        QCOMPARE(snaps[1].notes, u"-"_s);

        QVERIFY(snapcli::parseUpdates({u"All snaps up to date."_s}).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSnapCli)
#include "test_snapcli.moc"
