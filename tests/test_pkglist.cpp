// Checks pkglist.yaml: the shape the application expects, and that the listed
// packages still exist in the repositories.
#include "catalog.hpp"
#include "process.hpp"

#include <QBuffer>
#include <QFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTest>

using namespace Qt::StringLiterals;

class TestPkglist : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() {
        QFile file(QString::fromUtf8(PKGLIST_FILE));
        QVERIFY2(file.open(QIODevice::ReadOnly), "cannot open " PKGLIST_FILE);
        m_list = catalog::loadCurated(file);
    }

    void parse() {
        QByteArray yaml = R"(- name: "Video"
  packages:
    - vlc
    - "mpv mpv-mpris yt-dlp"
- name: Empty
  packages: []
)";
        QBuffer buffer(&yaml);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        const auto list = catalog::loadCurated(buffer);
        QCOMPARE(list.sections.size(), 2);
        QCOMPARE(list.sections.at(0).title, u"Video"_s);
        QCOMPARE(list.sections.at(0).names, (QStringList{u"vlc"_s, u"mpv"_s}));
        QCOMPARE(list.companions.value(u"mpv"_s), (QStringList{u"mpv-mpris"_s, u"yt-dlp"_s}));
        QVERIFY(!list.companions.contains(u"vlc"_s));
        QVERIFY(list.sections.at(1).names.isEmpty());
    }

    void shape() {
        const auto& sections = m_list.sections;
        QVERIFY2(!sections.isEmpty(), "no sections found in " PKGLIST_FILE);

        QSet<QString> titles;
        QSet<QString> seen;
        for (const auto& section : sections) {
            QVERIFY2(!titles.contains(section.title), qPrintable(u"section %1 is listed twice"_s.arg(section.title)));
            titles << section.title;
            QVERIFY2(!section.names.isEmpty(), qPrintable(u"section %1 is empty"_s.arg(section.title)));
            for (const auto& name : section.names) {
                QVERIFY2(!seen.contains(name), qPrintable(u"%1 is listed twice"_s.arg(name)));
                seen << name;
            }
        }
    }

    void packagesExist() {
        // the list is written for the x86_64 repositories; other ports lack some of it
        if (QSysInfo::currentCpuArchitecture() != "x86_64"_L1) {
            QSKIP("only checked on x86_64");
        }
        const auto pacman = QStandardPaths::findExecutable(u"pacman"_s);
        if (pacman.isEmpty()) {
            QSKIP("pacman is not available");
        }
        const auto listed = outputLines(pacman, {u"-Slq"_s});
        if (listed.isEmpty()) {
            QSKIP("could not list the sync databases");
        }
        const QSet<QString> available(listed.cbegin(), listed.cend());
        // a build chroot without the CachyOS repositories cannot judge the list
        if (!available.contains(u"cachyos-kernel-manager"_s)) {
            QSKIP("the CachyOS repositories are not set up here");
        }

        QStringList missing;
        for (const auto& section : std::as_const(m_list.sections)) {
            for (const auto& name : section.names) {
                for (const auto& required : QStringList{name} + m_list.companions.value(name)) {
                    if (!available.contains(required)) {
                        missing << u"%1 (%2)"_s.arg(required, section.title);
                    }
                }
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(u"not in the repositories: %1"_s.arg(missing.join(u", "_s))));
    }

 private:
    catalog::CuratedList m_list;
};

QTEST_GUILESS_MAIN(TestPkglist)
#include "test_pkglist.moc"
