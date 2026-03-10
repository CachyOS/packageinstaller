// Copyright (C) 2022-2025 Vladislav Nepogodin
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

// NOLINTBEGIN(bugprone-unhandled-exception-at-new)

#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include "about.hpp"
#include "alpm_manager.hpp"
#include "pacmancache.hpp"
#include "utils.hpp"
#include "versionnumber.hpp"

#include <algorithm>  // for all_of, find_if
#include <array>      // for array
#include <ranges>     // for ranges::*

#include <QCoreApplication>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

MainWindow::MainWindow(QWidget* parent) : QDialog(parent),
                                          m_ui(new Ui::MainWindow) {
    m_ui->setupUi(this);

    setAttribute(Qt::WA_NativeWindow);
    setWindowFlags(Qt::Window);  // for the close, min and max buttons

    {
        auto alpm_manager = alpm::AlpmManager::init_alpm_manager();
        if (alpm_manager.has_value()) {
            m_alpm_manager = std::make_unique<alpm::AlpmManager>(std::move(*alpm_manager));
        } else {
            QMessageBox::critical(this, tr("CachyOS Package Installer"), tr("Cannot initialize ALPM library"));
            return;
        }
    }

    setProgressDialog();

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::updateBar);
    connect(&m_cmd, &Cmd::started, this, &MainWindow::cmdStart);
    connect(&m_cmd, &Cmd::finished, this, &MainWindow::cmdDone);
    m_conn = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& out) { spdlog::debug("{}", out.trimmed().toStdString()); });
    connect(&m_cmd, &Cmd::errorAvailable, [](const QString& out) { spdlog::warn("{}", out.trimmed().toStdString()); });
    setWindowFlags(Qt::Window);  // for the close, min and max buttons

    // Set window title
    this->setWindowTitle(tr("CachyOS Package Installer"));

    setup();
    buildPackageLists();
}

MainWindow::~MainWindow() {
    delete m_ui;
}

// Setup versious items first time program runs
void MainWindow::setup() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->blockSignals(true);
    m_ui->pushRemoveOrphan->setHidden(true);

    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    m_ui->outputBox->setFont(font);

    m_user = "--system ";

    // Set comboUser to current user by default
    m_ui->comboUser->setCurrentIndex(1);

    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup, Qt::QueuedConnection);
    m_ui->tabWidget->setCurrentIndex(Tab::Repo);

    m_ui->treeRepo->hideColumn(TreeCol::Status);     // Status of the package: installed, upgradable, etc
    m_ui->treeRepo->hideColumn(TreeCol::Displayed);  // Displayed status true/false
    m_ui->treeFlatpak->hideColumn(FlatCol::Status);
    m_ui->treeFlatpak->hideColumn(FlatCol::Displayed);
    m_ui->treeFlatpak->hideColumn(FlatCol::Duplicate);
    m_ui->treeFlatpak->hideColumn(FlatCol::FullName);
    const QString icon      = "software-update-available-symbolic";
    const QIcon backup_icon = QIcon(":/icons/software-update-available.png");
    m_ui->icon->setIcon(QIcon::fromTheme(icon, backup_icon));

    // connect search boxes
    connect(m_ui->searchBoxRepo, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    connect(m_ui->searchBoxFlatpak, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);

    // connect combo filters
    connect(m_ui->comboFilterRepo, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(m_ui->comboFilterFlatpak, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);

    // connect tab widget
    connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::on_current_tab_changed);

    m_warning_flatpaks = false;
    m_tree             = m_ui->treeRepo;

    m_ui->treeRepo->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ui->treeFlatpak->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), false);

    const QSize size = this->size();
    if (m_settings.contains("geometry")) {
        restoreGeometry(m_settings.value("geometry").toByteArray());
        if (this->isMaximized()) {  // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    }

    // check/uncheck tree items space-bar press or double-click
    auto* shortcutToggle = new QShortcut(Qt::Key_Space, this);
    connect(shortcutToggle, &QShortcut::activated, this, &MainWindow::checkUncheckItem);

    const std::array list_tree{m_ui->treeRepo, m_ui->treeFlatpak};
    for (auto&& tree : list_tree) {
        if (tree == m_ui->treeRepo) {
            tree->setContextMenuPolicy(Qt::CustomContextMenu);
        }
        connect(tree, &QTreeWidget::itemDoubleClicked, [tree](QTreeWidgetItem* item) { tree->setCurrentItem(item); });
        connect(tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::checkUncheckItem);
    }

    // hide flatpak
    if (!m_settings.contains("showFlatpak") || !m_settings.value("showFlatpak").toBool()) {
        m_ui->tabWidget->setTabEnabled(Tab::Flatpak, false);
        m_ui->tabWidget->setTabVisible(Tab::Flatpak, false);
    }

    m_ui->tabWidget->blockSignals(false);

    // connect buttons
    connect(m_ui->pushHelp, &QPushButton::clicked, [] {
        about::display_doc(QStringLiteral("file:///usr/share/doc/cachyos-packageinstaller/cachyos-pi.html"));
    });

    connect(m_ui->pushAbout, &QPushButton::clicked, this, &MainWindow::on_push_about);
    connect(m_ui->pushInstall, &QPushButton::clicked, this, &MainWindow::on_push_install);
    connect(m_ui->pushUninstall, &QPushButton::clicked, this, &MainWindow::on_push_uninstall);
    connect(m_ui->pushRemoveUnused, &QPushButton::clicked, this, &MainWindow::on_push_remove_unused);
    connect(m_ui->pushRemoveOrphan, &QPushButton::clicked, this, &MainWindow::on_push_remove_orphan);

    connect(m_ui->pushForceUpdateRepo, &QPushButton::clicked, this, &MainWindow::on_push_force_update_repo);
    connect(m_ui->pushCancel, &QPushButton::clicked, this, &MainWindow::on_push_cancel);
    connect(m_ui->pushEnter, &QPushButton::clicked, this, &MainWindow::on_push_enter);
    connect(m_ui->pushUpgradeAll, &QPushButton::clicked, this, &MainWindow::on_push_upgrade_all);
    connect(m_ui->pushRemotes, &QPushButton::clicked, this, &MainWindow::on_push_remotes);
    connect(m_ui->pushUpgradeFP, &QPushButton::clicked, this, &MainWindow::on_push_upgrade_flatpak);
}

// Uninstall listed packages
bool MainWindow::uninstall(const QString& names) noexcept {
    using namespace std::string_view_literals;

    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);

    // simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    bool is_ok{};
    if (!confirmActions(names, "remove"sv, is_ok)) {
        return true;
    }

    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Uninstalling packages..."));
    displayOutput();

    const auto& cmd_str = [&is_ok, names = names.toStdString()]() -> std::string {
        if (is_ok) {
            return fmt::format("pkexec pacman -R --noconfirm {}", names);
        }
        return fmt::format("pkexec pacman -R {}", names);
    }();

    return m_cmd.run(cmd_str.c_str());
}

// convert number, unit to bytes
constexpr double convert(const double number, std::string_view unit) {
    if (unit == "KB") {  // assuming KiB not KB
        return number * 1024;
    } else if (unit == "MB") {
        return number * 1024 * 1024;
    } else if (unit == "GB") {
        return number * 1024 * 1024 * 1024;
    }
    // for "bytes"
    return number;
}

