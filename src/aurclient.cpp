// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurclient.hpp"
#include "packagemodel.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace {

constexpr auto AUR_HOST = "https://aur.archlinux.org";
/// Scripts next to the PKGBUILD that are fetched for review.
constexpr int MAX_FILES         = 8;
constexpr qint64 MAX_FILE_SIZE = 512 * 1024;
/// Names per info request of the update check; keeps the address well below the limit of the AUR.
constexpr qsizetype INFO_BATCH = 100;
/// Packages a dependency plan may grow to before it is given up.
constexpr qsizetype MAX_PLAN = 60;
/// how long a checkout may be shown again for another package of the same recipe
constexpr qint64 REUSE_SECONDS = 30 * 60;

/// Package bases become directory names and part of an address.
bool isPackageBase(const QString& name) {
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9@_+][a-z0-9@._+-]*$"));
    return name.size() <= 128 && valid.match(name).hasMatch();
}

/// Commit a checkout is at, read from the repository itself.
QString headCommit(const QString& directory) {
    const auto read = [&](const QString& name) {
        QFile file(directory + QStringLiteral("/.git/") + name);
        return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.read(256)).trimmed() : QString();
    };
    auto head = read(QStringLiteral("HEAD"));
    static const QRegularExpression reference(QStringLiteral("^ref: (refs/heads/[A-Za-z0-9._-]+)$"));
    if (const auto match = reference.match(head); match.hasMatch()) {
        head = read(match.captured(1));
    }
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{40,64}$"));
    return hash.match(head).hasMatch() ? head : QString();
}

/// A digest over every file of a checkout (names, link targets and contents), without
/// ".git". The commit alone does not show whether a file was edited after the review.
/// Empty when the tree has grown beyond what a fresh clone holds, as after a build.
QByteArray treeDigest(const QString& directory) {
    constexpr qint64 MAX_BYTES = 16 * 1024 * 1024;
    constexpr int MAX_ENTRIES  = 2000;

    QStringList paths;
    QDirIterator walk(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir root(directory);
    while (walk.hasNext()) {
        const auto relative = root.relativeFilePath(walk.next());
        if (relative == QLatin1String(".git") || relative.startsWith(QLatin1String(".git/"))) {
            continue;
        }
        paths << relative;
        if (paths.size() > MAX_ENTRIES) {
            return {};
        }
    }
    paths.sort();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    for (const auto& relative : std::as_const(paths)) {
        const QFileInfo info(root.filePath(relative));
        hash.addData(relative.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        if (info.isSymLink()) {
            hash.addData("link:");
            hash.addData(info.symLinkTarget().toUtf8());
        } else if (info.isDir()) {
            hash.addData("dir");
        } else {
            QFile file(info.filePath());
            total += info.size();
            if (total > MAX_BYTES || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
                return {};
            }
        }
        hash.addData(QByteArrayView("\0", 1));
    }
    return hash.result();
}

/// Text of a regular file inside a checkout; a link could point anywhere on the machine.
bool readText(const QString& path, QString* text) {
    const QFileInfo info(path);
    QFile file(path);
    if (info.isSymLink() || !info.isFile() || info.size() > MAX_FILE_SIZE || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    *text = QString::fromUtf8(file.readAll());
    return true;
}

/// "foo>=1.2" -> "foo"
QString dependName(const QString& depend) {
    static const QRegularExpression constraint(QStringLiteral("[<>=].*$"));
    auto name = depend;
    name.remove(constraint);
    return name;
}

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("cachyos-pi/" PI_VERSION));
    req.setTransferTimeout(20000);
    return req;
}

QStringList toStringList(const QJsonValue& value) {
    QStringList out;
    for (const auto& item : value.toArray()) {
        out << item.toString();
    }
    return out;
}

}  // namespace

