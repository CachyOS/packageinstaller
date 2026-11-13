// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagemodel.hpp"

#include <QScopedValueRollback>

#include <algorithm>
#include <numeric>

namespace {

bool isAscii(QStringView text) {
    return std::ranges::all_of(text, [](QChar c) { return c.unicode() < 0x80; });
}

}  // namespace

int PackageModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_packages.size());
}

QVariant PackageModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const auto& pkg = m_packages.at(index.row());
    switch (role) {
    case PackageRole: return QVariant::fromValue(pkg);
    case NameRole: return pkg.name;
    default: return {};
    }
}

QHash<int, QByteArray> PackageModel::roleNames() const {
    static const QHash<int, QByteArray> roles{{PackageRole, "pkg"}, {NameRole, "name"}};
    return roles;
}

void PackageModel::reset(QList<Package> packages) {
    beginResetModel();
    m_packages = std::move(packages);
    m_index.clear();
    m_index.reserve(m_packages.size());
    m_keys.clear();
    m_keys.reserve(m_packages.size());
    for (qsizetype i = 0; i < m_packages.size(); ++i) {
        const auto& pkg = m_packages.at(i);
        m_index.insert(pkg.name, i);
        SearchKey key;
        key.ascii = isAscii(pkg.name) && isAscii(pkg.title) && isAscii(pkg.summary) && isAscii(pkg.description);
        if (key.ascii) {
            key.name    = pkg.name.toLower();
            key.title   = pkg.title.toLower();
            key.summary = pkg.summary.toLower();
            // a package without AppStream data repeats its description as the summary
            if (pkg.description != pkg.summary) {
                key.description = pkg.description.toLower();
            }
        }
        m_keys.append(std::move(key));
    }

    // The order the filter would work out title by title, settled once here.
    const auto byTitle = [this](qsizetype a, qsizetype b) {
        return m_packages.at(a).title.compare(m_packages.at(b).title, Qt::CaseInsensitive);
    };
    QList<qsizetype> rows(m_packages.size());
    std::iota(rows.begin(), rows.end(), 0);
    std::ranges::stable_sort(rows, [&](qsizetype a, qsizetype b) { return byTitle(a, b) < 0; });
    m_titleOrder.resize(m_packages.size());
    for (qsizetype position = 0, order = 0; position < rows.size(); ++position) {
        if (position > 0 && byTitle(rows.at(position - 1), rows.at(position)) != 0) {
            ++order;
        }
        m_titleOrder[rows.at(position)] = order;
    }
    endResetModel();
}

void PackageModel::applyUpdates(const QHash<QString, QString>& newVersions) {
    qsizetype first = -1;
    qsizetype last  = -1;
    for (qsizetype i = 0; i < m_packages.size(); ++i) {
        auto& pkg             = m_packages[i];
        const auto found      = newVersions.constFind(pkg.name);
        const bool upgradable = found != newVersions.cend() && !pkg.ignored;
        if (!upgradable && !pkg.upgradable) {
            continue;
        }
        if (upgradable) {
            pkg.version = *found;
        }
        pkg.upgradable = upgradable;
        if (first < 0) {
            first = i;
        }
        last = i;
    }
    if (first >= 0) {
        emit dataChanged(index(static_cast<int>(first)), index(static_cast<int>(last)), {PackageRole});
    }
}

const Package* PackageModel::find(const QString& name) const {
    const auto found = m_index.constFind(name);
    return found == m_index.cend() ? nullptr : &m_packages.at(*found);
}

int PackageModel::upgradableCount() const {
    return static_cast<int>(std::ranges::count_if(m_packages, &Package::upgradable));
}

PackageFilter::PackageFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &PackageFilter::updateCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PackageFilter::updateCount);
    connect(this, &QAbstractItemModel::modelReset, this, &PackageFilter::updateCount);
    connect(this, &QAbstractItemModel::layoutChanged, this, &PackageFilter::updateCount);
}

