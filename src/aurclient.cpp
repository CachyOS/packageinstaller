// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurclient.hpp"
#include "memory.hpp"
#include "packagemodel.hpp"
#include "process.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr auto AUR_HOST = "https://aur.archlinux.org"_L1;
/// Names per info request, which keeps the address well below the limit of the AUR.
constexpr qsizetype INFO_BATCH = 100;
/// Packages a dependency plan may grow to before it is given up.
constexpr qsizetype MAX_PLAN = 60;
/// how long a checkout may be shown again for another package of the same recipe
constexpr qint64 REUSE_SECONDS = 30 * 60;

/// Commit a checkout is at, read from the repository itself.
QString headCommit(const QString& directory) {
    const auto read = [&](const QString& name) {
        QFile file(directory + u"/.git/"_s + name);
        return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.read(256)).trimmed() : QString();
    };
    auto head = read(u"HEAD"_s);
    static const QRegularExpression reference(u"^ref: (refs/heads/[A-Za-z0-9._-]+)$"_s);
    if (const auto match = reference.match(head); match.hasMatch()) {
        head = read(match.captured(1));
    }
    static const QRegularExpression hash(u"^[0-9a-f]{40,64}$"_s);
    return hash.match(head).hasMatch() ? head : QString();
}

struct TreeDigests {
    QByteArray digest;  ///< names, link targets and contents
    QByteArray stamp;   ///< names, link targets, sizes and modification times
};

/// Digests over every file of a checkout, without ".git". The commit alone does not show
/// whether a file was edited after the review. Without `contents` only the cheap stamp is
/// computed. Both are empty when the tree has grown beyond what a fresh clone holds, as after a build.
TreeDigests treeDigests(const QString& directory, bool contents) {
    constexpr qint64 MAX_BYTES = 16 * 1024 * 1024;
    constexpr qsizetype MAX_ENTRIES = 2000;

    QStringList paths;
    const QDir root(directory);
    for (const auto& entry : QDirListing(directory, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden)) {
        const auto relative = root.relativeFilePath(entry.filePath());
        if (relative == ".git"_L1 || relative.startsWith(".git/"_L1)) {
            continue;
        }
        paths << relative;
        if (paths.size() > MAX_ENTRIES) {
            return {};
        }
    }
    paths.sort();

    QCryptographicHash digest(QCryptographicHash::Sha256);
    QCryptographicHash stamp(QCryptographicHash::Sha256);
    const auto add = [&](QByteArrayView data) {
        digest.addData(data);
        stamp.addData(data);
    };
    qint64 total = 0;
    for (const auto& relative : std::as_const(paths)) {
        const QFileInfo info(root.filePath(relative));
        add(relative.toUtf8());
        add(QByteArrayView("\0", 1));
        if (info.isSymLink()) {
            add("link:");
            add(info.symLinkTarget().toUtf8());
        } else if (info.isDir()) {
            add("dir");
        } else if (total += info.size(); total > MAX_BYTES) {
            return {};
        } else {
            stamp.addData(QByteArray::number(info.size()) + ':' + QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
            if (QFile file(info.filePath()); contents && (!file.open(QIODevice::ReadOnly) || !digest.addData(&file))) {
                return {};
            }
        }
        add(QByteArrayView("\0", 1));
    }
    return {contents ? digest.result() : QByteArray(), stamp.result()};
}

/// "foo>=1.2" -> "foo"
QString dependName(QString depend) {
    static const QRegularExpression constraint(u"[<>=].*$"_s);
    return depend.remove(constraint);
}

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"cachyos-pi/" PI_VERSION ""_s);
    req.setTransferTimeout(20000);
    return req;
}

QUrl infoUrl(const QStringList& names) {
    QUrl url(AUR_HOST + "/rpc/v5/info"_L1);
    QUrlQuery query;
    for (const auto& name : names) {
        query.addQueryItem(u"arg[]"_s, QString::fromLatin1(QUrl::toPercentEncoding(name)));
    }
    url.setQuery(query);
    return url;
}

}  // namespace