// Add sizes for the installed packages for older flatpak that doesn't list size for all the packages
void MainWindow::listSizeInstalledFP() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    auto sizes_list = m_cmd.getCmdOut(QStringLiteral("flatpak list ") + m_user + QStringLiteral("--columns app,size")).split('\n');

    QString total;
    for (auto&& item : sizes_list) {
        total = addSizes(total, item.section('\t', 1));
    }

    m_ui->labelNumSize->setText(std::move(total));
}

// Block interface while updating Flatpak list
void MainWindow::blockInterfaceFP(bool block) noexcept {
    m_ui->tabWidget->widget(Tab::Flatpak)->setEnabled(!block);
    m_ui->comboRemote->setDisabled(block);
    m_ui->comboFilterFlatpak->setDisabled(block);
    m_ui->comboUser->setDisabled(block);
    m_ui->searchBoxFlatpak->setDisabled(block);
    m_ui->treeFlatpak->setDisabled(block);
    m_ui->frameFP->setDisabled(block);
    m_ui->labelFP->setDisabled(block);
    m_ui->labelRepo->setDisabled(block);
    block ? setCursor(QCursor(Qt::BusyCursor)) : setCursor(QCursor(Qt::ArrowCursor));
}

// Update interface when done loading info
void MainWindow::updateInterface() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
    m_progress->hide();

    auto upgr_list = m_tree->findItems(QLatin1String("upgradable"), Qt::MatchExactly, 5);
    auto inst_list = m_tree->findItems(QLatin1String("installed"), Qt::MatchExactly, 5);

    if (m_tree == m_ui->treeRepo) {
        m_ui->labelNumApps->setText(QString::number(m_tree->topLevelItemCount()));
        m_ui->labelNumUpgr->setText(QString::number(upgr_list.count()));
        m_ui->labelNumInst->setText(QString::number(inst_list.count() + upgr_list.count()));
        m_ui->pushUpgradeAll->setVisible(!upgr_list.isEmpty());
        m_ui->pushForceUpdateRepo->setEnabled(true);
        m_ui->searchBoxRepo->setFocus();
    }
}

// add two string "00 KB" and "00 GB", return similar string
QString MainWindow::addSizes(const QString& arg1, const QString& arg2) noexcept {
    const auto& number1 = arg1.simplified().section('.', 0, 0);
    const auto& number2 = arg2.simplified().section('.', 0, 0);
    const auto& unit1   = arg1.simplified().section('.', 1);
    const auto& unit2   = arg2.simplified().section('.', 1);

    // const auto& splitted_str1 = utils::make_multiline(arg1.simplified().toStdString(), ' ');
    // const auto& splitted_str2 = utils::make_multiline(arg2.simplified().toStdString(), ' ');
    // const auto& number1 = splitted_str1[0].c_str();
    // const auto& number2 = splitted_str2[0].c_str();
    // const auto& unit1   = splitted_str1[1].c_str();
    // const auto& unit2   = splitted_str2[1].c_str();

    // calculate
    const auto& bytes = convert(number1.toDouble(), unit1.toStdString()) + convert(number2.toDouble(), unit2.toStdString());

    // presentation
    if (bytes < 1024) {
        return QString::number(bytes) + " bytes";
    } else if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString::number(bytes / (1024 * 1024), 'f', 1) + " MB";
    }

    return QString::number(bytes / (1024 * 1024 * 1024), 'f', 2) + " GB";
}

void MainWindow::updateBar() {
    QApplication::processEvents();
    m_bar->setValue((m_bar->value() + 1) % m_bar->maximum() + 1);
}

void MainWindow::checkUncheckItem() {
    auto* t_widget = qobject_cast<QTreeWidget*>(focusWidget());
    if (!t_widget || t_widget->currentItem() == nullptr || t_widget->currentItem()->childCount() > 0) {
        return;
    }

    const auto new_state = (t_widget->currentItem()->checkState(TreeCol::Check)) ? Qt::Unchecked : Qt::Checked;
    t_widget->currentItem()->setCheckState(TreeCol::Check, new_state);
}

void MainWindow::outputAvailable(const QString& output) {
    m_ui->outputBox->moveCursor(QTextCursor::End);
    if (output.contains('\r')) {
        m_ui->outputBox->moveCursor(QTextCursor::Up, QTextCursor::KeepAnchor);
        m_ui->outputBox->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }
    m_ui->outputBox->insertPlainText(output);
    m_ui->outputBox->verticalScrollBar()->setValue(m_ui->outputBox->verticalScrollBar()->maximum());
}

// In case of duplicates add extra name to disambiguate
void MainWindow::removeDuplicatesFP() noexcept {
    // find and mark duplicates
    QTreeWidgetItemIterator it(m_ui->treeFlatpak);
    QTreeWidgetItem* prevItem = nullptr;
    QSet<QString> namesSet;

    // Find and mark duplicates
    while ((*it) != nullptr) {
        auto currentItem        = *it;
        const auto& currentName = currentItem->text(FlatCol::ShortName);
        if (namesSet.contains(currentName)) {
            // Mark both occurrences as duplicate
            if (prevItem) {
                prevItem->setText(FlatCol::Duplicate, QLatin1String("true"));
            }
            currentItem->setText(FlatCol::Duplicate, QLatin1String("true"));
        } else {
            namesSet.insert(currentName);
        }
        prevItem = *it;
        ++it;
    }

    // Rename duplicates to use more context
    for (it = QTreeWidgetItemIterator(m_ui->treeFlatpak); *it; ++it) {
        auto currentItem = *it;
        if (currentItem->text(FlatCol::Duplicate) == QLatin1String("true")) {
            const auto& longName = currentItem->text(FlatCol::LongName);
            currentItem->setText(FlatCol::ShortName, longName.section('.', -2));
        }
    }
}

// Setup progress dialog
void MainWindow::setProgressDialog() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_progress = new QProgressDialog(this);
    m_bar      = new QProgressBar(m_progress);
    m_bar->setMaximum(m_bar->maximum());
    m_pushCancel = new QPushButton(tr("Cancel"));
    connect(m_pushCancel, &QPushButton::clicked, this, &MainWindow::cancelDownload);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint
        | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    m_progress->setCancelButton(m_pushCancel);
    m_pushCancel->setDisabled(true);
    m_progress->setLabelText(tr("Please wait..."));
    m_progress->setAutoClose(false);
    m_progress->setBar(m_bar);
    m_bar->setTextVisible(false);
    m_progress->reset();
}

void MainWindow::setSearchFocus() noexcept {
    switch (m_ui->tabWidget->currentIndex()) {
    case Tab::Repo:
        m_ui->searchBoxRepo->setFocus();
        break;
    case Tab::Flatpak:
        m_ui->searchBoxFlatpak->setFocus();
        break;
    default:
        break;
    }
}