void PackageFilter::updateCount() {
    if (!m_refiltering && rowCount() != m_count) {
        m_count = rowCount();
        emit countChanged();
    }
}

/// Changes the filter state in `apply`. Pass `resort` when that changes the search rank too.
template <typename Apply>
void PackageFilter::refilter(Apply apply, bool resort) {
    {
        const QScopedValueRollback guard(m_refiltering, true);
        if (resort) {
            apply();
            invalidate();
        } else {
            beginFilterChange();
            apply();
            endFilterChange(Direction::Rows);
        }
    }
    updateCount();
}

void PackageFilter::setQuery(const QString& query) {
    const auto trimmed = query.trimmed();
    if (trimmed == m_query) {
        return;
    }
    refilter([&] {
        m_query  = trimmed;
        m_needle = isAscii(trimmed) ? trimmed.toLower() : QString();
    }, true);
    emit queryChanged();
}

void PackageFilter::setCategory(const QString& category) {
    if (category == m_category) {
        return;
    }
    refilter([&] { m_category = category; }, false);
    emit categoryChanged();
}

void PackageFilter::setMode(Mode mode) {
    if (mode == m_mode) {
        return;
    }
    refilter([&] { m_mode = mode; }, false);
    emit modeChanged();
}

/// 0 = no match; lower is better otherwise.
int PackageFilter::rank(const Package& pkg, const PackageModel::SearchKey& key) const {
    if (m_query.isEmpty()) {
        return 1;
    }
    // Plain ASCII on both sides, as nearly always: compare the lower-cased copies as they are.
    if (key.ascii && !m_needle.isEmpty()) {
        if (key.name == m_needle || key.title == m_needle) {
            return 1;
        }
        if (key.name.startsWith(m_needle) || key.title.startsWith(m_needle)) {
            return 2;
        }
        if (key.name.contains(m_needle) || key.title.contains(m_needle)) {
            return 3;
        }
        return key.summary.contains(m_needle) || key.description.contains(m_needle) ? 4 : 0;
    }
    const auto cs = Qt::CaseInsensitive;
    if (pkg.name.compare(m_query, cs) == 0 || pkg.title.compare(m_query, cs) == 0) {
        return 1;
    }
    if (pkg.name.startsWith(m_query, cs) || pkg.title.startsWith(m_query, cs)) {
        return 2;
    }
    if (pkg.name.contains(m_query, cs) || pkg.title.contains(m_query, cs)) {
        return 3;
    }
    if (pkg.summary.contains(m_query, cs) || pkg.description.contains(m_query, cs)) {
        return 4;
    }
    return 0;
}

bool PackageFilter::filterAcceptsRow(int sourceRow, const QModelIndex& /*sourceParent*/) const {
    const auto* model = qobject_cast<const PackageModel*>(sourceModel());
    if (model == nullptr) {
        return false;
    }
    const auto& pkg = model->at(sourceRow);
    const bool shown = [&] {
        switch (m_mode) {
        case Apps: return pkg.isApp;
        case Installed: return pkg.installed();
        case Updates: return pkg.upgradable;
        case All: break;
        }
        return true;
    }();
    if (!shown || (!m_category.isEmpty() && !pkg.categories.contains(m_category))) {
        return false;
    }
    if (m_rank.size() != model->rowCount()) {
        m_rank.resize(model->rowCount());
    }
    m_rank[sourceRow] = rank(pkg, model->searchKey(sourceRow));
    return m_rank.at(sourceRow) != 0;
}

bool PackageFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto* model = static_cast<const PackageModel*>(sourceModel());
    if (const int ra = m_rank.value(left.row()), rb = m_rank.value(right.row()); ra != rb) {
        return ra < rb;
    }
    if (const bool a = model->at(left.row()).isApp, b = model->at(right.row()).isApp; a != b) {
        return a;
    }
    return model->titleOrder(left.row()) < model->titleOrder(right.row());
}
