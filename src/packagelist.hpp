// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "alpmdb.hpp"

#include <cstdint>
#include <optional>
#include <utility>

#include <QHash>
#include <QList>

/// Every package of one database load, with what search and sorting need, moved onto
/// the loading thread.
class PackageList {
 public:
    /// What the search low-case compares against.
    struct SearchKey {
        QString name;
        QString title;
        QString summary;
        QString description;  ///< empty when it repeats the summary
        bool ascii = false;
    };

    PackageList() = default;
    explicit PackageList(QList<Package> packages);

    // container primitives
    [[nodiscard]] qsizetype size() const { return m_packages.size(); }
    [[nodiscard]] const Package& at(qsizetype row) const { return m_packages.at(row); }

    [[nodiscard]] const QList<Package>& packages() const { return m_packages; }
    [[nodiscard]] const SearchKey& searchKey(qsizetype row) const { return m_keys.at(row); }

    /// The position of a row when all packages are sorted by title, ignoring case; rows
    /// whose titles compare equal share one.
    [[nodiscard]] qsizetype titleOrder(qsizetype row) const { return m_titleOrder.at(row); }
    [[nodiscard]] const Package* find(const QString& name) const;
    [[nodiscard]] auto upgradableCount() const -> std::int32_t;

    /// Marks packages as upgradable with the version fresh database offers.
    /// Returns the first and last row that changed.
    auto applyUpdates(const QHash<QString, QString>& newVersions) -> std::optional<std::pair<qsizetype, qsizetype>>;

 private:
    QList<Package> m_packages;
    QList<SearchKey> m_keys;
    QList<qsizetype> m_titleOrder;
    QHash<QString, qsizetype> m_index;
};
