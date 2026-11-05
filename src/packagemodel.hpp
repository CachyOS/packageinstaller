// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "alpmdb.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class PackageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")

 public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        TitleRole,
        SummaryRole,
        DescriptionRole,
        LongDescriptionRole,
        VersionRole,
        InstalledVersionRole,
        InstalledRole,
        UpgradableRole,
        RepoRole,
        SourceRole,
        UrlRole,
        LicensesRole,
        DependsRole,
        InstallSizeRole,
        DownloadSizeRole,
        IsAppRole,
        DeveloperRole,
        CategoriesRole,
        ScreenshotsRole,
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(QList<Package> packages);
    /// Marks packages as upgradable with the version a fresh database offers (from `checkupdates`).
    void applyUpdates(const QHash<QString, QString>& newVersions);

    [[nodiscard]] const Package& at(int row) const { return m_packages.at(row); }
    [[nodiscard]] const Package* find(const QString& name) const;
    [[nodiscard]] QVariantMap toMap(const Package& pkg) const;
    [[nodiscard]] int upgradableCount() const;
    [[nodiscard]] QStringList upgradableNames() const;

 private:
    [[nodiscard]] static QVariant field(const Package& pkg, int role);

    QList<Package> m_packages;
    QHash<QString, int> m_index;
};

/// Search, category and state filtering on top of PackageModel, usable from QML.
class PackageFilter : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

 public:
    enum Mode { All,
        Apps,
        Installed,
        Updates };
    Q_ENUM(Mode)

    explicit PackageFilter(QObject* parent = nullptr);

    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] QString category() const { return m_category; }
    void setCategory(const QString& category);
    [[nodiscard]] Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    [[nodiscard]] int count() const { return rowCount(); }

 signals:
    void queryChanged();
    void categoryChanged();
    void modeChanged();
    void countChanged();

 protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

 private:
    [[nodiscard]] const PackageModel* packages() const;
    [[nodiscard]] int rank(const Package& pkg) const;
    void refilter();

    QString m_query;
    QString m_category;
    Mode m_mode = All;
};
