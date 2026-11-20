// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "packagelist.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

/// The packages of Backend's PackageList for the views. It only reads the list: every role
/// is a field the delegates show, so a delegate gets an implicitly shared string, not a copy
/// of the whole package.
class PackageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")

 public:
    enum Role { NameRole = Qt::UserRole + 1, TitleRole, SummaryRole, SourceRole, InstalledRole, UpgradableRole, VersionRole, InstalledVersionRole };

    explicit PackageModel(const PackageList& list, QObject* parent = nullptr) : QAbstractListModel(parent), m_list(list) { }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] const PackageList& list() const { return m_list; }
    /// Wraps the replacement of the whole list.
    template <typename Replace>
    void replace(Replace replace) {
        beginResetModel();
        replace();
        endResetModel();
    }
    /// After PackageList::applyUpdates changed these rows.
    void updated(qsizetype first, qsizetype last);

 private:
    const PackageList& m_list;
};

/// Search, category and state filtering over PackageModel
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
    int rank(const Package& pkg, const PackageList::SearchKey& key) const;
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