// Display only the listed apps (Flatpak only)
void MainWindow::displayFilteredFP(QStringList list, bool raw) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->treeFlatpak->blockSignals(true);

    // raw format that needs to be edited
    if (raw) {
        QMutableStringListIterator i(list);
        while (i.hasNext()) {
            // remove version and size
            i.setValue(i.next().section('\t', 1, 1).section('/', 1));
        }
    }

    std::uint32_t total{};
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto currentItem = *it;
        if (list.contains(currentItem->text(FlatCol::FullName))) {
            ++total;
            currentItem->setHidden(false);
            currentItem->setText(FlatCol::Displayed, QStringLiteral("true"));  // Displayed flag
            if (currentItem->checkState(FlatCol::Check) == Qt::Checked && currentItem->text(FlatCol::Status) == QLatin1String("installed")) {
                m_ui->pushUninstall->setEnabled(true);
                m_ui->pushInstall->setEnabled(false);
            } else {
                m_ui->pushUninstall->setEnabled(false);
                m_ui->pushInstall->setEnabled(true);
            }
        } else {
            currentItem->setHidden(true);
            currentItem->setText(FlatCol::Displayed, QStringLiteral("false"));
            if (currentItem->checkState(FlatCol::Check) == Qt::Checked) {
                // uncheck hidden item
                currentItem->setCheckState(FlatCol::Check, Qt::Unchecked);
                m_change_list.removeOne(currentItem->text(FlatCol::FullName));
            }
        }
        // reset comboFilterFlatpak if nothing is selected
        if (m_change_list.isEmpty()) {
            m_ui->pushUninstall->setEnabled(false);
            m_ui->pushInstall->setEnabled(false);
        }
    }
    m_ui->labelNumAppFP->setText(QString::number(total));
    m_ui->treeFlatpak->blockSignals(false);
    blockInterfaceFP(false);
}

// Display available packages
void MainWindow::displayPackages() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    // for m_ui-treeRepo, m_ui->treePopularApps, m_ui->treeFlatpak
    QTreeWidget* newtree{m_ui->treeRepo};  // use this to not overwrite current "tree"
    const auto repo_list{m_repo_list};

    newtree->blockSignals(true);

    auto hashInstalled = listInstalledVersions();
    // create a list of apps, create a hash with app_name, app_info
    for (const auto& [key, value] : repo_list) {
        auto* widget_item = new QTreeWidgetItem(newtree);
        widget_item->setCheckState(TreeCol::Check, Qt::Unchecked);
        widget_item->setText(TreeCol::Name, key);
        widget_item->setText(TreeCol::Version, value.at(0));
        widget_item->setText(TreeCol::Description, value.at(1));
        widget_item->setText(TreeCol::Displayed, QStringLiteral("true"));  // all items are displayed till filtered

        // update tree
        if (isFilteredName(key) && m_ui->checkHideLibs->isChecked()) {
            widget_item->setHidden(true);
        }

        const bool is_app_found = hashInstalled.contains(key);
        VersionNumber installed;
        if (is_app_found) {
            installed = hashInstalled.at(key);
        }
        widget_item->setIcon(TreeCol::UpdateIcon, QIcon());  // reset update icon
        if (!is_app_found) {
            for (int i = 0; i < widget_item->columnCount(); ++i) {
                if (m_repo_list.contains(key)) {
                    widget_item->setToolTip(i, tr("Version ") + value.at(0) + tr(" in repo"));
                } else {
                    widget_item->setToolTip(i, tr("Not available in repo"));
                }
            }
            widget_item->setText(TreeCol::Status, QStringLiteral("not installed"));
        } else {
            if (!m_repo_upd_list.contains(key)) {
                for (int i = 0; i < widget_item->columnCount(); ++i) {
                    widget_item->setForeground(TreeCol::Name, QBrush(Qt::gray));
                    widget_item->setForeground(TreeCol::Description, QBrush(Qt::gray));
                    widget_item->setToolTip(i, tr("Latest version ") + installed.toQString() + tr(" already installed"));
                }
                widget_item->setText(TreeCol::Status, QStringLiteral("installed"));
            } else {
                widget_item->setIcon(TreeCol::UpdateIcon, QIcon::fromTheme("software-update-available-symbolic", QIcon(":/icons/software-update-available.png")));
                for (int i = 0; i < widget_item->columnCount(); ++i) {
                    widget_item->setToolTip(i, tr("Version ") + installed.toQString() + tr(" installed"));
                }
                widget_item->setText(TreeCol::Status, QStringLiteral("upgradable"));
            }
        }
    }
    for (int i = 0; i < newtree->columnCount(); ++i) {
        newtree->resizeColumnToContents(i);
    }

    updateInterface();
    newtree->blockSignals(false);
}

void MainWindow::displayFlatpaks(bool force_update) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    if (m_flatpaks.isEmpty() || force_update) {
        setCursor(QCursor(Qt::BusyCursor));

        listFlatpakRemotes();
        m_ui->treeFlatpak->blockSignals(true);
        m_ui->treeFlatpak->clear();
        m_change_list.clear();

        m_progress->show();
        blockInterfaceFP(true);
        m_flatpaks = listFlatpaks(m_ui->comboRemote->currentText());
        m_flatpaks_apps.clear();
        flatpaks_runtimes.clear();

        // list installed packages
        m_installed_apps_fp = listInstalledFlatpaks("--app");

        // add runtimes (needed for older flatpak versions)
        m_installed_runtimes_fp = listInstalledFlatpaks("--runtime");

        std::uint32_t total_count{};
        QTreeWidgetItem* widget_item = nullptr;

        QString short_name;
        QString long_name;
        QString version;
        QString size;
        for (auto item : std::as_const(m_flatpaks)) {
            size    = item.section('\t', -1);
            version = item.section('\t', 0, 0);
            item    = item.section('\t', 1, 1).section('/', 1);
            if (version.isEmpty()) {
                version = item.section('/', -1);
            }

            long_name  = item.section('/', 0, 0);
            short_name = long_name.section('.', -1);
            if (short_name == QLatin1String("Locale") || short_name == QLatin1String("Sources")
                || short_name == QLatin1String("Debug")) {  // skip Locale, Sources, Debug
                continue;
            }
            ++total_count;
            widget_item = new QTreeWidgetItem(m_ui->treeFlatpak);
            widget_item->setCheckState(FlatCol::Check, Qt::Unchecked);
            widget_item->setText(FlatCol::ShortName, short_name);
            widget_item->setText(FlatCol::LongName, long_name);
            widget_item->setText(FlatCol::Version, version);
            widget_item->setText(FlatCol::Size, size);
            widget_item->setText(FlatCol::FullName, item);  // Full string
            const auto installed_all{m_installed_apps_fp + m_installed_runtimes_fp};
            if (installed_all.contains(item)) {
                widget_item->setForeground(FlatCol::ShortName, QBrush(Qt::gray));
                widget_item->setForeground(FlatCol::LongName, QBrush(Qt::gray));
                widget_item->setText(FlatCol::Status, QStringLiteral("installed"));
            } else {
                widget_item->setText(FlatCol::Status, QStringLiteral("not installed"));
            }
            widget_item->setText(FlatCol::Displayed, QStringLiteral("true"));  // all items are displayed till filtered
        }

        // add sizes for the installed packages for older flatpak that doesn't list size for all the packages
        listSizeInstalledFP();
        m_ui->labelNumAppFP->setText(QString::number(total_count));

        const auto total = (m_installed_apps_fp != QStringList(QLatin1String(""))) ? m_installed_apps_fp.count() : 0;
        m_ui->labelNumInstFP->setText(QString::number(total));

        m_ui->treeFlatpak->sortByColumn(FlatCol::ShortName, Qt::AscendingOrder);
        removeDuplicatesFP();

        for (std::int32_t i = 0; i < m_ui->treeFlatpak->columnCount(); ++i) {
            m_ui->treeFlatpak->resizeColumnToContents(i);
        }
    }

    m_ui->treeFlatpak->blockSignals(false);
    if (!m_ui->comboFilterFlatpak->currentText().isEmpty()) {
        filterChanged(m_ui->comboFilterFlatpak->currentText());
    }
    m_ui->searchBoxFlatpak->setFocus();
    m_progress->hide();
    blockInterfaceFP(false);
}

