// SPDX-License-Identifier: GPL-3.0-or-later
#include "packagelist.hpp"

#include <algorithm>
#include <numeric>
#include <optional>

namespace {

bool isAscii(QStringView text) {
    return std::ranges::all_of(text, [](QChar c) { return c.unicode() < 0x80; });
}

}  // namespace

PackageList::PackageList(QList<Package> packages) : m_packages(std::move(packages)) {
    m_index.reserve(m_packages.size());
    m_keys.reserve(m_packages.size());

    // TODO(vnepogodin): refactor that raw loop into concept type conv
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
}

auto PackageList::applyUpdates(const QHash<QString, QString>& newVersions) -> std::optional<std::pair<qsizetype, qsizetype>> {
    // aka vercmp
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
    if (first < 0) {
        return std::nullopt;
    }
    return std::pair{first, last};
}

const Package* PackageList::find(const QString& name) const {
    const auto found = m_index.constFind(name);
    return found == m_index.cend() ? nullptr : &m_packages.at(*found);
}

int32_t PackageList::upgradableCount() const {
    return static_cast<int32_t>(std::ranges::count_if(m_packages, &Package::upgradable));
}