AurClient::AurClient(const PackageModel* packages, QObject* parent) : QObject(parent), m_packages(packages) {
    // Checkouts only live as long as the window they were reviewed in. Each window has a
    // directory of its own and holds a lock for it, so that a second window does not delete
    // what the first one is building; whatever no running window holds is left over.
    const auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/aur");
    QDir().mkpath(base);
    const auto entries = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        const auto path = base + u'/' + entry;
        QLockFile lock(path + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);  // only a lock whose process is gone counts as stale
        if (lock.tryLock(0)) {
            QDir(path).removeRecursively();
            lock.unlock();
        }
    }
    m_cacheRoot = base + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cacheLock = std::make_unique<QLockFile>(m_cacheRoot + QStringLiteral(".lock"));
    m_cacheLock->setStaleLockTime(0);
    m_cacheLock->tryLock(0);
    QDir().mkpath(m_cacheRoot);
}

AurClient::~AurClient() {
    QDir(m_cacheRoot).removeRecursively();
}

bool AurClient::intact(const Checkout& checkout) const {
    return !checkout.commit.isEmpty() && !checkout.digest.isEmpty() && headCommit(checkout.directory) == checkout.commit
        && treeDigest(checkout.directory) == checkout.digest;
}

void AurClient::setError(const QString& error) {
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

QVariantMap AurClient::toMap(const QJsonObject& object) const {
    const auto name     = object.value(QLatin1String("Name")).toString();
    const auto* local   = m_packages->find(name);
    const auto version  = object.value(QLatin1String("Version")).toString();
    const bool installed = local != nullptr && local->installed();
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("pkgbase"), object.value(QLatin1String("PackageBase")).toString()},
        {QStringLiteral("version"), version},
        {QStringLiteral("description"), object.value(QLatin1String("Description")).toString()},
        {QStringLiteral("url"), object.value(QLatin1String("URL")).toString()},
        {QStringLiteral("maintainer"), object.value(QLatin1String("Maintainer")).toString()},
        {QStringLiteral("submitter"), object.value(QLatin1String("Submitter")).toString()},
        {QStringLiteral("firstSubmitted"), object.value(QLatin1String("FirstSubmitted")).toInteger()},
        {QStringLiteral("votes"), object.value(QLatin1String("NumVotes")).toInt()},
        {QStringLiteral("popularity"), object.value(QLatin1String("Popularity")).toDouble()},
        {QStringLiteral("outOfDate"), !object.value(QLatin1String("OutOfDate")).isNull()},
        {QStringLiteral("lastModified"), object.value(QLatin1String("LastModified")).toInteger()},
        {QStringLiteral("licenses"), toStringList(object.value(QLatin1String("License")))},
        {QStringLiteral("depends"), toStringList(object.value(QLatin1String("Depends")))},
        {QStringLiteral("makeDepends"), toStringList(object.value(QLatin1String("MakeDepends")))},
        {QStringLiteral("checkDepends"), toStringList(object.value(QLatin1String("CheckDepends")))},
        {QStringLiteral("installed"), installed},
        {QStringLiteral("installedVersion"), installed ? local->installedVersion : QString()},
        {QStringLiteral("upgradable"), installed && alpmdb::vercmp(local->installedVersion, version) < 0},
        // a package of the same name in the repositories should be installed from there instead
        {QStringLiteral("inRepos"), local != nullptr && !local->foreign()},
    };
}

