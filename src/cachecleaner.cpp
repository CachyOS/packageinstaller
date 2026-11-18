// Copyright (C) 2026 Yan Henridev
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "cachecleaner.hpp"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

namespace {

constexpr auto kKeepInstalledKey   = "cacheCleaner/keepInstalled";
constexpr auto kKeepUninstalledKey = "cacheCleaner/keepUninstalled";
constexpr auto kGeometryKey        = "cacheCleaner/geometry";

constexpr auto kCandidateHeader = "==> Candidate packages:";
constexpr auto kFinishedPrefix  = "==> finished dry run:";
constexpr auto kRemovedPrefix   = "==> finished:";
constexpr auto kNoCandidates    = "no candidate packages found";

auto extract_saved_size(const QString& line) -> QString {
    const auto marker = line.indexOf(QLatin1String("disk space saved:"));
    if (marker < 0) {
        return {};
    }

    auto size = line.mid(marker + static_cast<qsizetype>(QLatin1String("disk space saved:").size()));
    size.remove(QLatin1Char(')'));
    return size.trimmed();
}

auto extract_summary_line(const QString& output) -> QString {
    const auto lines = output.split(QLatin1Char('\n'));
    for (const auto& raw_line : lines) {
        const auto line = raw_line.trimmed();
        if (line.startsWith(QLatin1String(kRemovedPrefix))) {
            return line.mid(static_cast<qsizetype>(QLatin1String(kRemovedPrefix).size())).trimmed();
        }
    }
    return {};
}

}  // namespace

CacheCleaner::CacheCleaner(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Clean Package Cache"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
    setModal(true);
    resize(760, 560);

    QSettings settings;
    const auto geometry = settings.value(QString::fromLatin1(kGeometryKey)).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    setup_ui();
}

CacheCleaner::~CacheCleaner() {
    const std::array groups{&m_installed, &m_uninstalled};
    for (auto* group : groups) {
        if (group->process != nullptr && group->process->state() != QProcess::NotRunning) {
            group->process->kill();
            group->process->waitForFinished(1000);
        }
    }
}

void CacheCleaner::setup_ui() {
    auto* layout = new QVBoxLayout(this);

    auto* explanation = new QLabel(tr("Old package versions kept in the pacman cache can be removed to free disk space. "
                                      "The lists below show what would be deleted; the cache is only cleaned after confirmation."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    create_group(m_installed, layout, tr("Installed packages"), false);
    create_group(m_uninstalled, layout, tr("Uninstalled packages"), true);

    m_button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_button_box);

    // Populate both lists when the dialog is shown.
    refresh(m_installed);
    refresh(m_uninstalled);
}

void CacheCleaner::create_group(CacheGroup& group, QVBoxLayout* parent_layout, const QString& title, bool uninstalled) {
    group.uninstalled = uninstalled;

    auto* box        = new QGroupBox(title, this);
    auto* box_layout = new QVBoxLayout(box);

    auto* hint = new QLabel(uninstalled
            ? tr("Cached versions of packages that are no longer installed.")
            : tr("Older cached versions of packages that are currently installed."),
        box);
    hint->setWordWrap(true);
    box_layout->addWidget(hint);

    group.list = new QListWidget(box);
    group.list->setObjectName(uninstalled ? QStringLiteral("uninstalledCacheList") : QStringLiteral("installedCacheList"));
    group.list->setSelectionMode(QAbstractItemView::NoSelection);
    group.list->setMinimumHeight(110);
    box_layout->addWidget(group.list);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Keep versions:"), box));

    group.keep = new QSpinBox(box);
    group.keep->setRange(0, 999);
    group.keep->setToolTip(tr("Number of cached versions to keep for each package (0 removes every cached version)"));
    QSettings settings;
    const auto key = uninstalled ? QString::fromLatin1(kKeepUninstalledKey) : QString::fromLatin1(kKeepInstalledKey);
    group.keep->setValue(settings.value(key, uninstalled ? 0 : 1).toInt());
    controls->addWidget(group.keep);

    group.summary = new QLabel(box);
    group.summary->setWordWrap(true);
    controls->addWidget(group.summary, 1);

    group.refresh = new QPushButton(tr("Refresh"), box);
    group.refresh->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    controls->addWidget(group.refresh);

    group.clean = new QPushButton(tr("Clean"), box);
    group.clean->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    group.clean->setEnabled(false);
    controls->addWidget(group.clean);

    box_layout->addLayout(controls);
    parent_layout->addWidget(box);

    group.process = new QProcess(this);
    group.process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(group.refresh, &QPushButton::clicked, this, [this, &group] { refresh(group); });
    connect(group.clean, &QPushButton::clicked, this, [this, &group] { request_clean(group); });
    connect(group.process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, &group](int exit_code, QProcess::ExitStatus exit_status) {
            on_process_finished(group, exit_code, exit_status);
        });
    connect(group.process, &QProcess::errorOccurred, this, [this, &group](QProcess::ProcessError error) {
        on_process_error(group, error);
    });

    auto* debounce = new QTimer(group.process);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    connect(debounce, &QTimer::timeout, this, [this, &group] { refresh(group); });
    connect(group.keep, QOverload<int>::of(&QSpinBox::valueChanged), this, [&group, debounce](int) {
        group.clean->setEnabled(false);
        debounce->start();
    });

    update_group(group);
}