AurClient::AurClient(const PackageModel* packages, QObject* parent) : QObject(parent), m_packages(packages) {
    // Checkouts only live as long as the window they were reviewed in. Each window has a
    // directory of its own and holds a lock for it, so that a second window does not delete
    // what the first one is building; whatever no running window holds is left over.
    const auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/aur"_s;
    QDir().mkpath(base);
    for (const auto& entry : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = base + u'/' + entry;
        QLockFile lock(path + u".lock"_s);
        lock.setStaleLockTime(0);  // only a lock whose process is gone counts as stale
        if (lock.tryLock(0)) {
            QDir(path).removeRecursively();
            lock.unlock();
        }
    }
    m_cacheRoot = base + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cacheLock = std::make_unique<QLockFile>(m_cacheRoot + u".lock"_s);
    m_cacheLock->setStaleLockTime(0);
    m_cacheLock->tryLock(0);
    QDir().mkpath(m_cacheRoot);
}

AurClient::~AurClient() {
    if (!m_keepCheckouts) {
        QDir(m_cacheRoot).removeRecursively();
    }
}

bool AurClient::intact(const Checkout& checkout, bool verifyContents) {
    const auto& expected = verifyContents ? checkout.digest : checkout.stamp;
    if (checkout.commit.isEmpty() || expected.isEmpty() || headCommit(checkout.directory) != checkout.commit) {
        return false;
    }
    const auto tree = treeDigests(checkout.directory, verifyContents);
    return (verifyContents ? tree.digest : tree.stamp) == expected;
}

void AurClient::setError(const QString& error) {
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

void AurClient::applyLocal(AurPackage& package) const {
    const auto* local        = m_packages->find(package.name);
    package.installed        = local != nullptr && local->installed();
    package.installedVersion = package.installed ? local->installedVersion : QString();
    package.upgradable       = package.installed && alpmdb::vercmp(package.installedVersion, package.version) < 0;
    package.inRepos          = local != nullptr && !local->foreign();
}

AurPackage AurClient::fromJson(const QJsonObject& object) const {
    AurPackage package{
        .name           = object.value("Name"_L1).toString(),
        .pkgbase        = object.value("PackageBase"_L1).toString(),
        .version        = object.value("Version"_L1).toString(),
        .description    = object.value("Description"_L1).toString(),
        .url            = object.value("URL"_L1).toString(),
        .maintainer     = object.value("Maintainer"_L1).toString(),
        .submitter      = object.value("Submitter"_L1).toString(),
        .firstSubmitted = object.value("FirstSubmitted"_L1).toInteger(),
        .votes          = object.value("NumVotes"_L1).toInt(),
        .popularity     = object.value("Popularity"_L1).toDouble(),
        .outOfDate      = !object.value("OutOfDate"_L1).isNull(),
        .lastModified   = object.value("LastModified"_L1).toInteger(),
        .licenses       = object.value("License"_L1).toVariant().toStringList(),
        .depends        = object.value("Depends"_L1).toVariant().toStringList(),
        .makeDepends    = object.value("MakeDepends"_L1).toVariant().toStringList(),
        .checkDepends   = object.value("CheckDepends"_L1).toVariant().toStringList(),
    };
    applyLocal(package);
    return package;
}

void AurClient::fetchInfo(const QStringList& names, std::function<void(QList<AurPackage>, bool)> done) {
    struct Lookup {
        qsizetype pending = 0;
        bool failed       = false;
        QList<AurPackage> found;
    };
    const auto lookup = std::make_shared<Lookup>();
    lookup->pending   = (names.size() + INFO_BATCH - 1) / INFO_BATCH;
    if (lookup->pending == 0) {
        done({}, false);
        return;
    }
    for (qsizetype first = 0; first < names.size(); first += INFO_BATCH) {
        auto* reply = m_network.get(request(infoUrl(names.mid(first, INFO_BATCH))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, lookup, done] {
            reply->deleteLater();
            // a captive portal or an error page can answer with status 200 as well
            const auto json    = QJsonDocument::fromJson(reply->readAll()).object();
            const auto results = json.value("results"_L1);
            lookup->failed     = lookup->failed || reply->error() != QNetworkReply::NoError
                || json.value("type"_L1).toString() != "multiinfo"_L1 || !results.isArray();
            for (const auto& item : results.toArray()) {
                lookup->found << fromJson(item.toObject());
            }
            if (--lookup->pending == 0) {
                done(std::move(lookup->found), lookup->failed);
            }
        });
    }
}

void AurClient::git(const QStringList& arguments, std::function<void(bool)> done) {
    static const auto environment = [] {
        auto env = plainEnvironment();
        for (const auto& [key, value] : std::to_array<std::pair<QString, QString>>({{u"GIT_CONFIG_NOSYSTEM"_s, u"1"_s},
                 {u"GIT_CONFIG_GLOBAL"_s, u"/dev/null"_s}, {u"GIT_TERMINAL_PROMPT"_s, u"0"_s},
                 {u"GIT_HTTP_LOW_SPEED_LIMIT"_s, u"1000"_s}, {u"GIT_HTTP_LOW_SPEED_TIME"_s, u"30"_s}})) {
            env.insert(key, value);
        }
        return env;
    }();
    auto* process = new QProcess(this);
    process->setProcessEnvironment(environment);
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {  // finished() is not emitted then
            process->deleteLater();
            done(false);
        }
    });
    connect(process, &QProcess::finished, this, [process, done](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        done(status == QProcess::NormalExit && exitCode == 0);
    });
    process->start(u"git"_s, QStringList{u"-c"_s, u"core.hooksPath=/dev/null"_s} + arguments);
}