void AurClient::search(const QString& query) {
    const auto term = query.trimmed();
    const int generation = ++m_generation;
    setError({});
    if (term.size() < 2) {
        m_results.clear();
        emit resultsChanged();
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        return;
    }

    QUrl url(QStringLiteral("%1/rpc/v5/search/%2").arg(QLatin1String(AUR_HOST), QString::fromLatin1(QUrl::toPercentEncoding(term))));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("by"), QStringLiteral("name-desc"));
    url.setQuery(urlQuery);

    m_searching = true;
    emit searchingChanged();

    auto* reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) {
            return;  // superseded by a newer search
        }
        m_searching = false;
        emit searchingChanged();

        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError && json.isEmpty()) {
            setError(tr("Could not reach the AUR: %1").arg(reply->errorString()));
            return;
        }
        if (json.value(QLatin1String("type")).toString() == QLatin1String("error")) {
            setError(json.value(QLatin1String("error")).toString());
            m_results.clear();
            emit resultsChanged();
            return;
        }

        QList<QVariantMap> maps;
        for (const auto& item : json.value(QLatin1String("results")).toArray()) {
            maps << toMap(item.toObject());
        }
        std::ranges::sort(maps, [](const QVariantMap& a, const QVariantMap& b) {
            return a.value(QStringLiteral("popularity")).toDouble() > b.value(QStringLiteral("popularity")).toDouble();
        });
        m_results.clear();
        for (const auto& map : std::as_const(maps)) {
            m_results << map;
        }
        emit resultsChanged();
    });
}

void AurClient::loadDetails(const QString& name, const QString& pkgbase) {
    const int generation = ++m_detailsGeneration;
    m_details.clear();
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

    QUrl infoUrl(QStringLiteral("%1/rpc/v5/info").arg(QLatin1String(AUR_HOST)));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("arg[]"), QString::fromLatin1(QUrl::toPercentEncoding(name)));
    infoUrl.setQuery(infoQuery);

    m_pending += 2;
    emit loadingDetailsChanged();

    auto* info = m_network.get(request(infoUrl));
    connect(info, &QNetworkReply::finished, this, [this, info, generation, finishOne] {
        info->deleteLater();
        finishOne();
        if (generation != m_detailsGeneration) {
            return;
        }
        const auto results = QJsonDocument::fromJson(info->readAll()).object().value(QLatin1String("results")).toArray();
        if (!results.isEmpty()) {
            m_details = toMap(results.first().toObject());
            emit detailsChanged();

            QStringList known;
            for (int row = 0; row < m_packages->rowCount(); ++row) {
                if (!m_packages->at(row).foreign()) {
                    known << m_packages->at(row).name;
                }
            }
            m_lookAlike = pkgscan::similarName(m_details.value(QStringLiteral("name")).toString(), known);
            rescan();
        }
    });

    // A fresh clone per review: everything shown comes from this one commit, and this
    // directory is what gets built. Cloning runs none of the code in the repository.
    if (!isPackageBase(pkgbase)) {
        finishOne();
        return;
    }
    m_pkgbaseOf.insert(name, pkgbase);

    // Packages built from one recipe share one checkout. When another of them was accepted
    // a moment ago, show that same checkout: a new clone would invalidate the acceptance,
    // and the two could then never be built together.
    const auto existing = m_checkouts.value(pkgbase);
    const bool sibling  = std::ranges::any_of(m_accepted.keys(), [&](const QString& other) {
        return other != name && m_pkgbaseOf.value(other) == pkgbase && m_accepted.value(other) == existing.commit;
    });
    if (sibling && existing.cloned.secsTo(QDateTime::currentDateTimeUtc()) < REUSE_SECONDS && intact(existing)) {
        m_revision = existing.commit;
        readCheckout(existing.directory);
        finishOne();
        return;
    }
    m_checkouts.remove(pkgbase);
    m_accepted.removeIf([&](const auto& entry) { return m_pkgbaseOf.value(entry.key()) == pkgbase; });

    const auto directory = QStringLiteral("%1/%2/%3").arg(m_cacheRoot).arg(++m_cloneCount).arg(pkgbase);
    QDir().mkpath(QFileInfo(directory).path());

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));

    auto* clone = new QProcess(this);
    clone->setProcessEnvironment(env);
    clone->setProcessChannelMode(QProcess::MergedChannels);
    connect(clone, &QProcess::errorOccurred, this, [clone, finishOne](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {  // git is missing; finished() is not emitted then
            clone->deleteLater();
            finishOne();
        }
    });
    connect(clone, &QProcess::finished, this, [this, clone, env, pkgbase, directory, generation, finishOne](int exitCode, QProcess::ExitStatus status) {
        clone->deleteLater();
        if (generation != m_detailsGeneration || status != QProcess::NormalExit || exitCode != 0) {
            QDir(directory).removeRecursively();
            finishOne();
            return;
        }
        // nothing may move the checkout to another commit after the review
        auto* detach = new QProcess(this);
        detach->setProcessEnvironment(env);
        connect(detach, &QProcess::finished, this, [this, detach, pkgbase, directory, generation, finishOne] {
            detach->deleteLater();
            const auto commit = headCommit(directory);
            const auto digest = treeDigest(directory);
            if (generation == m_detailsGeneration && !commit.isEmpty() && !digest.isEmpty()) {
                m_checkouts.insert(pkgbase, {directory, commit, digest, QDateTime::currentDateTimeUtc()});
                m_revision = commit;
                readCheckout(directory);
            }
            finishOne();
        });
        detach->start(QStringLiteral("git"), {QStringLiteral("-C"), directory, QStringLiteral("remote"), QStringLiteral("remove"), QStringLiteral("origin")});
    });
    clone->start(QStringLiteral("git"),
        {QStringLiteral("-c"), QStringLiteral("core.hooksPath=/dev/null"), QStringLiteral("clone"), QStringLiteral("--quiet"),
            QStringLiteral("--depth=1"), QStringLiteral("--no-tags"), QStringLiteral("--"),
            QStringLiteral("%1/%2.git").arg(QLatin1String(AUR_HOST), pkgbase), directory});
}