void CacheCleaner::refresh(CacheGroup& group) {
    if (group.state == State::Cleaning) {
        return;
    }
    if (group.state == State::Scanning) {
        group.rescan_pending = true;
        return;
    }

    group.state          = State::Scanning;
    group.rescan_pending = false;
    group.candidates.clear();
    group.saved_size.clear();
    group.list->clear();
    group.summary->setToolTip({});
    group.summary->setText(tr("Scanning cache..."));
    update_group(group);

    QStringList args{QStringLiteral("-v"), QStringLiteral("-d"), QStringLiteral("-k"), QString::number(group.keep->value())};
    if (group.uninstalled) {
        args.prepend(QStringLiteral("-u"));
    }
    group.process->start(QStringLiteral("paccache"), args);
}

void CacheCleaner::request_clean(CacheGroup& group) {
    if (group.state != State::Idle || group.candidates.isEmpty()) {
        return;
    }

    if (QFile::exists(QStringLiteral("/var/lib/pacman/db.lck"))) {
        QMessageBox::warning(this, tr("Clean Package Cache"),
            tr("Another package management application is running.\nPlease close it before cleaning the cache."));
        return;
    }

    const auto count = static_cast<int>(group.candidates.size());
    const auto text  = group.saved_size.isEmpty()
        ? tr("Remove %n cached package(s)?", "", count)
        : tr("Remove %n cached package(s)?\n\nThis will free approximately %1.", "", count).arg(group.saved_size);

    const auto answer = QMessageBox::question(this, tr("Clean Package Cache"), text,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    group.state = State::Cleaning;
    group.summary->setText(tr("Cleaning cache..."));
    update_group(group);

    QStringList args{QStringLiteral("paccache"), QStringLiteral("-r"), QStringLiteral("-k"), QString::number(group.keep->value())};
    if (group.uninstalled) {
        args.insert(1, QStringLiteral("-u"));
    }
    group.process->start(QStringLiteral("pkexec"), args);
}

void CacheCleaner::on_process_finished(CacheGroup& group, int exit_code, QProcess::ExitStatus exit_status) {
    const auto output = QString::fromLocal8Bit(group.process->readAllStandardOutput());
    const auto errors = QString::fromLocal8Bit(group.process->readAllStandardError()).trimmed();
    const bool ok     = exit_status == QProcess::NormalExit && exit_code == 0;

    if (group.state == State::Scanning) {
        group.state = State::Idle;
        if (ok || output.contains(QLatin1String(kNoCandidates))) {
            parse_dry_run(group, output);
        } else {
            group.list->clear();
            group.candidates.clear();
            group.saved_size.clear();
            update_group(group);
            group.summary->setText(tr("Could not read the package cache."));
            group.summary->setToolTip(errors.isEmpty() ? output.trimmed() : errors);
        }

        if (group.rescan_pending) {
            group.rescan_pending = false;
            refresh(group);
        }
        return;
    }

    if (group.state != State::Cleaning) {
        return;
    }

    group.state = State::Idle;
    if (ok) {
        const auto summary = extract_summary_line(output);
        QMessageBox::information(this, tr("Clean Package Cache"),
            summary.isEmpty() ? tr("The package cache was cleaned successfully.") : summary);
        refresh(group);
        return;
    }

    auto details = errors;
    if (details.isEmpty()) {
        details = output.trimmed();
    }

    QString message;
    switch (exit_code) {
    case 126:
        message = tr("Authentication was dismissed or not authorized.");
        break;
    case 127:
        message = tr("Authentication failed.");
        break;
    default:
        message = tr("Could not clean the package cache.");
        break;
    }
    if (!details.isEmpty()) {
        message += QLatin1String("\n\n") + details;
    }

    update_group(group);
    QMessageBox::critical(this, tr("Clean Package Cache"), message);
}

void CacheCleaner::on_process_error(CacheGroup& group, QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
        return;
    }

    const bool was_cleaning = group.state == State::Cleaning;
    group.state             = State::Idle;
    update_group(group);
    group.summary->setText(tr("Could not start the required command."));

    const auto program = group.process->program();
    auto message       = tr("Could not start \"%1\".").arg(program);
    if (program == QLatin1String("paccache")) {
        message += QLatin1String("\n\n") + tr("Install the \"pacman-contrib\" package to use this feature.");
    }

    if (was_cleaning) {
        QMessageBox::critical(this, tr("Clean Package Cache"), message);
    } else {
        group.summary->setToolTip(message);
    }
}