void AurClient::search(const QString& query) {
    const auto term       = query.trimmed();
    const int generation  = ++m_generation;
    const bool wasSearching = std::exchange(m_searching, term.size() >= 2);
    setError({});
    if (wasSearching != m_searching) {
        emit searchingChanged();
    }
    if (!m_searching) {
        m_results.clear();
        emit resultsChanged();
        return;
    }

    QUrl url(AUR_HOST + "/rpc/v5/search/"_L1 + QString::fromLatin1(QUrl::toPercentEncoding(term)));
    url.setQuery(u"by=name-desc"_s);
    auto* reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, term = term.toLower()] {
        reply->deleteLater();
        if (generation != m_generation) {
            return;  // superseded by a newer search
        }
        m_searching = false;
        emit searchingChanged();

        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        QList<AurPackage> packages;
        if (reply->error() != QNetworkReply::NoError && json.isEmpty()) {
            setError(tr("Could not reach the AUR: %1").arg(reply->errorString()));
        } else if (json.value("type"_L1).toString() == "error"_L1) {
            setError(json.value("error"_L1).toString());
        } else {
            for (const auto& item : json.value("results"_L1).toArray()) {
                packages << fromJson(item.toObject());
            }
            std::ranges::sort(packages, std::ranges::greater{}, &AurPackage::popularity);
            // the package that is called what was typed comes first
            std::ranges::stable_partition(packages, [&term](const AurPackage& package) { return package.name == term; });
        }
        m_results = std::move(packages);
        emit resultsChanged();
    });
}

