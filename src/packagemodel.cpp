// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagemodel.hpp"

int PackageModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_packages.size());
}

QVariant PackageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_packages.size()) {
        return {};
    }
    return field(m_packages.at(index.row()), role);
}

QVariant PackageModel::field(const Package& pkg, int role) {
    switch (role) {
    case NameRole: return pkg.name;
    case TitleRole: return pkg.title;
    case SummaryRole: return pkg.summary;
    case DescriptionRole: return pkg.description;
    case LongDescriptionRole: return pkg.longDescription;
    case VersionRole: return pkg.version;
    case InstalledVersionRole: return pkg.installedVersion;
    case InstalledRole: return pkg.installed();
    case UpgradableRole: return pkg.upgradable;
    case RepoRole: return pkg.repo;
    case SourceRole:
        if (pkg.foreign()) {
            return QStringLiteral("local");
        }
        return pkg.repo.startsWith(QLatin1String("cachyos")) ? QStringLiteral("cachyos") : QStringLiteral("arch");
    case UrlRole: return pkg.url;
    case LicensesRole: return pkg.licenses;
    case DependsRole: return pkg.depends;
    case InstallSizeRole: return pkg.installSize;
    case DownloadSizeRole: return pkg.downloadSize;
    case IsAppRole: return pkg.isApp;
    case DeveloperRole: return pkg.developer;
    case CategoriesRole: return pkg.categories;
    case ScreenshotsRole: return pkg.screenshots;
    default: return {};
    }
}

QHash<int, QByteArray> PackageModel::roleNames() const {
    return {
        {NameRole, "name"},
        {TitleRole, "title"},
        {SummaryRole, "summary"},
        {DescriptionRole, "description"},
        {LongDescriptionRole, "longDescription"},
        {VersionRole, "version"},
        {InstalledVersionRole, "installedVersion"},
        {InstalledRole, "installed"},
        {UpgradableRole, "upgradable"},
        {RepoRole, "repo"},
        {SourceRole, "source"},
        {UrlRole, "url"},
        {LicensesRole, "licenses"},
        {DependsRole, "depends"},
        {InstallSizeRole, "installSize"},
        {DownloadSizeRole, "downloadSize"},
        {IsAppRole, "isApp"},
        {DeveloperRole, "developer"},
        {CategoriesRole, "categories"},
        {ScreenshotsRole, "screenshots"},
    };
}

void PackageModel::reset(QList<Package> packages) {
    beginResetModel();
    m_packages = std::move(packages);
    m_index.clear();
    m_index.reserve(m_packages.size());
    for (int i = 0; i < m_packages.size(); ++i) {
        m_index.insert(m_packages.at(i).name, i);
    }
    endResetModel();
}

void PackageModel::applyUpdates(const QHash<QString, QString>& newVersions) {
    for (int i = 0; i < m_packages.size(); ++i) {
        auto& pkg        = m_packages[i];
        const auto found = newVersions.constFind(pkg.name);
        const bool upgradable = found != newVersions.cend();
        if (!upgradable && !pkg.upgradable) {
            continue;
        }
        if (upgradable) {
            pkg.version = *found;
        }
        pkg.upgradable = upgradable;
        emit dataChanged(index(i), index(i), {UpgradableRole, VersionRole});
    }
}

const Package* PackageModel::find(const QString& name) const {
    const auto found = m_index.constFind(name);
    return found == m_index.cend() ? nullptr : &m_packages.at(*found);
}

QVariantMap PackageModel::toMap(const Package& pkg) const {
    QVariantMap map;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        map.insert(QString::fromLatin1(it.value()), field(pkg, it.key()));
    }
    return map;
}

int PackageModel::upgradableCount() const {
    return static_cast<int>(std::ranges::count_if(m_packages, [](const Package& pkg) { return pkg.upgradable; }));
}

QStringList PackageModel::upgradableNames() const {
    QStringList names;
    for (const auto& pkg : m_packages) {
        if (pkg.upgradable) {
            names << pkg.name;
        }
    }
    return names;
}

PackageFilter::PackageFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &PackageFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PackageFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &PackageFilter::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &PackageFilter::countChanged);
}

const PackageModel* PackageFilter::packages() const {
    return qobject_cast<const PackageModel*>(sourceModel());
}

void PackageFilter::refilter() {
    invalidate();
    emit countChanged();
}

void PackageFilter::setQuery(const QString& query) {
    const auto trimmed = query.trimmed();
    if (trimmed == m_query) {
        return;
    }
    m_query = trimmed;
    emit queryChanged();
    refilter();
}

void PackageFilter::setCategory(const QString& category) {
    if (category == m_category) {
        return;
    }
    m_category = category;
    emit categoryChanged();
    refilter();
}

void PackageFilter::setMode(Mode mode) {
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    emit modeChanged();
    refilter();
}

/// 0 = no match; lower is better otherwise.
int PackageFilter::rank(const Package& pkg) const {
    if (m_query.isEmpty()) {
        return 1;
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
    const auto* model = packages();
    if (model == nullptr) {
        return false;
    }
    const auto& pkg = model->at(sourceRow);
    switch (m_mode) {
    case Apps:
        if (!pkg.isApp) {
            return false;
        }
        break;
    case Installed:
        if (!pkg.installed()) {
            return false;
        }
        break;
    case Updates:
        if (!pkg.upgradable) {
            return false;
        }
        break;
    case All:
        break;
    }
    if (!m_category.isEmpty() && !pkg.categories.contains(m_category)) {
        return false;
    }
    return rank(pkg) != 0;
}

bool PackageFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto* model = packages();
    const auto& a     = model->at(left.row());
    const auto& b     = model->at(right.row());
    if (!m_query.isEmpty()) {
        const int ra = rank(a);
        const int rb = rank(b);
        if (ra != rb) {
            return ra < rb;
        }
    }
    if (a.isApp != b.isApp) {
        return a.isApp;
    }
    return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
}
