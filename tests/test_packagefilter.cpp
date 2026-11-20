// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagemodel.hpp"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QStringList shown(const PackageFilter& filter) {
    QStringList names;
    for (int row = 0; row < filter.rowCount(); ++row) {
        names << filter.index(row, 0).data(PackageModel::NameRole).toString();
    }
    return names;
}

}  // namespace

class TestPackageFilter : public QObject {
    Q_OBJECT

 private slots:
    void init() {
        QList<Package> packages;
        for (const auto* name : {"alpha", "bravo", "charlie", "delta", "echo"}) {
            Package pkg;
            pkg.name  = QString::fromLatin1(name);
            pkg.title = pkg.name;
            packages << pkg;
        }
        m_model.replace([&] { m_list = PackageList(packages); });
        m_filter.setSourceModel(&m_model);
        m_filter.setNames({});
        m_filter.setLimit(-1);
        m_filter.setQuery({});
    }

    void shelfOrder() {
        m_filter.setNames({u"echo"_s, u"alpha"_s, u"missing"_s, u"charlie"_s});
        QCOMPARE(shown(m_filter), (QStringList{u"echo"_s, u"alpha"_s, u"charlie"_s}));
        QCOMPARE(m_filter.count(), 3);
        // the search of the browse page does not apply to a shelf
        m_filter.setQuery(u"bravo"_s);
        QCOMPARE(m_filter.count(), 3);
    }

    void limit() {
        m_filter.setNames({u"delta"_s, u"bravo"_s, u"echo"_s, u"alpha"_s});
        m_filter.setLimit(2);
        QCOMPARE(shown(m_filter), (QStringList{u"delta"_s, u"bravo"_s}));

        // the cards already shown must stay: rows are added and removed, never reset
        QSignalSpy reset(&m_filter, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&m_filter, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&m_filter, &QAbstractItemModel::rowsRemoved);
        m_filter.setLimit(3);
        QCOMPARE(shown(m_filter), (QStringList{u"delta"_s, u"bravo"_s, u"echo"_s}));
        m_filter.setLimit(-1);
        QCOMPARE(m_filter.count(), 4);
        m_filter.setLimit(1);
        QCOMPARE(shown(m_filter), QStringList{u"delta"_s});
        QCOMPARE(reset.count(), 0);
        QCOMPARE(inserted.count(), 2);
        QCOMPARE(removed.count(), 1);
    }

    void noShelf() {
        m_filter.setNames({});
        QCOMPARE(m_filter.count(), 5);
    }

 private:
    PackageList m_list;
    PackageModel m_model{m_list};
    PackageFilter m_filter;
};

QTEST_GUILESS_MAIN(TestPackageFilter)
#include "test_packagefilter.moc"