void AurClient::loadDetails(const QString& name, const QString& pkgbase) {
    const int generation = ++m_detailsGeneration;
    m_details = {};
    m_pkgbuild.clear();
    m_revision.clear();
    m_files.clear();
    m_findings.clear();
    m_analysis = {};
    m_unreadable.clear();
    m_lookAlike.clear();
    emit detailsChanged();
    emit pkgbuildChanged();
    emit filesChanged();
    emit findingsChanged();

    const auto finishOne = [this] {
        --m_pending;
        emit loadingDetailsChanged();
    };
    m_pending += 2;
    emit loadingDetailsChanged();

    fetchInfo({name}, [this, generation, finishOne](const QList<AurPackage>& found, bool) {
        finishOne();
        if (generation != m_detailsGeneration || found.isEmpty()) {
            return;
        }
        m_details = found.first();
        emit detailsChanged();
        m_lookAlike = pkgscan::similarName(m_details.name, m_repoNames);
        rescan();
    });

    // A fresh clone per review: everything shown comes from this one commit, and this
    // directory is what gets built. Cloning runs none of the code in the repository.
    if (!pkgscan::validPkgbase(pkgbase)) {
        finishOne();
        return;
    }
    m_pkgbaseOf.insert(name, pkgbase);

    // Packages built from one recipe share one checkout. When another of them was accepted
    // a moment ago, show that same checkout: a new clone would invalidate the acceptance,
    // and the two could then never be built together.
    const auto existing = m_checkouts.value(pkgbase);
    const bool sibling  = std::ranges::any_of(m_accepted.asKeyValueRange(), [&](const auto& entry) {
        const auto& [other, commit] = entry;
        return other != name && m_pkgbaseOf.value(other) == pkgbase && commit == existing.commit;
    });
    if (sibling && existing.cloned.secsTo(QDateTime::currentDateTimeUtc()) < REUSE_SECONDS && intact(existing)) {
        m_revision = existing.commit;
        readCheckout(existing.directory);
        finishOne();
        return;
    }
    m_checkouts.remove(pkgbase);
    m_accepted.removeIf([&](const auto& entry) { return m_pkgbaseOf.value(entry.key()) == pkgbase; });

    const auto directory = u"%1/%2/%3"_s.arg(m_cacheRoot).arg(++m_cloneCount).arg(pkgbase);
    QDir().mkpath(QFileInfo(directory).path());

    const auto fail = [directory, finishOne] {
        QDir(directory).removeRecursively();
        finishOne();
    };
    git({u"clone"_s, u"--quiet"_s, u"--depth=1"_s, u"--no-tags"_s, u"--"_s, u"%1/%2.git"_s.arg(AUR_HOST, pkgbase), directory},
        [this, pkgbase, directory, generation, finishOne, fail](bool cloned) {
            if (!cloned || generation != m_detailsGeneration) {
                fail();
                return;
            }
            // nothing may move the checkout to another commit between the last check and the build
            git({u"-C"_s, directory, u"remote"_s, u"remove"_s, u"origin"_s}, [this, pkgbase, directory, generation, finishOne, fail](bool detached) {
                const auto commit = headCommit(directory);
                auto tree         = treeDigests(directory, true);
                if (!detached || generation != m_detailsGeneration || commit.isEmpty() || tree.digest.isEmpty()) {
                    fail();
                    return;
                }
                m_checkouts.insert(pkgbase, {directory, commit, std::move(tree.digest), std::move(tree.stamp), QDateTime::currentDateTimeUtc()});
                m_revision = commit;
                readCheckout(directory);
                finishOne();
            });
        });
}

void AurClient::readCheckout(const QString& directory) {
    auto pkgbuild = pkgscan::readText(directory + u"/PKGBUILD"_s);
    if (!pkgbuild) {
        m_pkgbuild.clear();
        m_revision.clear();
        emit pkgbuildChanged();
        return;
    }
    m_pkgbuild = *std::move(pkgbuild);
    // makepkg runs the PKGBUILD, but the AUR web page shows the sources from .SRCINFO
    m_analysis = pkgscan::scanPkgbuild(m_pkgbuild, pkgscan::readText(directory + u"/.SRCINFO"_s).value_or(QString()));
    auto local   = pkgscan::collectFiles(QDir(directory), m_analysis);
    m_unreadable = std::move(local.unreadable);
    m_files      = std::move(local.files);
    emit pkgbuildChanged();
    emit filesChanged();
    rescan();
}

void AurClient::resolve(const QStringList& names) {
    const int generation = ++m_planGeneration;
    m_plan.clear();
    m_problems.clear();
    m_planInfo.clear();
    m_needs.clear();
    m_unresolved.clear();
    m_planTargets = names;
    m_resolving   = !names.isEmpty();
    emit planChanged();
    if (m_resolving) {
        resolveNext(names, {}, generation);
    }
}