void CacheCleaner::parse_dry_run(CacheGroup& group, const QString& output) {
    group.list->clear();
    group.candidates.clear();
    group.saved_size.clear();

    bool in_candidates = false;
    const auto lines   = output.split(QLatin1Char('\n'));
    for (const auto& raw_line : lines) {
        const auto line = raw_line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1String(kCandidateHeader))) {
            in_candidates = true;
            continue;
        }
        if (line.startsWith(QLatin1String(kFinishedPrefix))) {
            group.saved_size = extract_saved_size(line);
            break;
        }
        if (line.startsWith(QLatin1String("==>")) || !in_candidates) {
            continue;
        }
        group.candidates << line;
    }

    group.list->addItems(group.candidates);
    update_group(group);
}

void CacheCleaner::update_group(CacheGroup& group) {
    update_control_states();

    const bool has_items = !group.candidates.isEmpty();

    if (has_items) {
        group.clean->setText(group.saved_size.isEmpty()
                ? tr("Clean")
                : tr("Clean %1").arg(group.saved_size));
    } else {
        group.clean->setText(tr("Clean"));
    }

    if (group.state == State::Scanning) {
        group.summary->setText(tr("Scanning cache..."));
    } else if (group.state == State::Cleaning) {
        group.summary->setText(tr("Cleaning cache..."));
    } else if (has_items) {
        const auto count = static_cast<int>(group.candidates.size());
        group.summary->setText(group.saved_size.isEmpty()
                ? tr("%n package(s) can be removed", "", count)
                : tr("%n package(s)", "", count) + QLatin1String(" - ") + tr("%1 can be freed").arg(group.saved_size));
    } else {
        group.summary->setText(tr("No packages to remove."));
    }
}

void CacheCleaner::update_control_states() {
    const bool busy = is_busy();
    const std::array groups{&m_installed, &m_uninstalled};
    for (auto* group : groups) {
        if (group->keep == nullptr) {
            continue;
        }
        const bool idle = group->state == State::Idle;
        group->keep->setEnabled(idle && !busy);
        group->refresh->setEnabled(idle && !busy);
        group->clean->setEnabled(idle && !busy && !group->candidates.isEmpty());
    }

    if (m_button_box != nullptr) {
        m_button_box->setEnabled(!busy);
    }
}

void CacheCleaner::save_settings() const {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kKeepInstalledKey), m_installed.keep->value());
    settings.setValue(QString::fromLatin1(kKeepUninstalledKey), m_uninstalled.keep->value());
    settings.setValue(QString::fromLatin1(kGeometryKey), saveGeometry());
}

bool CacheCleaner::is_busy() const noexcept {
    return m_installed.state == State::Cleaning || m_uninstalled.state == State::Cleaning;
}

void CacheCleaner::closeEvent(QCloseEvent* event) {
    if (is_busy()) {
        event->ignore();
        return;
    }
    save_settings();
    QDialog::closeEvent(event);
}

void CacheCleaner::done(int result) {
    if (is_busy()) {
        return;
    }
    save_settings();
    QDialog::done(result);
}

void CacheCleaner::reject() {
    if (is_busy()) {
        return;
    }
    QDialog::reject();
}

void CacheCleaner::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && is_busy()) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}
