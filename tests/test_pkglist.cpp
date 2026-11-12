// Checks pkglist.yaml: the shape the application expects, and that the listed
// packages still exist in the repositories.
#include <QFile>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTest>

namespace {

struct Entry {
    QString section;
    QStringList names;  // the package, then its companions
};

QList<Entry> readList(QStringList& sections) {
    QList<Entry> entries;
    QFile file(QStringLiteral(PKGLIST_FILE));
    if (!file.open(QIODevice::ReadOnly)) {
        return entries;
    }
    // same subset of YAML as Backend::loadCuratedList()
    while (!file.atEnd()) {
        const auto raw  = QString::fromUtf8(file.readLine());
        const auto line = raw.trimmed();
        if (line.startsWith(QLatin1String("- name:"))) {
            sections << line.mid(7).trimmed().remove(QLatin1Char('"'));
        } else if (line.startsWith(QLatin1String("- ")) && raw.startsWith(QLatin1Char(' ')) && !sections.isEmpty()) {
            entries << Entry{sections.last(), line.mid(2).remove(QLatin1Char('"')).split(QLatin1Char(' '), Qt::SkipEmptyParts)};
        }
    }
    return entries;
}

}  // namespace

class TestPkglist : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() {
        m_entries = readList(m_sections);
    }

    void shape() {
        QVERIFY2(!m_sections.isEmpty(), "no sections found in " PKGLIST_FILE);
        QCOMPARE(QSet<QString>(m_sections.cbegin(), m_sections.cend()).size(), m_sections.size());

        QSet<QString> populated;
        QSet<QString> seen;
        for (const auto& entry : std::as_const(m_entries)) {
            QVERIFY2(!entry.names.isEmpty(), qPrintable(QStringLiteral("empty entry in %1").arg(entry.section)));
            populated << entry.section;
            const auto& name = entry.names.first();
            QVERIFY2(!seen.contains(name), qPrintable(QStringLiteral("%1 is listed twice").arg(name)));
            seen << name;
        }
        for (const auto& section : std::as_const(m_sections)) {
            QVERIFY2(populated.contains(section), qPrintable(QStringLiteral("section %1 is empty").arg(section)));
        }
    }

    void packagesExist() {
        // the list is written for the x86_64 repositories; other ports lack some of it
        if (QSysInfo::currentCpuArchitecture() != QLatin1String("x86_64")) {
            QSKIP("only checked on x86_64");
        }
        const auto pacman = QStandardPaths::findExecutable(QStringLiteral("pacman"));
        if (pacman.isEmpty()) {
            QSKIP("pacman is not available");
        }
        QProcess process;
        process.start(pacman, {QStringLiteral("-Slq")});
        if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            QSKIP("could not list the sync databases");
        }
        const auto listed = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QSet<QString> available(listed.cbegin(), listed.cend());
        // a build chroot without the CachyOS repositories cannot judge the list
        if (!available.contains(QStringLiteral("cachyos-kernel-manager"))) {
            QSKIP("the CachyOS repositories are not set up here");
        }

        QStringList missing;
        for (const auto& entry : std::as_const(m_entries)) {
            for (const auto& name : entry.names) {
                if (!available.contains(name)) {
                    missing << QStringLiteral("%1 (%2)").arg(name, entry.section);
                }
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("not in the repositories: %1").arg(missing.join(QStringLiteral(", ")))));
    }

 private:
    QStringList m_sections;
    QList<Entry> m_entries;
};

QTEST_GUILESS_MAIN(TestPkglist)
#include "test_pkglist.moc"