void AurClient::resolveNext(const QStringList& wanted, const QHash<QString, QStringList>& wantedBy, int generation) {
    fetchInfo(wanted, [this, wanted, wantedBy, generation](const QList<AurPackage>& found, bool failed) {
        if (generation != m_planGeneration) {
            return;
        }
        if (failed) {
            m_problems << tr("The AUR could not be asked about dependencies, so nothing can be built right now.");
            // nothing is known about these packages, which blocks them in blockedReason()
            m_planInfo.clear();
            finishResolve();
            return;
        }
        QHash<QString, QStringList> dependsOf;
        QSet<QString> all;
        for (const auto& package : found) {
            m_planInfo.insert(package.name, package);
            m_pkgbaseOf.insert(package.name, package.pkgbase);
            auto& depends = dependsOf[package.name];
            depends << package.depends << package.makeDepends << package.checkDepends;
            all.unite(QSet<QString>(depends.cbegin(), depends.cend()));
        }
        // asked for, but not a package in the AUR
        for (const auto& name : wanted) {
            if (!m_planInfo.contains(name)) {
                for (const auto& package : wantedBy.value(name, {name})) {
                    m_unresolved[package] << name;
                }
            }
        }

        QtConcurrent::run(alpmdb::unsatisfied, all.values()).then(this, [this, dependsOf, generation](const QStringList& result) {
            releaseFreedMemory();  // the databases read for the check
            if (generation != m_planGeneration) {
                return;
            }
            const QSet<QString> missing(result.cbegin(), result.cend());
            QHash<QString, QStringList> next;
            for (const auto& [package, depends] : dependsOf.asKeyValueRange()) {
                for (const auto& depend : depends) {
                    const auto name = dependName(depend);
                    if (!missing.contains(depend) || name == package) {
                        continue;
                    }
                    if (auto& needs = m_needs[package]; !needs.contains(name)) {
                        needs << name;
                    }
                    if (!m_planInfo.contains(name) && !next[name].contains(package)) {
                        next[name] << package;
                    }
                }
            }
            if (next.isEmpty()) {
                finishResolve();
            } else if (m_planInfo.size() + next.size() > MAX_PLAN) {
                m_problems << tr("These packages need more than %1 other AUR packages, which is too many to review here.").arg(MAX_PLAN);
                m_planInfo.clear();
                finishResolve();
            } else {
                resolveNext(next.keys(), next, generation);
            }
        });
    });
}

void AurClient::finishResolve() {
    m_resolving = false;
    auto names  = m_planInfo.keys();
    names.sort();
    for (const auto& name : std::as_const(names)) {
        if (m_planTargets.contains(name)) {
            continue;
        }
        auto package = m_planInfo.value(name);
        for (const auto& [other, needs] : m_needs.asKeyValueRange()) {
            if (needs.contains(name)) {
                package.neededBy << other;
            }
        }
        package.neededBy.sort();
        m_plan << std::move(package);
    }
    auto packages = m_unresolved.keys();
    packages.sort();
    for (const auto& package : std::as_const(packages)) {
        const auto& depends = m_unresolved[package];
        m_problems << (depends == QStringList{package}
                ? tr("%1 is not a package in the AUR.").arg(package)
                : tr("%1 needs %2, which is neither in the repositories nor a package in the AUR.").arg(package, depends.join(", "_L1)));
    }
    emit planChanged();
}

void AurClient::accept(const QString& name) {
    const auto pkgbase  = m_pkgbaseOf.value(name);
    const auto checkout = m_checkouts.value(pkgbase);
    // only what is on screen right now can have been read
    if (m_pending == 0 && !checkout.commit.isEmpty() && checkout.commit == m_revision && !m_pkgbuild.isEmpty()
        && m_details.pkgbase == pkgbase) {
        m_accepted.insert(name, checkout.commit);
    }
}

QString AurClient::blockedReason(const QStringList& names, bool verifyContents) const {
    if (m_resolving) {
        return tr("The dependencies are still being worked out.");
    }
    for (const auto& name : names) {
        if (!m_planInfo.contains(name)) {
            return m_problems.isEmpty() ? tr("The dependencies of %1 are not known.").arg(name) : m_problems.first();
        }
        if (const auto unresolved = m_unresolved.constFind(name); unresolved != m_unresolved.cend()) {
            return tr("%1 needs %2, which is neither in the repositories nor a package in the AUR.").arg(name, unresolved->join(", "_L1));
        }
        // every AUR package it needs, directly or not, has to be part of the build
        QStringList open = m_needs.value(name);
        QSet<QString> seen;
        while (!open.isEmpty()) {
            const auto depend = open.takeLast();
            if (seen.contains(depend)) {
                continue;
            }
            seen << depend;
            if (!names.contains(depend)) {
                return tr("%1 needs the AUR package %2, which was not reviewed and accepted.").arg(name, depend);
            }
            open += m_needs.value(depend);
        }
        const auto checkout = m_checkouts.value(m_pkgbaseOf.value(name));
        // the commit and every file have to be what was on screen during the review
        if (checkout.commit != m_accepted.value(name) || !intact(checkout, verifyContents)) {
            return tr("%1 has to be reviewed again.").arg(name);
        }
    }
    return {};
}