// Display warning
void MainWindow::displayWarning(std::string_view repo) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    bool* displayed = nullptr;
    QString msg;

    using namespace std::string_view_literals;
    if (repo == "flatpaks"sv) {
        displayed = &m_warning_flatpaks;
        msg       = tr("CachyOS includes this repository of flatpaks for the users' convenience only, and "
                             "is not responsible for the functionality of the individual flatpaks themselves. "
                             "For more, consult flatpaks in the Wiki.");
    }
    if (!displayed || *displayed || (m_settings.value("disableWarning", false).toBool())) {
        return;
    }

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), msg, QMessageBox::Close, this);
    auto* cb = new QCheckBox();
    msgBox.setCheckBox(cb);
    cb->setText(tr("Do not show this message again"));
    connect(cb, &QCheckBox::clicked, this, [this](bool clicked) { this->disableWarning(clicked); });
    msgBox.exec();
    *displayed = true;
}

// If download fails hide progress bar and show first tab
void MainWindow::ifDownloadFailed() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_progress->hide();
    m_ui->tabWidget->setCurrentWidget(m_ui->tabRepo);
}

// List the flatpak remote and load them into combobox
void MainWindow::listFlatpakRemotes() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    auto currentRemote = m_ui->comboRemote->currentText();
    m_ui->comboRemote->blockSignals(true);
    m_ui->comboRemote->clear();

    auto remote_list = m_cmd.getCmdOut(QStringLiteral("flatpak remote-list ") + m_user + QStringLiteral("| cut -f1")).remove(' ').split('\n');
    m_ui->comboRemote->addItems(remote_list);
    // set flathub default
    m_ui->comboRemote->setCurrentText(currentRemote.isEmpty() ? "flathub" : currentRemote);
    m_ui->comboRemote->blockSignals(false);
}

// Display warning
bool MainWindow::confirmActions(const QString& names, std::string_view action, bool& is_ok) noexcept {
    using namespace std::string_view_literals;

    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    std::vector<std::string> change_list(static_cast<std::size_t>(m_change_list.size()));
    for (std::size_t i = 0; i < change_list.size(); ++i) {
        change_list[i] = m_change_list[static_cast<std::int32_t>(i)].toStdString();
    }
    QString msg;

    QString detailed_names;
    QStringList detailed_installed_names;
    QString detailed_to_install;
    QString detailed_removed_names;
    std::string summary;
    std::string msg_ok_status;

    if (m_tree == m_ui->treeFlatpak) {
        detailed_installed_names = m_change_list;
    } else {
        const char delim      = (names.contains('\n')) ? '\n' : ' ';
        const auto& name_list = ::utils::make_multiline(names.toStdString(), delim);

        is_ok = true;
        if (action == "install"sv) {
            detailed_names = QString::fromStdString(m_alpm_manager->display_install_targets(name_list, true, summary));
        } else {
            detailed_names = QString::fromStdString(m_alpm_manager->display_remove_targets(name_list, true, summary));
        }

        if (action == "install"sv) {
            is_ok = (m_alpm_manager->prepare_add_trans(name_list, msg_ok_status) == 0);
        }
        m_alpm_manager->refresh_alpm();
    }

    if ((m_tree != m_ui->treeFlatpak) && (!is_ok)) {
        QMessageBox msgBox(this);
        msg = "<b>The following packages have conflicts.</b>";
        msgBox.setText(msg);
        msgBox.setInformativeText("\n" + names + "\n\n" + QString::fromStdString(msg_ok_status));

        auto* replaceBtn = msgBox.addButton("Replace", QMessageBox::AcceptRole);
        auto* ignoreBtn  = msgBox.addButton("Ignore", QMessageBox::RejectRole);

        // set default action(e.g. on close/reject) to ignore
        msgBox.setDefaultButton(ignoreBtn);

        // make it wider
        auto* horizontalSpacer = new QSpacerItem(600, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        auto* layout           = qobject_cast<QGridLayout*>(msgBox.layout());
        layout->addItem(horizontalSpacer, 0, 1);

        // as we added custom buttons, we cannot use return value of exec
        msgBox.exec();
        if (msgBox.clickedButton() != replaceBtn) {
            return false;
        }
    }

    if (m_tree != m_ui->treeFlatpak) {
        if (action == "install"sv) {
            detailed_to_install = detailed_names;
        } else {
            detailed_removed_names = detailed_names;
        }
        if (!detailed_removed_names.isEmpty()) {
            detailed_removed_names.prepend(tr("Remove") + '\n');
        }
        if (!detailed_to_install.isEmpty()) {
            detailed_to_install.prepend(tr("Install") + '\n');
        }
    } else {
        if (action == "remove"sv) {
            detailed_removed_names = m_change_list.join('\n');
            detailed_to_install.clear();
        }
        if (action == "install"sv) {
            detailed_to_install = m_change_list.join('\n');
            detailed_removed_names.clear();
        }
    }

    msg = "<b>" + tr("The following packages were selected. Click Show Details for list of changes.") + "</b>";

    QMessageBox msgBox(this);
    msgBox.setText(msg);
    msgBox.setInformativeText("\n" + names + "\n\n" + QString::fromStdString(summary));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);

    if (action == "install"sv) {
        msgBox.setDetailedText(detailed_to_install + "\n" + detailed_removed_names);
    } else {
        msgBox.setDetailedText(detailed_removed_names + "\n" + detailed_to_install);
    }

    // make it wider
    auto* horizontalSpacer = new QSpacerItem(600, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto* layout           = qobject_cast<QGridLayout*>(msgBox.layout());
    layout->addItem(horizontalSpacer, 0, 1);

    return msgBox.exec() == QMessageBox::Ok;
}

// Install the list of apps
bool MainWindow::install(const QString& names) noexcept {
    using namespace std::string_view_literals;
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Installing packages..."));

    // simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    bool is_ok{};
    if (!confirmActions(names, "install"sv, is_ok)) {
        return true;
    }

    displayOutput();

    const auto& cmd_str = QStringLiteral("pkexec pacman -S ") + names;
    return m_cmd.run(cmd_str);
}

// Install selected items
bool MainWindow::installSelected() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);

    const bool result = install(m_change_list.join(' '));
    m_change_list.clear();
    m_installed_packages = listInstalled();
    return result;
}

// check if the name is filtered (lib, dev, dbg, etc.)
bool MainWindow::isFilteredName(const QString& name) noexcept {
    return ((name.startsWith(QLatin1String("lib")) && !name.startsWith(QLatin1String("libre")))
        || name.endsWith(QLatin1String("-dev")) || name.endsWith(QLatin1String("-dbg")) || name.endsWith(QLatin1String("-dbgsym"))
        || name.endsWith(QLatin1String("-debug"))
        || name.endsWith(QLatin1String("-devel")));
}

// Build the list of available packages from various source
bool MainWindow::buildPackageLists(bool force_download) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    clearUi();
    if (!fetchPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    displayPackages();
    return true;
}

bool MainWindow::fetchPackageList(bool force) noexcept {
    m_progress->setLabelText(tr("Fetching infomation about packages..."));
    m_pushCancel->setEnabled(true);

    if (m_repo_list.empty() || force) {
        m_progress->show();

        PacmanCache cache(m_alpm_manager);
        m_repo_list     = cache.get_candidates();
        m_repo_upd_list = cache.get_upgrade_candidates();
        if (m_repo_list.empty()) {
            cache.refresh_list();
            m_repo_list     = cache.get_candidates();
            m_repo_upd_list = cache.get_upgrade_candidates();
        }
    }

    return true;
}

