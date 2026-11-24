// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagemodel.hpp"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

Package package(const char* name, const char* title, const char* installedVersion = "") {
    Package pkg;
    pkg.name             = QString::fromLatin1(name);
    pkg.title            = QString::fromLatin1(title);
    pkg.version          = u"1.0-1"_s;
    pkg.installedVersion = QString::fromLatin1(installedVersion);
    pkg.repo             = u"extra"_s;
    return pkg;
}

}  // namespace

class TestPackageModel : public QObject {
    Q_OBJECT

 private slots:
    void init() {
        m_model.replace([&] {
            m_list = PackageList({package("zed", "Zed", "1.0-1"), package("alpha", "alpha"), package("beta", "Alpha")});
        });
    }

    void roles() {
        QCOMPARE(m_model.rowCount(), 3);
        const auto first = m_model.index(0);
        QCOMPARE(first.data(PackageModel::NameRole).toString(), u"zed"_s);
        QCOMPARE(first.data(PackageModel::TitleRole).toString(), u"Zed"_s);
        QCOMPARE(first.data(PackageModel::SourceRole).toString(), u"arch"_s);
        QCOMPARE(first.data(PackageModel::InstalledRole).toBool(), true);
        QCOMPARE(m_model.index(1).data(PackageModel::InstalledRole).toBool(), false);

        const auto names = m_model.roleNames().values();
        for (const auto* role : {"name", "title", "summary", "source", "installed", "upgradable", "version", "installedVersion"}) {
            QVERIFY2(names.contains(role), role);
        }
    }

    void sources() {
        const auto source = [](const char* repo) {
            Package pkg;
            pkg.repo = QString::fromLatin1(repo);
            return pkg.source();
        };
        QCOMPARE(source("core"), u"arch"_s);
        QCOMPARE(source("extra"), u"arch"_s);
        QCOMPARE(source("multilib"), u"arch"_s);
        QCOMPARE(source("cachyos"), u"cachyos"_s);
        QCOMPARE(source("cachyos-v3"), u"cachyos"_s);
        QCOMPARE(source("cachyos-extra-znver4"), u"cachyos"_s);
        QCOMPARE(source("chaotic-aur"), u"chaotic-aur"_s);
        QCOMPARE(source("local"), u"local"_s);
    }

    void lookups() {
        QCOMPARE(m_list.find(u"beta"_s)->title, u"Alpha"_s);
        QCOMPARE(m_list.find(u"missing"_s), nullptr);

        // case-insensitives
        QCOMPARE(m_list.titleOrder(1), m_list.titleOrder(2));
        QVERIFY(m_list.titleOrder(1) < m_list.titleOrder(0));
        QCOMPARE(m_list.searchKey(0).title, u"zed"_s);
    }

    void updates() {
        QSignalSpy changed(&m_model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&m_model, &QAbstractItemModel::modelReset);
        const auto rows = m_list.applyUpdates({{u"zed"_s, u"2.0-1"_s}});
        QVERIFY(rows.has_value());
        m_model.updated(rows->first, rows->second);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m_model.index(0).data(PackageModel::UpgradableRole).toBool(), true);
        QCOMPARE(m_model.index(0).data(PackageModel::VersionRole).toString(), u"2.0-1"_s);
        QCOMPARE(m_list.upgradableCount(), 1);

        QCOMPARE(m_list.applyUpdates({}), std::optional(std::pair<qsizetype, qsizetype>{0, 0}));
        QCOMPARE(m_list.applyUpdates({}), std::nullopt);
    }

 private:
    PackageList m_list;
    PackageModel m_model{m_list};
};

QTEST_GUILESS_MAIN(TestPackageModel)
#include "test_packagemodel.moc"
