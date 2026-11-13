// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "alpmdb.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

class PackageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")

 public:
    enum Role { PackageRole = Qt::UserRole + 1, NameRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(QList<Package> packages);
    /// Marks packages as upgradable with the version a fresh database offers (from `checkupdates`).
    void applyUpdates(const QHash<QString, QString>& newVersions);

    /// What the search compares against: the fields in lower case, so that a search needs
    /// no case folding. Only used when `ascii` holds, where lower case and Unicode case
    /// folding are the same thing; other packages are searched in their own fields.
    struct SearchKey {
        QString name;
        QString title;
        QString summary;
        QString description;  ///< empty when it repeats the summary
        bool ascii = false;
    };

    const QList<Package>& packages() const { return m_packages; }
    const SearchKey& searchKey(qsizetype row) const { return m_keys.at(row); }
    /// The position of a row when all packages are sorted by title, ignoring case; rows
    /// whose titles compare equal share one.
    qsizetype titleOrder(qsizetype row) const { return m_titleOrder.at(row); }
    const Package& at(qsizetype row) const { return m_packages.at(row); }
    const Package* find(const QString& name) const;
    int upgradableCount() const;

 private:
    QList<Package> m_packages;
    QList<SearchKey> m_keys;
    QList<qsizetype> m_titleOrder;
    QHash<QString, qsizetype> m_index;
};

/// Search, category and state filtering on top of PackageModel, usable from QML.
class PackageFilter : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged FINAL)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

 public:
    enum Mode { All, Apps, Installed, Updates };
    Q_ENUM(Mode)

    explicit PackageFilter(QObject* parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString& query);
    QString category() const { return m_category; }
    void setCategory(const QString& category);
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    int count() const { return rowCount(); }

 signals:
    void queryChanged();
    void categoryChanged();
    void modeChanged();
    void countChanged();

 protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

 private:
    int rank(const Package& pkg, const PackageModel::SearchKey& key) const;
    template <typename Apply>
    void refilter(Apply apply, bool resort);
    void updateCount();

    QString m_query;
    QString m_needle;  ///< the query in lower case; empty unless the query is plain ASCII
    QString m_category;
    Mode m_mode = All;
    mutable QList<int> m_rank;  ///< search rank per source row, filled by filterAcceptsRow
    int m_count        = 0;
    bool m_refiltering = false;
};
