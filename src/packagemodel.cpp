// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagemodel.hpp"

#include <QScopedValueRollback>

#include <algorithm>

namespace {

bool isAscii(QStringView text) {
    return std::ranges::all_of(text, [](QChar c) { return c.unicode() < 0x80; });
}

}  // namespace

int PackageModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_list.size());
}

QVariant PackageModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const auto& pkg = m_list.at(index.row());
    switch (role) {
    case NameRole:
        return pkg.name;
    case TitleRole:
        return pkg.title;
    case SummaryRole:
        return pkg.summary;
    case SourceRole:
        return pkg.source();
    case InstalledRole:
        return pkg.installed();
    case UpgradableRole:
        return pkg.upgradable;
    case VersionRole:
        return pkg.version;
    case InstalledVersionRole:
        return pkg.installedVersion;
    default:
        return {};
    }
}

QHash<int, QByteArray> PackageModel::roleNames() const {
    static const QHash<int, QByteArray> roles{
        {NameRole, "name"},
        {TitleRole, "title"},
        {SummaryRole, "summary"},
        {SourceRole, "source"},
        {InstalledRole, "installed"},
        {UpgradableRole, "upgradable"},
        {VersionRole, "version"},
        {InstalledVersionRole, "installedVersion"},
    };
    return roles;
}

void PackageModel::updated(qsizetype first, qsizetype last) {
    emit dataChanged(index(static_cast<int>(first)), index(static_cast<int>(last)), {UpgradableRole, VersionRole});
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
    },
        true);
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
int PackageFilter::rank(const Package& pkg, const PackageList::SearchKey& key) const {
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
    const auto& list = model->list();
    const auto& pkg  = list.at(sourceRow);
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
    m_rank[sourceRow] = rank(pkg, list.searchKey(sourceRow));
    return m_rank.at(sourceRow) != 0;
}

bool PackageFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto& list = static_cast<const PackageModel*>(sourceModel())->list();
    if (const int ra = m_rank.value(left.row()), rb = m_rank.value(right.row()); ra != rb) {
        return ra < rb;
    }
    if (const bool a = list.at(left.row()).isApp, b = list.at(right.row()).isApp; a != b) {
        return a;
    }
    return list.titleOrder(left.row()) < list.titleOrder(right.row());
}