std::expected<QStringList, QString> AurClient::buildDirectories(const QStringList& names) const {
    if (auto reason = blockedReason(names, true); !reason.isEmpty()) {
        return std::unexpected(std::move(reason));
    }
    QStringList directories;
    for (const auto& name : names) {
        directories << m_checkouts.value(m_pkgbaseOf.value(name)).directory;
    }
    directories.removeDuplicates();
    return directories;
}

QString AurClient::buildWorkingDirectory() const {
    const auto directory = m_cacheRoot + u"/empty"_s;
    QDir(directory).removeRecursively();
    QDir().mkpath(directory);
    return directory;
}

void AurClient::rescan() {
    auto findings = m_analysis.findings;
    for (const auto& file : std::as_const(m_files)) {
        findings += pkgscan::scanScript(file.text, file.name);
    }
    for (const auto& name : std::as_const(m_unreadable)) {
        findings << pkgscan::Finding{u"unreadable-file"_s, pkgscan::Severity::Warning, {}, 0,
            tr("“%1” could not be checked").arg(name),
            tr("This file is part of the package and may run on your machine, but it could not be loaded for review."), {}};
    }
    // the search result lacks the submission details, wait for the full record
    if (m_details.firstSubmitted > 0) {
        findings += pkgscan::scanMetadata({m_details.name, m_details.maintainer, m_details.submitter, m_details.votes,
                                              m_details.popularity, m_details.firstSubmitted, m_details.lastModified},
            QDateTime::currentDateTimeUtc());
    }
    if (!m_lookAlike.isEmpty()) {
        findings << pkgscan::Finding{u"look-alike"_s, pkgscan::Severity::Warning, {}, 0,
            tr("The name is almost the same as “%1”").arg(m_lookAlike),
            tr("“%1” is a package in the repositories. Make sure that this is not an imitation with a misspelled name.").arg(m_lookAlike), {}};
    }
    pkgscan::sort(findings);
    m_findings = std::move(findings);
    emit findingsChanged();
}

void AurClient::checkUpdates() {
    // packages held back in pacman.conf are not offered, like `pacman -Syu` and paru do
    const auto names = m_packages->packages()
        | std::views::filter([](const Package& package) { return package.foreign() && package.installed() && !package.ignored; })
        | std::views::transform(&Package::name) | std::ranges::to<QStringList>();

    const int generation = ++m_updatesGeneration;
    if (names.isEmpty()) {
        m_updates.clear();
        m_installedCount = 0;
        emit updatesChanged();
        return;
    }
    if (m_updateChecks++ == 0) {
        emit checkingUpdatesChanged();
    }
    fetchInfo(names, [this, generation](QList<AurPackage> found, bool failed) {
        // a check that could not reach the AUR keeps the previous list
        if (generation == m_updatesGeneration && !failed) {
            m_installedCount = static_cast<int>(found.size());
            // installed packages that moved into a repository are updated from there
            found.removeIf([](const AurPackage& package) { return !package.upgradable || package.inRepos; });
            std::ranges::sort(found, {}, &AurPackage::name);
            m_updates = std::move(found);
            emit updatesChanged();
        }
        if (--m_updateChecks == 0) {
            emit checkingUpdatesChanged();
        }
    });
}

void AurClient::clearUpdates() {
    ++m_updatesGeneration;
    m_updates.clear();
    m_installedCount = -1;
    emit updatesChanged();
}

void AurClient::refreshInstalled() {
    m_repoNames = m_packages->packages() | std::views::filter([](const Package& package) { return !package.foreign(); })
        | std::views::transform(&Package::name) | std::ranges::to<QStringList>();

    const auto refresh = [this](AurPackage& package) {
        const auto before = package;
        applyLocal(package);
        return package != before;
    };
    bool changed = false;
    for (auto& package : m_results) {
        changed |= refresh(package);
    }
    if (changed) {
        emit resultsChanged();
    }
    if (!m_details.name.isEmpty() && refresh(m_details)) {
        emit detailsChanged();
    }
}