void AurClient::readCheckout(const QString& directory) {
    if (!readText(directory + QStringLiteral("/PKGBUILD"), &m_pkgbuild)) {
        m_pkgbuild.clear();
        m_revision.clear();
        emit pkgbuildChanged();
        return;
    }
    m_analysis = pkgscan::scanPkgbuild(m_pkgbuild);

    // a source without a telling name is still a script when it starts with "#!"
    for (const auto& name : std::as_const(m_analysis.otherLocalFiles)) {
        QFile file(directory + u'/' + name);
        if (!QFileInfo(file).isSymLink() && file.open(QIODevice::ReadOnly) && file.read(2) == "#!") {
            m_analysis.localFiles << name;
        }
    }

    const auto count = qMin(m_analysis.localFiles.size(), qsizetype{MAX_FILES});
    m_unreadable     = m_analysis.localFiles.mid(count);
    for (const auto& name : m_analysis.localFiles.mid(0, count)) {
        QString text;
        if (readText(directory + u'/' + name, &text)) {
            m_files << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("text"), text}};
        } else {
            m_unreadable << name;
        }
    }
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
    // the AUR answers for a limited number of names at once, so ask in batches
    struct Lookup {
        int pending = 0;
        bool failed = false;
        QHash<QString, QStringList> dependsOf;
        QSet<QString> all;
    };
    const auto lookup = std::make_shared<Lookup>();

    for (qsizetype first = 0; first < wanted.size(); first += INFO_BATCH) {
        QUrl url(QStringLiteral("%1/rpc/v5/info").arg(QLatin1String(AUR_HOST)));
        QUrlQuery query;
        for (const auto& name : wanted.mid(first, INFO_BATCH)) {
            query.addQueryItem(QStringLiteral("arg[]"), QString::fromLatin1(QUrl::toPercentEncoding(name)));
        }
        url.setQuery(query);

        ++lookup->pending;
        auto* reply = m_network.get(request(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, lookup, wanted, wantedBy, generation] {
            reply->deleteLater();
            if (generation != m_planGeneration) {
                return;
            }
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError || json.value(QLatin1String("type")).toString() != QLatin1String("multiinfo")
                || !json.value(QLatin1String("results")).isArray()) {
                lookup->failed = true;
            }
            for (const auto& item : json.value(QLatin1String("results")).toArray()) {
                const auto map  = toMap(item.toObject());
                const auto name = map.value(QStringLiteral("name")).toString();
                m_planInfo.insert(name, map);
                m_pkgbaseOf.insert(name, map.value(QStringLiteral("pkgbase")).toString());
                auto& depends = lookup->dependsOf[name];
                for (const char* key : {"depends", "makeDepends", "checkDepends"}) {
                    depends += map.value(QLatin1String(key)).toStringList();
                }
                lookup->all += QSet<QString>(depends.cbegin(), depends.cend());
            }
            if (--lookup->pending > 0) {
                return;
            }
            if (lookup->failed) {
                m_problems << tr("The AUR could not be asked about dependencies, so nothing can be built right now.");
                // nothing is known about these packages, which blocks them in blockedReason()
                m_planInfo.clear();
                finishResolve();
                return;
            }
            const auto dependsOf = lookup->dependsOf;
            const auto all       = lookup->all;
            // asked for, but not a package in the AUR
            for (const auto& name : wanted) {
                if (!m_planInfo.contains(name)) {
                    for (const auto& package : wantedBy.value(name, {name})) {
                        m_unresolved[package] << name;
                    }
                }
            }

            auto* watcher = new QFutureWatcher<QStringList>(this);
            connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, dependsOf, generation] {
                watcher->deleteLater();
                if (generation != m_planGeneration) {
                    return;
                }
                const auto result = watcher->result();
                const QSet<QString> missing(result.cbegin(), result.cend());
                QHash<QString, QStringList> next;
                for (auto it = dependsOf.cbegin(); it != dependsOf.cend(); ++it) {
                    for (const auto& depend : it.value()) {
                        const auto name = dependName(depend);
                        if (!missing.contains(depend) || name == it.key()) {
                            continue;
                        }
                        if (!m_needs[it.key()].contains(name)) {
                            m_needs[it.key()] << name;
                        }
                        if (!m_planInfo.contains(name) && !next[name].contains(it.key())) {
                            next[name] << it.key();
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
            watcher->setFuture(QtConcurrent::run(alpmdb::unsatisfied, QStringList(all.cbegin(), all.cend())));
        });
    }
}

void AurClient::finishResolve() {
    m_resolving = false;
    auto names  = m_planInfo.keys();
    names.sort();
    for (const auto& name : std::as_const(names)) {
        if (m_planTargets.contains(name)) {
            continue;
        }
        QStringList neededBy;
        for (auto it = m_needs.cbegin(); it != m_needs.cend(); ++it) {
            if (it.value().contains(name)) {
                neededBy << it.key();
            }
        }
        neededBy.sort();
        auto map = m_planInfo.value(name);
        map.insert(QStringLiteral("neededBy"), neededBy);
        m_plan << map;
    }
    auto packages = m_unresolved.keys();
    packages.sort();
    for (const auto& package : std::as_const(packages)) {
        const auto& depends = m_unresolved[package];
        m_problems << (depends == QStringList{package}
                ? tr("%1 is not a package in the AUR.").arg(package)
                : tr("%1 needs %2, which is neither in the repositories nor a package in the AUR.").arg(package, depends.join(QStringLiteral(", "))));
    }
    emit planChanged();
}

void AurClient::accept(const QString& name) {
    const auto checkout = m_checkouts.value(m_pkgbaseOf.value(name));
    // only what is on screen right now can have been read
    if (m_pending == 0 && !checkout.commit.isEmpty() && checkout.commit == m_revision && !m_pkgbuild.isEmpty()
        && m_details.value(QStringLiteral("pkgbase")).toString() == m_pkgbaseOf.value(name)) {
        m_accepted.insert(name, checkout.commit);
    }
}

QString AurClient::blockedReason(const QStringList& names) const {
    if (m_resolving) {
        return tr("The dependencies are still being worked out.");
    }
    for (const auto& name : names) {
        if (!m_planInfo.contains(name)) {
            return m_problems.isEmpty() ? tr("The dependencies of %1 are not known.").arg(name) : m_problems.first();
        }
        if (m_unresolved.contains(name)) {
            return tr("%1 needs %2, which is neither in the repositories nor a package in the AUR.").arg(name, m_unresolved.value(name).join(QStringLiteral(", ")));
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
        const auto accepted = m_accepted.value(name);
        // the commit and every file have to be what was on screen during the review
        if (accepted.isEmpty() || checkout.commit != accepted || !intact(checkout)) {
            return tr("%1 has to be reviewed again.").arg(name);
        }
    }
    return {};
}

QStringList AurClient::buildDirectories(const QStringList& names) const {
    QStringList directories;
    if (names.isEmpty() || !blockedReason(names).isEmpty()) {
        return directories;
    }
    for (const auto& name : names) {
        const auto directory = m_checkouts.value(m_pkgbaseOf.value(name)).directory;
        if (!directories.contains(directory)) {
            directories << directory;
        }
    }
    return directories;
}

QString AurClient::buildWorkingDirectory() const {
    const auto directory = m_cacheRoot + QStringLiteral("/empty");
    QDir(directory).removeRecursively();
    QDir().mkpath(directory);
    return directory;
}

void AurClient::rescan() {
    auto findings = m_analysis.findings;
    for (const auto& item : std::as_const(m_files)) {
        const auto file = item.toMap();
        findings += pkgscan::scanScript(file.value(QStringLiteral("text")).toString(), file.value(QStringLiteral("name")).toString());
    }
    for (const auto& name : std::as_const(m_unreadable)) {
        findings << pkgscan::Finding{QStringLiteral("unreadable-file"), pkgscan::Severity::Warning, {}, 0,
            tr("“%1” could not be checked").arg(name),
            tr("This file is part of the package and may run on your machine, but it could not be loaded for review."), {}};
    }
    // the search result lacks the submission details, wait for the full record
    if (m_details.contains(QStringLiteral("firstSubmitted")) && m_details.value(QStringLiteral("firstSubmitted")).toLongLong() > 0) {
        findings += pkgscan::scanMetadata(
            {m_details.value(QStringLiteral("name")).toString(),
                m_details.value(QStringLiteral("maintainer")).toString(),
                m_details.value(QStringLiteral("submitter")).toString(),
                m_details.value(QStringLiteral("votes")).toInt(),
                m_details.value(QStringLiteral("popularity")).toDouble(),
                m_details.value(QStringLiteral("firstSubmitted")).toLongLong(),
                m_details.value(QStringLiteral("lastModified")).toLongLong()},
            QDateTime::currentDateTimeUtc());
    }
    if (!m_lookAlike.isEmpty()) {
        findings << pkgscan::Finding{QStringLiteral("look-alike"), pkgscan::Severity::Warning, {}, 0,
            tr("The name is almost the same as “%1”").arg(m_lookAlike),
            tr("“%1” is a package in the repositories. Make sure that this is not an imitation with a misspelled name.").arg(m_lookAlike), {}};
    }
    pkgscan::sort(findings);

    m_findings.clear();
    for (const auto& finding : std::as_const(findings)) {
        m_findings << QVariantMap{
            {QStringLiteral("id"), finding.id},
            {QStringLiteral("severity"), pkgscan::severityName(finding.severity)},
            {QStringLiteral("file"), finding.file},
            {QStringLiteral("line"), finding.line},
            {QStringLiteral("title"), finding.title},
            {QStringLiteral("detail"), finding.detail},
            {QStringLiteral("excerpt"), finding.excerpt},
        };
    }
    emit findingsChanged();
}

void AurClient::checkUpdates() {
    QStringList names;
    for (int row = 0; row < m_packages->rowCount(); ++row) {
        const auto& package = m_packages->at(row);
        // packages held back in pacman.conf are not offered, like `pacman -Syu` and paru do
        if (package.foreign() && package.installed() && !package.ignored) {
            names << package.name;
        }
    }

    const int generation = ++m_updatesGeneration;
    m_updatesFound.clear();
    m_updatesKnown   = 0;
    m_updatesFailed  = false;
    m_updatesPending = 0;
    if (names.isEmpty()) {
        m_updates.clear();
        m_installedCount = 0;
        emit updatesChanged();
        return;
    }

    const bool wasChecking = checkingUpdates();
    for (qsizetype first = 0; first < names.size(); first += INFO_BATCH) {
        QUrl url(QStringLiteral("%1/rpc/v5/info").arg(QLatin1String(AUR_HOST)));
        QUrlQuery query;
        for (const auto& name : names.mid(first, INFO_BATCH)) {
            query.addQueryItem(QStringLiteral("arg[]"), QString::fromLatin1(QUrl::toPercentEncoding(name)));
        }
        url.setQuery(query);

        ++m_updateRequests;
        ++m_updatesPending;
        auto* reply = m_network.get(request(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
            reply->deleteLater();
            --m_updateRequests;
            if (generation == m_updatesGeneration) {
                --m_updatesPending;
                // a captive portal or an error page can answer with status 200 as well
                const auto json = QJsonDocument::fromJson(reply->readAll()).object();
                if (reply->error() != QNetworkReply::NoError || json.value(QLatin1String("type")).toString() != QLatin1String("multiinfo")
                    || !json.value(QLatin1String("results")).isArray()) {
                    m_updatesFailed = true;
                }
                for (const auto& item : json.value(QLatin1String("results")).toArray()) {
                    auto map = toMap(item.toObject());
                    ++m_updatesKnown;
                    // installed packages that moved into a repository are updated from there
                    if (map.value(QStringLiteral("upgradable")).toBool() && !map.value(QStringLiteral("inRepos")).toBool()) {
                        m_updatesFound << map;
                    }
                }
                // a check that could not reach the AUR keeps the previous list
                if (m_updatesPending == 0 && !m_updatesFailed) {
                    std::ranges::sort(m_updatesFound, [](const QVariantMap& a, const QVariantMap& b) {
                        return a.value(QStringLiteral("name")).toString() < b.value(QStringLiteral("name")).toString();
                    });
                    m_updates.clear();
                    for (const auto& map : std::as_const(m_updatesFound)) {
                        m_updates << map;
                    }
                    m_installedCount = m_updatesKnown;
                    emit updatesChanged();
                }
            }
            if (m_updateRequests == 0) {
                emit checkingUpdatesChanged();
            }
        });
    }
    if (!wasChecking) {
        emit checkingUpdatesChanged();
    }
}

void AurClient::clearUpdates() {
    ++m_updatesGeneration;
    m_updates.clear();
    m_installedCount = -1;
    emit updatesChanged();
}

void AurClient::refreshInstalled() {
    bool changed = false;
    for (auto& item : m_results) {
        auto map            = item.toMap();
        const auto* local   = m_packages->find(map.value(QStringLiteral("name")).toString());
        const bool installed = local != nullptr && local->installed();
        const auto installedVersion = installed ? local->installedVersion : QString();
        if (map.value(QStringLiteral("installedVersion")).toString() != installedVersion) {
            map.insert(QStringLiteral("installed"), installed);
            map.insert(QStringLiteral("installedVersion"), installedVersion);
            map.insert(QStringLiteral("upgradable"),
                installed && alpmdb::vercmp(installedVersion, map.value(QStringLiteral("version")).toString()) < 0);
            item    = map;
            changed = true;
        }
    }
    if (changed) {
        emit resultsChanged();
    }
    if (!m_details.isEmpty()) {
        const auto* local = m_packages->find(m_details.value(QStringLiteral("name")).toString());
        const bool installed = local != nullptr && local->installed();
        m_details.insert(QStringLiteral("installed"), installed);
        m_details.insert(QStringLiteral("installedVersion"), installed ? local->installedVersion : QString());
        m_details.insert(QStringLiteral("upgradable"),
            installed && alpmdb::vercmp(local->installedVersion, m_details.value(QStringLiteral("version")).toString()) < 0);
        emit detailsChanged();
    }
}