void MainWindow::enableTabs(bool enable) noexcept {
    for (int tab = 0; tab < m_ui->tabWidget->count() - 1; ++tab) {  // enable all except last (Console)
        m_ui->tabWidget->setTabEnabled(tab, enable);
    }
}

// Cancel download
void MainWindow::cancelDownload() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_cmd.terminate();
}

void MainWindow::centerWindow() noexcept {
    const auto screenGeometry = qApp->screens().first()->geometry();
    const auto x              = (screenGeometry.width() - this->width()) / 2;
    const auto y              = (screenGeometry.height() - this->height()) / 2;
    this->move(x, y);
}

// Clear UI when building package list
void MainWindow::clearUi() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    blockSignals(true);
    m_ui->pushCancel->setEnabled(true);
    m_ui->pushInstall->setEnabled(false);
    m_ui->pushUninstall->setEnabled(false);

    if (m_tree == m_ui->treeRepo) {
        m_ui->labelNumApps->clear();
        m_ui->labelNumInst->clear();
        m_ui->labelNumUpgr->clear();
        m_ui->treeRepo->clear();
        m_ui->pushUpgradeAll->setHidden(true);
    }
    m_ui->comboFilterFlatpak->setCurrentIndex(0);
    m_ui->comboFilterRepo->setCurrentIndex(0);
    blockSignals(false);
}

// Cleanup environment when window is closed
void MainWindow::cleanup() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    m_cmd.halt();
    m_settings.setValue("geometry", saveGeometry());
}

// Get version of the package
auto MainWindow::get_package_version(std::string_view name) noexcept -> std::string {
    if (auto pkg = m_alpm_manager->get_package_view(name)) {
        return std::string{pkg->pkgver};
    }
    return {};
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QString& names) const noexcept {
    if (names.isEmpty()) {
        return false;
    }

    return std::ranges::all_of(names.split('\n'), [this](auto&& name) { return m_installed_packages.contains(name.trimmed()); });
}

// Return true if all the packages in the list are installed
bool MainWindow::checkInstalled(const QStringList& name_list) const noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    if (name_list.isEmpty()) {
        return false;
    }

    return std::ranges::all_of(name_list, [this](auto&& name) { return m_installed_packages.contains(name); });
}

// return true if all the items in the list are upgradable
bool MainWindow::checkUpgradable(const QStringList& name_list) const noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    if (name_list.isEmpty()) {
        return false;
    }

    return std::ranges::all_of(name_list, [this](auto&& name) {
        auto item_list = m_tree->findItems(name, Qt::MatchExactly, TreeCol::Name);
        return !(item_list.isEmpty() || item_list.at(0)->text(TreeCol::Status) != QLatin1String("upgradable"));
    });
}

// Returns list of all installed packages
QStringList MainWindow::listInstalled() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    disconnect(m_conn);
    const auto& installed_list = m_cmd.getCmdOut("pacman -Qq").split('\n');
    m_conn                     = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& out) { spdlog::debug("{}", out.trimmed().toStdString()); });
    return installed_list;
}

// Return list flatpaks from current remote
QStringList MainWindow::listFlatpaks(const QString& remote, std::string_view type) noexcept {
    using namespace std::string_view_literals;

    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    static bool updated = false;

    bool success = false;
    QString out;
    QStringList flatpak_list;

    // need to specify arch for older version (flatpak takes different format than dpkg)
    const auto arch_fp = QStringLiteral("--arch=x86_64 ");

    disconnect(m_conn);
    if (!updated) {
        success = m_cmd.run("flatpak update --appstream");
        updated = true;
    }
    // list version too, unfortunatelly the resulting string structure is different depending on type option
    if (type == "--app"sv || type.empty()) {
        success      = m_cmd.run(QStringLiteral("flatpak remote-ls ") + m_user
                     + remote + ' ' + arch_fp + QStringLiteral(" --app --columns=ver,ref,installed-size 2>/dev/null"),
                 out);
        flatpak_list = out.split('\n');
        if (flatpak_list == QStringList("")) {
            flatpak_list = QStringList();
        }
    }
    if (type == "--runtime"sv || type.empty()) {
        success = m_cmd.run(QStringLiteral("flatpak remote-ls ") + m_user
                + remote + ' ' + arch_fp + QStringLiteral(" --runtime --columns=branch,ref,installed-size 2>/dev/null"),
            out);
        flatpak_list += out.split('\n');
        if (flatpak_list == QStringList("")) {
            flatpak_list = QStringList();
        }
    }
    m_conn = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& cmd_out) { spdlog::debug("{}", cmd_out.trimmed().toStdString()); });

    if (!success || flatpak_list == QStringList("")) {
        spdlog::error("Could not list packages from {} remote, or remote doesn't contain packages", remote.toStdString());
        return {};
    }
    return flatpak_list;
}

// list installed flatpaks by type: apps, runtimes, or all (if no type is provided)
QStringList MainWindow::listInstalledFlatpaks(std::string_view type) {
    const auto& flatpak_cmd = fmt::format("flatpak list {} {} --columns=ref 2>/dev/null", m_user.toStdString(), type);

    QStringList installed_list;
    installed_list << m_cmd.getCmdOut(flatpak_cmd.c_str())
                          .remove(' ')
                          .split('\n');

    if (installed_list == QStringList("")) {
        return {};
    }
    return installed_list;
}

// return the visible tree
void MainWindow::setCurrentTree() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    const QList list({m_ui->treeRepo, m_ui->treeFlatpak});

    auto it = std::ranges::find_if(list, [](const auto& item) { return item->isVisible(); });
    if (it != list.end()) {
        m_tree = *it;
        updateInterface();
        return;
    }
}

auto MainWindow::listInstalledVersions() -> std::unordered_map<QString, VersionNumber> {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    disconnect(m_conn);
    auto out = m_cmd.getCmdOut("pacman -Q", true);
    m_conn   = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& cmd_out) { spdlog::debug("{}", cmd_out.trimmed().toStdString()); });

    QString name;
    std::string ver_str;
    QStringList item;
    std::unordered_map<QString, VersionNumber> result;

    const auto& lines = out.split('\n');
    for (const auto& line : lines) {
        item         = line.simplified().split(' ');
        name         = item.at(0);
        ver_str      = item.at(1).toStdString();
        result[name] = VersionNumber(ver_str);
    }
    return result;
}

// Things to do when the command starts
void MainWindow::cmdStart() {
    m_timer.start(100);
    setCursor(QCursor(Qt::BusyCursor));
    m_ui->lineEdit->setFocus();
}

// Things to do when the command is done
void MainWindow::cmdDone() {
    m_timer.stop();
    setCursor(QCursor(Qt::ArrowCursor));
    disableOutput();
    m_bar->setValue(m_bar->maximum());
}

void MainWindow::displayOutput() {
    connect(&m_cmd, &Cmd::outputAvailable, this, &MainWindow::outputAvailable, Qt::UniqueConnection);
    connect(&m_cmd, &Cmd::errorAvailable, this, &MainWindow::outputAvailable, Qt::UniqueConnection);
}

