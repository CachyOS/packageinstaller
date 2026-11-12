// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "iconprovider.hpp"

#include <QFile>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace {
Backend* s_instance = nullptr;
}

Backend::Backend(IconProvider* icons, QObject* parent)
  : QObject(parent), m_aur(&m_packages), m_icons(icons) {
    s_instance      = this;
    m_aurEnabled    = QSettings().value(QStringLiteral("aur/enabled"), false).toBool();
    m_paruAvailable = !QStandardPaths::findExecutable(QStringLiteral("paru")).isEmpty();

    connect(&m_watcher, &QFutureWatcher<LoadResult>::finished, this, &Backend::onLoaded);
    connect(&m_transaction, &Transaction::finished, this, [this](bool) { reload(); });
    connect(&m_checkUpdates, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_checkingUpdates = false;
        emit checkingUpdatesChanged();
        if (m_checkOutdated) {
            // started before the last reload: its answer may name packages that were updated since
            m_checkOutdated = false;
            if (!m_loading) {
                checkUpdates();
            }
            return;
        }
        // checkupdates: 0 = updates listed, 2 = no updates, anything else = failure (keep the local view)
        if (status != QProcess::NormalExit || (exitCode != 0 && exitCode != 2) || m_loading) {
            return;
        }
        QHash<QString, QString> versions;
        const auto lines = QString::fromUtf8(m_checkUpdates.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            // "name 1.0-1 -> 1.1-1"
            const auto fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (fields.size() == 4) {
                versions.insert(fields.at(0), fields.at(3));
            }
        }
        m_packages.applyUpdates(versions);
        ++m_revision;
        emit revisionChanged();
    });

    // A transaction touches the database many times; reload once things have settled,
    // and leave it to the transaction itself while one of ours is running.
    m_dbChanged.setSingleShot(true);
    m_dbChanged.setInterval(1500);
    connect(&m_dbWatcher, &QFileSystemWatcher::directoryChanged, &m_dbChanged, qOverload<>(&QTimer::start));
    connect(&m_dbChanged, &QTimer::timeout, this, [this] {
        if (m_transaction.state() == Transaction::Idle) {
            reload();
        }
    });

    loadCuratedList();
    reload();
}

Backend* Backend::create(QQmlEngine* /*qmlEngine*/, QJSEngine* /*jsEngine*/) {
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void Backend::setAurEnabled(bool enabled) {
    if (enabled == m_aurEnabled) {
        return;
    }
    m_aurEnabled = enabled;
    QSettings().setValue(QStringLiteral("aur/enabled"), enabled);
    emit aurEnabledChanged();
    if (enabled) {
        m_aur.checkUpdates();
    } else {
        m_aur.clearUpdates();
    }
}

void Backend::reload() {
    if (m_loading) {
        m_reloadQueued = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    m_watcher.setFuture(QtConcurrent::run(catalog::load));
}

void Backend::onLoaded() {
    auto result = m_watcher.result();
    m_error     = result.error;
    if (m_dbWatcher.directories().isEmpty() && !result.dbPath.isEmpty()) {
        m_dbWatcher.addPaths({result.dbPath + QStringLiteral("/local"), result.dbPath + QStringLiteral("/sync")});
    }
    m_icons->setIndex(std::move(result.icons));
    m_packages.reset(std::move(result.packages));
    rebuildCurated();
    m_aur.refreshInstalled();
    // only after the opt-in: the names of the installed packages are sent to the AUR
    if (m_aurEnabled) {
        m_aur.checkUpdates();
    }

    m_loading = false;
    ++m_revision;
    emit loadingChanged();
    emit revisionChanged();

    if (m_reloadQueued) {
        m_reloadQueued = false;
        reload();
        return;
    }
    checkUpdates();
}

void Backend::checkUpdates() {
    if (m_checkUpdates.state() != QProcess::NotRunning) {
        m_checkOutdated = true;
        return;
    }
    const auto program = QStandardPaths::findExecutable(QStringLiteral("checkupdates"));
    if (program.isEmpty()) {
        return;  // pacman-contrib is optional; the local sync databases are used as they are
    }
    m_checkingUpdates = true;
    emit checkingUpdatesChanged();
    m_checkUpdates.start(program, {QStringLiteral("--nocolor")});
}

/// pkglist.yaml is a flat list of sections, each with a list of entries; an
/// entry is "package [companion...]". That subset is parsed here directly.
void Backend::loadCuratedList() {
    QFile file(QStringLiteral(":/qt/qml/CachyOS/PackageInstaller/pkglist.yaml"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("pkglist.yaml is missing from the resources");
        return;
    }
    while (!file.atEnd()) {
        const auto raw  = QString::fromUtf8(file.readLine());
        const auto line = raw.trimmed();
        if (line.startsWith(QLatin1String("- name:"))) {
            auto title = line.mid(7).trimmed();
            title.remove(QLatin1Char('"'));
            m_sections << CuratedSection{title, {}};
        } else if (line.startsWith(QLatin1String("- ")) && raw.startsWith(QLatin1Char(' ')) && !m_sections.isEmpty()) {
            auto names = line.mid(2).remove(QLatin1Char('"')).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (names.isEmpty()) {
                continue;
            }
            const auto name = names.takeFirst();
            m_sections.last().entries << CuratedEntry{name, names};
            if (!names.isEmpty()) {
                m_companions.insert(name, names);
            }
        }
    }
}

void Backend::rebuildCurated() {
    m_curated.clear();
    for (const auto& section : std::as_const(m_sections)) {
        QVariantList packages;
        for (const auto& entry : section.entries) {
            if (const auto* pkg = m_packages.find(entry.name); pkg != nullptr && !pkg->foreign()) {
                packages << m_packages.toMap(*pkg);
            }
        }
        if (!packages.isEmpty()) {
            m_curated << QVariantMap{{QStringLiteral("title"), section.title}, {QStringLiteral("packages"), packages}};
        }
    }
}

QVariantMap Backend::package(const QString& name) const {
    const auto* pkg = m_packages.find(name);
    return pkg == nullptr ? QVariantMap{} : m_packages.toMap(*pkg);
}

QString Backend::formatSize(qint64 bytes) const {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
}

void Backend::install(const QString& name) {
    QStringList targets{name};
    for (const auto& companion : m_companions.value(name)) {
        if (const auto* pkg = m_packages.find(companion); pkg != nullptr && !pkg->foreign() && !pkg->installed()) {
            targets << companion;
        }
    }
    m_transaction.prepare(Transaction::Install, targets);
}

void Backend::remove(const QString& name) {
    m_transaction.prepare(Transaction::Remove, {name});
}

void Backend::upgradeAll() {
    m_transaction.prepare(Transaction::Upgrade, {});
}

void Backend::installFromAur(const QString& name) {
    installManyFromAur({name});
}

void Backend::installManyFromAur(const QStringList& names) {
    if (m_aurEnabled && m_paruAvailable && !names.isEmpty()) {
        // empty unless every package was reviewed and accepted at the revision that is on disk
        m_transaction.prepareAur(names, m_aur.buildDirectories(names), m_aur.buildWorkingDirectory());
    }
}