void MainWindow::disableOutput() {
    disconnect(&m_cmd, &Cmd::outputAvailable, this, &MainWindow::outputAvailable);
    disconnect(&m_cmd, &Cmd::errorAvailable, this, &MainWindow::outputAvailable);
}

// Disable warning
void MainWindow::disableWarning(bool checked) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_settings.setValue("disableWarning", checked);
}

void MainWindow::displayPackageInfo(const QTreeWidgetItem* item) {
    const auto& item_pkgname = item->text(2).toStdString();
    QString msg              = m_cmd.getCmdOut(QString::fromStdString(fmt::format("pacman -Si {}", item_pkgname)));
    QString details          = m_cmd.getCmdOut(QString::fromStdString(fmt::format("pacman -Siv {}", item_pkgname)));

    auto detail_list  = details.split('\n');
    auto msg_list     = msg.split('\n');
    auto max_no_lines = 20;                // cut message after these many lines
    if (msg_list.size() > max_no_lines) {  // split msg into details if too large
        msg         = msg_list.mid(0, max_no_lines).join('\n');
        detail_list = msg_list.mid(max_no_lines, msg_list.length()) + QStringList{""} + detail_list;
        details     = detail_list.join('\n');
    }
    msg += "\n\n" + detail_list.at(detail_list.size() - 2);  // add info about space needed/freed

    QMessageBox info(QMessageBox::NoIcon, tr("Package info"), msg, QMessageBox::Close);
    info.setDetailedText(details);

    // make it wider
    auto* horizontalSpacer = new QSpacerItem(this->width(), 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto* layout           = qobject_cast<QGridLayout*>(info.layout());
    layout->addItem(horizontalSpacer, 0, 1);
    info.exec();
}

// Find packages in other sources
void MainWindow::findPackageOther() {
    const auto& word = [this]() -> QString {
        if (m_tree == m_ui->treeRepo) {
            return m_ui->searchBoxRepo->text();
        } else if (m_tree == m_ui->treeFlatpak) {
            return m_ui->searchBoxFlatpak->text();
        }
        return {};
    }();
    if (word.length() == 1) {
        return;
    }

    auto found_items = m_tree->findItems(word, Qt::MatchContains, TreeCol::Name);
    if (m_tree != m_ui->treeFlatpak) {
        // not for treeFlatpak as it has a different column structure
        found_items << m_tree->findItems(word, Qt::MatchContains, TreeCol::Description);
    }

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto currentItem = *it;
        currentItem->setHidden(currentItem->text(TreeCol::Displayed) != QLatin1String("true") || !found_items.contains(currentItem));
        // Hide libs
        if (isFilteredName(currentItem->text(TreeCol::Name)) && m_ui->checkHideLibs->isChecked()) {
            currentItem->setHidden(true);
        }
    }
}

void MainWindow::showOutput() {
    m_ui->outputBox->clear();
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);
    m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);
    enableTabs(false);
}

// Install button clicked
void MainWindow::on_push_install() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    // qDebug() << "change list"  << .join(' ');
    showOutput();

    if (m_tree == m_ui->treeFlatpak) {
        // confirmation dialog
        bool is_ok{};
        if (!confirmActions(m_change_list.join(' '), "install", is_ok)) {
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
            enableTabs(true);
            return;
        }
        setCursor(QCursor(Qt::BusyCursor));
        displayOutput();
        if (m_cmd.run("socat SYSTEM:'flatpak install -y " + m_user
                + m_ui->comboRemote->currentText() + ' ' + m_change_list.join(' ') + "',stderr STDIO")) {
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    } else {
        const bool success = installSelected();
        buildPackageLists();
        if (success) {
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    }
    enableTabs(true);
}

// About button clicked
void MainWindow::on_push_about() noexcept {
    const auto& msgbox_title = tr("About %1").arg(this->windowTitle());
    const auto& msgbox_body  = "<p align=\"center\"><b><h2>" + this->windowTitle() + "</h2></b></p><p align=\"center\">" + tr("Version: ") + APP_VERSION + "</p><p align=\"center\"><h3>" + tr("Package Installer for CachyOS") + R"(</h3></p><p align="center"><a href="http://cachyos.org">http://cachyos.org</a><br /></p><p align="center">)" + tr("Copyright (c) CachyOS") + "<br /><br /></p>";
    about::display_about_msgbox(msgbox_title, msgbox_body,
        QStringLiteral("file:///usr/share/doc/cachyos-packageinstaller/license.html"));
}

// Uninstall clicked
void MainWindow::on_push_uninstall() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    showOutput();

    QString names;

    if (m_tree == m_ui->treeFlatpak) {
        bool success = true;

        // new version of flatpak takes a "-y" confirmation
        const auto conf = QStringLiteral("-y ");

        // confirmation dialog
        bool is_ok{};
        if (!confirmActions(m_change_list.join(' '), "remove", is_ok)) {
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            listFlatpakRemotes();
            m_ui->comboRemote->setCurrentIndex(0);
            on_comboRemote_activated(m_ui->comboRemote->currentIndex());
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            enableTabs(true);
            return;
        }

        setCursor(QCursor(Qt::BusyCursor));
        for (const auto& app : m_change_list) {
            displayOutput();
            if (!m_cmd.run("socat SYSTEM:'flatpak uninstall " + conf
                    + app + "',stderr STDIO")) {  // success if all processed successfully, failure if one failed
                success = false;
            }
        }
        if (success) {  // success if all processed successfully, failure if one failed
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            listFlatpakRemotes();
            m_ui->comboRemote->setCurrentIndex(0);
            on_comboRemote_activated(m_ui->comboRemote->currentIndex());
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling, please check output"));
        }
        enableTabs(true);
        return;
    } else {
        names = m_change_list.join(' ');
    }

    const bool success = uninstall(names);
    if (!m_repo_list.empty()) {  // update list if it already exists
        buildPackageLists();
    }
    if (success) {
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
}

// Actions on switching the tabs
void MainWindow::on_current_tab_changed(int index) noexcept {
    using namespace std::string_view_literals;

    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Console Output"));
    m_ui->pushInstall->setEnabled(false);
    m_ui->pushUninstall->setEnabled(false);

    // reset checkboxes when tab changes
    m_tree->blockSignals(true);
    m_tree->clearSelection();

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto* currentItem = *it;
        currentItem->setCheckState(0, Qt::Unchecked);
    }
    m_tree->blockSignals(false);

    // save the search text
    QString search_str;
    int filter_idx = 0;
    if (m_tree == m_ui->treeRepo) {
        search_str = m_ui->searchBoxRepo->text();
        filter_idx = m_ui->comboFilterRepo->currentIndex();
    } else if (m_tree == m_ui->treeFlatpak) {
        search_str = m_ui->searchBoxFlatpak->text();
    }

    bool success = false;
    switch (index) {
    case Tab::Repo:
        m_ui->searchBoxRepo->setText(search_str);

        // TODO(vnepogodin): here should be just native pacman check on output if we have any orphans
        m_ui->pushRemoveOrphan->setVisible(system("test -n \"$(pacman -Qtdq)\"") == 0);
        enableTabs(true);
        setCurrentTree();
        m_change_list.clear();
        if (m_tree->topLevelItemCount() == 0) {
            buildPackageLists();
        }
        m_ui->comboFilterRepo->setCurrentIndex(filter_idx);
        findPackageOther();
        m_ui->searchBoxRepo->setFocus();
        break;
    case Tab::Flatpak:
        m_ui->searchBoxFlatpak->setText(search_str);
        enableTabs(true);
        setCurrentTree();
        displayWarning("flatpaks"sv);
        blockInterfaceFP(true);

        if (!checkInstalled("flatpak")) {
            const auto ans = QMessageBox::question(this, tr("Flatpak not installed"), tr("Flatpak is not currently installed.\nOK to go ahead and install it?"));
            if (ans == QMessageBox::No) {
                m_ui->tabWidget->setCurrentIndex(Tab::Repo);
                break;
            }
            m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);
            setCursor(QCursor(Qt::BusyCursor));
            install("flatpak");
            m_change_list.clear();
            m_installed_packages = listInstalled();
            buildPackageLists();
            if (!checkInstalled("flatpak")) {
                QMessageBox::critical(this, tr("Flatpak not installed"), tr("Flatpak was not installed"));
                m_ui->tabWidget->setCurrentIndex(Tab::Repo);
                setCursor(QCursor(Qt::ArrowCursor));
                enableTabs(true);
                m_ui->tabWidget->blockSignals(false);
                return;
            }
            success = m_cmd.run("pkexec /bin/bash -c \"flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo && flatpak remote-add --if-not-exists --subset=verified flathub-verified https://flathub.org/repo/flathub.flatpakrepo\"");
            if (!success) {
                QMessageBox::critical(this, tr("Flathub remote failed"), tr("Flathub remote could not be added"));
                m_ui->tabWidget->setCurrentIndex(Tab::Repo);
                setCursor(QCursor(Qt::ArrowCursor));
                break;
            }
            displayOutput();
            listFlatpakRemotes();

            setCursor(QCursor(Qt::ArrowCursor));
            m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Console Output"));
            m_ui->tabWidget->blockSignals(true);
            displayFlatpaks(true);
            m_ui->tabWidget->blockSignals(false);
            QMessageBox::warning(this, tr("Needs re-login"), tr("You might need to logout/login to see installed items in the menu"));
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            enableTabs(true);
            return;
        }
        setCursor(QCursor(Qt::BusyCursor));
        displayOutput();
        setCursor(QCursor(Qt::ArrowCursor));
        if (m_ui->comboRemote->currentText().isEmpty()) {
            listFlatpakRemotes();
        }

        displayFlatpaks(false);
        break;
    case Tab::Output:
        m_ui->searchBoxRepo->clear();
        m_ui->pushInstall->setDisabled(true);
        m_ui->pushUninstall->setDisabled(true);
        break;
    default:
        break;
    }
    m_ui->pushUpgradeAll->setVisible((m_tree == m_ui->treeRepo) && (m_ui->labelNumUpgr->text().toInt() > 0));
}

// Filter items according to selected filter
void MainWindow::filterChanged(const QString& arg1) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_tree->blockSignals(true);

    QList<QTreeWidgetItem*> found_items;
    // filter for Flatpak
    if (m_tree == m_ui->treeFlatpak) {
        if (arg1 == tr("Installed runtimes")) {
            displayFilteredFP(m_installed_runtimes_fp);
        } else if (arg1 == tr("Installed apps")) {
            displayFilteredFP(m_installed_apps_fp);
        } else if (arg1 == tr("All apps")) {
            if (m_flatpaks_apps.isEmpty()) {
                m_flatpaks_apps = listFlatpaks(m_ui->comboRemote->currentText(), "--app");
            }
            displayFilteredFP(m_flatpaks_apps, true);
        } else if (arg1 == tr("All runtimes")) {
            if (flatpaks_runtimes.isEmpty()) {
                flatpaks_runtimes = listFlatpaks(m_ui->comboRemote->currentText(), "--runtime");
            }
            displayFilteredFP(flatpaks_runtimes, true);
        } else if (arg1 == tr("All available")) {
            int total = 0;
            for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                auto currentItem = *it;
                currentItem->setText(FlatCol::Displayed, QStringLiteral("true"));
                currentItem->setHidden(false);
                ++total;
            }
            m_ui->labelNumAppFP->setText(QString::number(total));
        } else if (arg1 == tr("All installed")) {
            displayFilteredFP(m_installed_apps_fp + m_installed_runtimes_fp);
        } else if (arg1 == tr("Not installed")) {
            found_items = m_tree->findItems("not installed", Qt::MatchExactly, FlatCol::Status);
            QStringList new_list;
            for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                auto currentItem = *it;
                if (found_items.contains(currentItem)) {
                    new_list << currentItem->text(FlatCol::FullName);
                }
            }
            displayFilteredFP(new_list);
        }
        setSearchFocus();
        findPackageOther();
        m_tree->blockSignals(false);
        return;
    }

    if (arg1 == tr("All packages")) {
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            auto currentItem = *it;
            currentItem->setText(TreeCol::Displayed, QStringLiteral("true"));
            currentItem->setHidden(false);
        }
        findPackageOther();
        setSearchFocus();
        m_tree->blockSignals(false);
        return;
    }

    if (arg1 == tr("Upgradable")) {
        found_items = m_tree->findItems(QLatin1String("upgradable"), Qt::MatchExactly, TreeCol::Status);
    } else if (arg1 == tr("Installed")) {
        found_items = m_tree->findItems(QLatin1String("installed"), Qt::MatchExactly, TreeCol::Status);
    } else if (arg1 == tr("Not installed")) {
        found_items = m_tree->findItems(QLatin1String("not installed"), Qt::MatchExactly, TreeCol::Status);
    }

    m_change_list.clear();
    m_ui->pushUninstall->setEnabled(false);
    m_ui->pushInstall->setEnabled(false);

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto currentItem = *it;
        currentItem->setCheckState(TreeCol::Check, Qt::Unchecked);  // uncheck all items
        if (found_items.contains(currentItem)) {
            currentItem->setHidden(false);
            currentItem->setText(TreeCol::Displayed, QLatin1String("true"));
        } else {
            currentItem->setHidden(true);
            currentItem->setText(TreeCol::Displayed, QLatin1String("false"));
        }
    }
    findPackageOther();
    setSearchFocus();
    m_tree->blockSignals(false);
}

// When selecting on item in the list
void MainWindow::on_treeRepo_itemChanged(QTreeWidgetItem* item) noexcept {
    if (item->checkState(TreeCol::Check) == Qt::Checked) {
        m_ui->treeRepo->setCurrentItem(item);
    }
    buildChangeList(item);
}

void MainWindow::on_treeFlatpak_itemChanged(QTreeWidgetItem* item) noexcept {
    if (item->checkState(FlatCol::Check) == Qt::Checked) {
        m_ui->treeFlatpak->setCurrentItem(item);
    }
    buildChangeList(item);
}

// Build the change_list when selecting on item in the tree
void MainWindow::buildChangeList(QTreeWidgetItem* item) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    /* if all apps are uninstalled (or some installed) -> enable Install, disable Uinstall
     * if all apps are installed or upgradable -> enable Uninstall, enable Install
     * if all apps are upgradable -> change Install label to Upgrade;
     */

    const auto newapp = [this, &item]() -> QString {
        if (m_tree == m_ui->treeFlatpak) {
            if (m_change_list.isEmpty() && m_indexFilterFP.isEmpty()) {  // remember the Flatpak combo location first time this is called
                m_indexFilterFP = m_ui->comboFilterFlatpak->currentText();
            }
            return item->text(FlatCol::FullName);
        } else {
            return item->text(TreeCol::Name);
        }
        return {};
    }();

    if (item->checkState(0) == Qt::Checked) {
        m_ui->pushInstall->setEnabled(true);
        m_change_list.append(std::move(newapp));
    } else {
        m_change_list.removeOne(std::move(newapp));
    }

    if (m_tree != m_ui->treeFlatpak) {
        m_ui->pushUninstall->setEnabled(checkInstalled(m_change_list));
        m_ui->pushInstall->setText(checkUpgradable(m_change_list) ? tr("Upgrade") : tr("Install"));
    } else {  // for Flatpaks allow selection only of installed or not installed items so one clicks on an installed item only installed items should be displayed and the other way round
        m_ui->pushInstall->setText(tr("Install"));
        if (item->text(FlatCol::Status) == QLatin1String("installed")) {
            if (item->checkState(FlatCol::Check) == Qt::Checked && m_ui->comboFilterFlatpak->currentText() != tr("All installed")) {
                m_ui->comboFilterFlatpak->setCurrentText(tr("All installed"));
            }
            m_ui->pushUninstall->setEnabled(true);
            m_ui->pushInstall->setEnabled(false);
        } else {
            if (item->checkState(FlatCol::Check) == Qt::Checked && m_ui->comboFilterFlatpak->currentText() != tr("Not installed")) {
                m_ui->comboFilterFlatpak->setCurrentText(tr("Not installed"));
            }
            m_ui->pushUninstall->setEnabled(false);
            m_ui->pushInstall->setEnabled(true);
        }
        if (m_change_list.isEmpty()) {  // reset comboFilterFlatpak if nothing is selected
            m_ui->comboFilterFlatpak->setCurrentText(m_indexFilterFP);
            m_indexFilterFP.clear();
        }
        m_ui->treeFlatpak->setFocus();
    }

    if (m_change_list.isEmpty()) {
        m_ui->pushInstall->setEnabled(false);
        m_ui->pushUninstall->setEnabled(false);
    }
}

// Force repo upgrade
void MainWindow::on_push_force_update_repo() noexcept {
    m_ui->searchBoxRepo->clear();
    m_ui->comboFilterRepo->setCurrentIndex(0);
    m_alpm_manager->refresh_alpm();
    buildPackageLists(true);
}

// Hide/unhide lib/-dev packages
void MainWindow::on_checkHideLibs_toggled(bool checked) noexcept {
    for (QTreeWidgetItemIterator it(m_ui->treeRepo); *it; ++it) {
        auto currentItem = *it;
        currentItem->setHidden(isFilteredName(currentItem->text(TreeCol::Name)) && checked);
    }
    filterChanged(m_ui->comboFilterRepo->currentText());
}

// Upgrade all packages (from Stable repo only)
void MainWindow::on_push_upgrade_all() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();

    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Upgrading system..."));

    displayOutput();

    const auto& cmd_str = QStringLiteral("pkexec pacman -Syu");
    if (m_cmd.run(cmd_str)) {
        m_alpm_manager->refresh_alpm();
        buildPackageLists(true);

        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
    }

    enableTabs(true);
}

// Pressing Enter or buttonEnter should do the same thing
void MainWindow::on_push_enter() noexcept {
    on_lineEdit_returnPressed();
}

// Send the response to terminal process
void MainWindow::on_lineEdit_returnPressed() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_cmd.write(m_ui->lineEdit->text().toUtf8() + "\n");
    m_ui->outputBox->appendPlainText(m_ui->lineEdit->text() + "\n");
    m_ui->lineEdit->clear();
    m_ui->lineEdit->setFocus();
}

void MainWindow::on_push_cancel() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    if (m_cmd.state() != QProcess::NotRunning) {
        if (QMessageBox::warning(this, tr("Quit?"),
                tr("Process still running, quitting might leave the system in an unstable state.<p><b>Are you sure you want to exit CachyOS Package Installer?</b>"),
                QMessageBox::Yes, QMessageBox::No)
            == QMessageBox::No) {
            return;
        }
    }
    cleanup();
    qApp->quit();
}

// on change flatpak remote
void MainWindow::on_comboRemote_activated(int) noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    displayFlatpaks(true);
}

void MainWindow::on_push_upgrade_flatpak() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    displayOutput();
    if (m_cmd.run("socat SYSTEM:'flatpak update " + m_user.trimmed() + "',pty STDIO")) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        m_ui->tabWidget->blockSignals(true);
        m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
        m_ui->tabWidget->blockSignals(false);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
    }
    enableTabs(true);
}

void MainWindow::on_push_remotes() noexcept {
    auto* dialog = new ManageRemotes(this);
    dialog->exec();
    if (dialog->is_changed()) {
        listFlatpakRemotes();
        displayFlatpaks(true);
    }
    auto install_ref = dialog->get_install_ref();
    if (!install_ref.isEmpty()) {
        showOutput();
        setCursor(QCursor(Qt::BusyCursor));
        displayOutput();
        if (m_cmd.run("socat SYSTEM:'flatpak install -y " + dialog->get_user() + "--from " + install_ref.replace(':', "\\:") + "',stderr STDIO")) {
            listFlatpakRemotes();
            displayFlatpaks(true);
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
        enableTabs(true);
    }
}

void MainWindow::on_comboUser_activated(int index) noexcept {
    static bool updated{};
    if (index == 0) {
        m_user = "--system ";
    } else {
        m_user = "--user ";
        if (!updated) {
            setCursor(QCursor(Qt::BusyCursor));
            displayOutput();
            m_cmd.run("flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo");
            m_cmd.run("flatpak --user remote-add --if-not-exists --subset=verified flathub-verified https://flathub.org/repo/flathub.flatpakrepo");

            m_cmd.run("flatpak update --appstream");

            setCursor(QCursor(Qt::ArrowCursor));
            updated = true;
        }
    }
    listFlatpakRemotes();
    displayFlatpaks(true);
}

void MainWindow::on_treeRepo_customContextMenuRequested(const QPoint& pos) noexcept {
    auto* t_widget = qobject_cast<QTreeWidget*>(focusWidget());
    auto* action   = new QAction(QIcon::fromTheme("dialog-information"), tr("More &info..."), this);
    QMenu menu(this);
    menu.addAction(action);
    connect(action, &QAction::triggered, [this, t_widget] { displayPackageInfo(t_widget->currentItem()); });
    menu.exec(m_ui->treeRepo->mapToGlobal(pos));
    action->deleteLater();
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        this->on_push_cancel();
    }
}

void MainWindow::on_push_remove_unused() noexcept {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    displayOutput();
    // new version of flatpak takes a "-y" confirmation
    const auto& conf = QStringLiteral("-y ");
    if (m_cmd.run("socat SYSTEM:'flatpak uninstall --unused " + conf + m_user + "',pty STDIO")) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"), tr("Problem detected during last operation, please inspect the console output."));
    }
    enableTabs(true);
}

void MainWindow::on_push_remove_orphan() noexcept {
    const auto names = m_cmd.getCmdOut("pacman -Qdtq | tr '\\n' ' '");
    QMessageBox::warning(this, tr("Warning"), tr("Potentially dangerous operation.\nPlease make sure you check carefully the list of packages to be removed."));
    showOutput();

    const bool success = uninstall(names);
    if (!m_repo_list.empty()) {  // update list if it already exists
        buildPackageLists();
    }

    if (success) {
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
    m_ui->tabWidget->setCurrentIndex(Tab::Repo);
}

// NOLINTEND(bugprone-unhandled-exception-at-new)
