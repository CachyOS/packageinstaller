// Copyright (C) 2022 Vladislav Nepogodin
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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshadow"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include <ryml_std.hpp>
#include <ryml.hpp>
#include <cpr/cpr.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include "about.hpp"
#include "alpm_helper.hpp"
#include "config.hpp"
#include "pacmancache.hpp"
#include "utils.hpp"
#include "version.hpp"
#include "versionnumber.hpp"

#include <alpm.h>
#include <alpm_list.h>

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

MainWindow::MainWindow(QWidget* parent) : QDialog(parent),
                                          m_ui(new Ui::MainWindow) {
    spdlog::debug("{} version:{}", QCoreApplication::applicationName().toStdString(), VERSION);

    m_setup_assistant_mode = Config::instance()->data()["setupmode"];

    m_ui->setupUi(this);
    setProgressDialog();

    setup_alpm(m_handle);

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::updateBar);
    connect(&m_cmd, &Cmd::started, this, &MainWindow::cmdStart);
    connect(&m_cmd, &Cmd::finished, this, &MainWindow::cmdDone);
    m_conn = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& out) { spdlog::debug("{}", out.trimmed().toStdString()); });
    connect(&m_cmd, &Cmd::errorAvailable, [](const QString& out) { spdlog::warn("{}", out.trimmed().toStdString()); });
    setWindowFlags(Qt::Window);  // for the close, min and max buttons
    setup();
}

MainWindow::~MainWindow() {
    destroy_alpm(m_handle);
    delete m_ui;
}

// Setup versious items first time program runs
void MainWindow::setup() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->blockSignals(true);
    m_ui->pushRemoveOrphan->setHidden(true);

    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    m_ui->outputBox->setFont(font);

    m_fp_ver = VersionNumber(getVersion("flatpak").toStdString());
    m_user   = "--system ";

    m_arch     = PacmanCache::getArch();
    m_ver_name = "nil";

    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup, Qt::QueuedConnection);
    if (m_setup_assistant_mode) {
        this->setWindowTitle(tr("CachyOS Setup Assistant"));
    } else {
        this->setWindowTitle(tr("CachyOS Package Installer"));
    }
    m_ui->tabWidget->setCurrentIndex(Tab::Popular);
    QStringList column_names;
    column_names << ""
                 << "" << tr("Package") << tr("Info") << tr("Description");
    m_ui->treePopularApps->setHeaderLabels(column_names);
    m_ui->treeRepo->hideColumn(TreeCol::Status);     // Status of the package: installed, upgradable, etc
    m_ui->treeRepo->hideColumn(TreeCol::Displayed);  // Displayed status true/false
    m_ui->treeFlatpak->hideColumn(FlatCol::Status);
    m_ui->treeFlatpak->hideColumn(FlatCol::Displayed);
    m_ui->treeFlatpak->hideColumn(FlatCol::Duplicate);
    m_ui->treeFlatpak->hideColumn(FlatCol::FullName);
    const QString icon      = "software-update-available-symbolic";
    const QIcon backup_icon = QIcon(":/icons/software-update-available.png");
    m_ui->icon->setIcon(QIcon::fromTheme(icon, backup_icon));
    loadTxtFiles();
    refreshPopularApps();

    // connect search boxes
    connect(m_ui->searchPopular, &QLineEdit::textChanged, this, &MainWindow::findPopular);
    connect(m_ui->searchBoxRepo, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    connect(m_ui->searchBoxFlatpak, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);

    // connect combo filters
    connect(m_ui->comboFilterRepo, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(m_ui->comboFilterFlatpak, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);

    m_ui->searchPopular->setFocus();
    m_updated_once     = false;
    m_warning_flatpaks = false;
    m_tree             = m_ui->treePopularApps;

    m_ui->treeRepo->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ui->treePopularApps->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ui->treeFlatpak->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), false);
    m_ui->tabWidget->blockSignals(false);

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

    QList<QTreeWidget*> list_tree{m_ui->treePopularApps, m_ui->treeRepo, m_ui->treeFlatpak};
    for (const auto& tree : list_tree) {
        if (tree == m_ui->treePopularApps || tree == m_ui->treeRepo)
            tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree, &QTreeWidget::itemDoubleClicked, [tree](QTreeWidgetItem* item) { tree->setCurrentItem(item); });
        connect(tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::checkUncheckItem);
    }

    if (m_setup_assistant_mode) {
        m_ui->pushAbout->hide();
        m_ui->pushHelp->hide();
        for (int tab = 1; tab < m_ui->tabWidget->count() - 1; ++tab) {  // disable all except first & last (Console)
            m_ui->tabWidget->setTabEnabled(tab, false);
            m_ui->tabWidget->setTabVisible(tab, false);
        }
    }
}

// Uninstall listed packages
bool MainWindow::uninstall(const QString& names) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);

    m_lockfile.unlock();
    // simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    bool is_ok{};
    if (!confirmActions(names, "remove", is_ok))
        return true;

    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Uninstalling packages..."));
    displayOutput();

    bool success = false;
    if (is_ok) {
        success = m_cmd.run(fmt::format("pacman -R --noconfirm {}", names.toStdString()).c_str());
    } else {
        success = m_cmd.run(fmt::format("yes | pacman -R {}", names.toStdString()).c_str());
    }
    m_lockfile.lock();

    return success;
}

// Run pacman update
bool MainWindow::update() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_lockfile.unlock();
    m_ui->tabOutput->isVisible()  // don't display in output if calling to refresh from tabs
        ? m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Refreshing sources..."))
        : m_progress->show();

    displayOutput();
    if (m_cmd.run("pacman -Sy")) {
        m_lockfile.lock();
        spdlog::info("sources updated OK");
        m_updated_once = true;
        return true;
    }
    m_lockfile.lock();
    spdlog::error("problem updating sources");
    QMessageBox::critical(this, tr("Error"), tr("There was a problem updating sources. Some sources may not have provided updates. For more info check: ") + "<a href=\"/var/log/cachyospi.log\">/var/log/cachyospi.log</a>");
    return false;
}

// convert number, unit to bytes
constexpr inline double convert(const double number, std::string_view unit) {
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
void MainWindow::listSizeInstalledFP() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    QString name, size;
    QString total = "0 bytes";
    QStringList list, runtimes;
    if (m_fp_ver < VersionNumber("1.0.1")) {  // older version doesn't display all apps and runtimes without specifying them
        list     = m_cmd.getCmdOut("runuser -s /bin/bash -l $(logname) -c \"flatpak -d list --app " + m_user + "|tr -s ' ' |cut -f1,5,6 -d' '\"").split("\n");
        runtimes = m_cmd.getCmdOut("runuser -s /bin/bash -l $(logname) -c \"flatpak -d list --runtime " + m_user + "|tr -s ' '|cut -f1,5,6 -d' '\"").split("\n");
        if (!runtimes.isEmpty())
            list << runtimes;
        for (QTreeWidgetItemIterator it(m_ui->treeFlatpak); *it; ++it) {
            for (const QString& item : list) {
                name = item.section(" ", 0, 0);
                size = item.section(" ", 1);
                if (name == (*it)->text(FlatCol::FullName)) {
                    total = addSizes(total, size);
                    (*it)->setText(FlatCol::Size, size);
                }
            }
        }
    } else if (m_fp_ver < VersionNumber("1.2.4"))
        list = m_cmd.getCmdOut("runuser -s /bin/bash -l $(logname) -c \"flatpak -d list " + m_user + "|tr -s ' '|cut -f1,5\"").split("\n");
    else
        list = m_cmd.getCmdOut("runuser -s /bin/bash -l $(logname) -c \"flatpak list " + m_user + "--columns app,size\"").split("\n");

    for (const QString& item : list)
        total = addSizes(total, item.section("\t", 1));

    m_ui->labelNumSize->setText(total);
}

// Block interface while updating Flatpak list
void MainWindow::blockInterfaceFP(bool block) {
    for (int tab = 0; tab < 4; ++tab)
        m_ui->tabWidget->setTabEnabled(tab, !block);

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
void MainWindow::updateInterface() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

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

    QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
    m_progress->hide();
}

// add two string "00 KB" and "00 GB", return similar string
QString MainWindow::addSizes(const QString& arg1, const QString& arg2) {
    const auto& number1 = arg1.simplified().section(" ", 0, 0);
    const auto& number2 = arg2.simplified().section(" ", 0, 0);
    const auto& unit1   = arg1.simplified().section(" ", 1);
    const auto& unit2   = arg2.simplified().section(" ", 1);

    // const auto& splitted_str1 = utils::make_multiline(arg1.simplified().toStdString(), false, " ");
    // const auto& splitted_str2 = utils::make_multiline(arg2.simplified().toStdString(), false, " ");
    // const auto& number1 = splitted_str1[0].c_str();
    // const auto& number2 = splitted_str2[0].c_str();
    // const auto& unit1   = splitted_str1[1].c_str();
    // const auto& unit2   = splitted_str2[1].c_str();

    // calculate
    const auto& bytes = convert(number1.toDouble(), unit1.toStdString().data()) + convert(number2.toDouble(), unit2.toStdString().data());

    // presentation
    if (bytes < 1024)
        return QString::number(bytes) + " bytes";
    else if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024) + " KB";
    else if (bytes < 1024 * 1024 * 1024)
        return QString::number(bytes / (1024 * 1024), 'f', 1) + " MB";

    return QString::number(bytes / (1024 * 1024 * 1024), 'f', 2) + " GB";
}

void MainWindow::updateBar() {
    qApp->processEvents();
    m_bar->setValue((m_bar->value() + 1) % 100);
}

void MainWindow::checkUncheckItem() {
    if (auto t_widget = qobject_cast<QTreeWidget*>(focusWidget())) {
        if (t_widget->currentItem() == nullptr || t_widget->currentItem()->childCount() > 0)
            return;
        const int col  = (t_widget == m_ui->treePopularApps) ? static_cast<int>(PopCol::Check) : static_cast<int>(TreeCol::Check);
        auto new_state = (t_widget->currentItem()->checkState(col)) ? Qt::Unchecked : Qt::Checked;
        t_widget->currentItem()->setCheckState(col, new_state);
    }
}

void MainWindow::outputAvailable(const QString& output) {
    m_ui->outputBox->moveCursor(QTextCursor::End);
    if (output.contains("\r")) {
        m_ui->outputBox->moveCursor(QTextCursor::Up, QTextCursor::KeepAnchor);
        m_ui->outputBox->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }
    m_ui->outputBox->insertPlainText(output);
    m_ui->outputBox->verticalScrollBar()->setValue(m_ui->outputBox->verticalScrollBar()->maximum());
}

// Load info from the .txt files
void MainWindow::loadTxtFiles() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    cpr::Response r = cpr::Get(cpr::Url{"https://raw.githubusercontent.com/cachyos/packageinstaller/develop/pkglist.yaml"},
        cpr::ProgressCallback([&]([[maybe_unused]] auto&& downloadTotal, [[maybe_unused]] auto&& downloadNow, [[maybe_unused]] auto&& uploadTotal,
                                  [[maybe_unused]] auto&& uploadNow, [[maybe_unused]] auto&& userdata) -> bool { return true; }));

    if (r.error.code == cpr::ErrorCode::OK) {
        std::ofstream pkglistyaml{"/usr/lib/cachyos-pi/pkglist.yaml"};
        pkglistyaml << r.text;
    }

    QFile file("/usr/lib/cachyos-pi/pkglist.yaml");
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        spdlog::error("Could not open: {}", file.fileName().toStdString());
        return;
    }
    const auto& src    = file.readAll().toStdString();
    ryml::Tree tree    = ryml::parse_in_arena(ryml::to_csubstr(src));
    ryml::NodeRef root = tree.rootref();  // get a reference to the root

    const auto& get_node_key = [](auto&& node) {
        std::string key{};
        if (node.has_key() && !node.has_key_tag()) {
            key = std::string{node.key().str, node.key().len};
        }
        return key;
    };

    const auto& process_map = [this, &get_node_key](auto&& parent_category, auto&& node) {
        for (const ryml::NodeRef& map : node.children()) {
            std::string category{};
            for (const ryml::NodeRef& map_child : map.children()) {
                if (map_child.has_val() && !map_child.has_val_tag()) {
                    category = std::string{map_child.val().str, map_child.val().len};
                }

                const std::string key{get_node_key(map_child)};

                if (map_child.is_container()) {
                    std::vector<std::string> lines;
                    lines.reserve(map_child.num_children());
                    for (const ryml::NodeRef& pkg_list : map_child.children()) {
                        if (pkg_list.has_val() && !pkg_list.has_val_tag()) {
                            lines.emplace_back(std::string{pkg_list.val().str, pkg_list.val().len});
                        }
                    }
                    for (const auto& line : lines) {
                        processFile(parent_category, category, ::utils::make_multiline(line, false, " "));
                    }
                }
            }
        }
    };
    for (const ryml::NodeRef& map : root.children()) {
        std::string category{};
        for (const ryml::NodeRef& map_child : map.children()) {
            if (map_child.has_val() && !map_child.has_val_tag()) {
                category = std::string{map_child.val().str, map_child.val().len};
            }

            const std::string key{get_node_key(map_child)};

            if (map_child.is_container()) {
                if (key == "subgroups") {
                    process_map(category, map_child);
                } else {
                    std::vector<std::string> lines;
                    lines.reserve(map_child.num_children());
                    for (const ryml::NodeRef& pkg_list : map_child.children()) {
                        if (pkg_list.has_val() && !pkg_list.has_val_tag()) {
                            lines.emplace_back(std::string{pkg_list.val().str, pkg_list.val().len});
                        }
                    }
                    for (const auto& line : lines) {
                        processFile(category, category, ::utils::make_multiline(line, false, " "));
                    }
                }
            }
        }
    }

    file.close();
}

// Process docs
void MainWindow::processFile(const std::string& group, const std::string& category, const std::vector<std::string>& names) {
    if (names.empty())
        return;

    QString description;
    QString install_names;
    QString uninstall_names;
    QStringList list;

    auto* dbs = alpm_get_syncdbs(m_handle);
    for (alpm_list_t* i = dbs; i != nullptr; i = i->next) {
        auto* db  = reinterpret_cast<alpm_db_t*>(i->data);
        auto* pkg = alpm_db_get_pkg(db, names[0].c_str());
        if (pkg) {
            description = alpm_pkg_get_desc(pkg);
            break;
        }
    }

    install_names   = fmt::format("{} {}", names[0], utils::make_multiline_range(names.begin() + 1, names.end(), false, " ")).c_str();
    uninstall_names = install_names;

    list << QString::fromStdString(category) << QString::fromStdString(names[0])
         << description << install_names << uninstall_names << QString::fromStdString(group);

    m_popular_apps << list;
}

// Reload and refresh interface
void MainWindow::refreshPopularApps() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    disableOutput();
    m_ui->treePopularApps->clear();
    m_ui->searchPopular->clear();
    m_ui->pushInstall->setEnabled(false);
    m_ui->pushUninstall->setEnabled(false);
    m_installed_packages = listInstalled();
    displayPopularApps();
}

// In case of duplicates add extra name to disambiguate
void MainWindow::removeDuplicatesFP() {
    // find and mark duplicates
    QTreeWidgetItemIterator it(m_ui->treeFlatpak);
    QString current, next;
    while (*it) {
        current = ((*it))->text(FlatCol::ShortName);
        if (*(++it)) {
            next = ((*it))->text(FlatCol::ShortName);
            if (next == current) {
                --it;
                (*(it))->setText(FlatCol::Duplicate, QLatin1String("true"));
                ++it;
                (*it)->setText(FlatCol::Duplicate, QLatin1String("true"));
            }
        }
    }
    // rename duplicate to use more context
    for (it = QTreeWidgetItemIterator(m_ui->treeFlatpak); *it; ++it) {
        if ((*(it))->text(FlatCol::Duplicate) == QLatin1String("true"))
            (*it)->setText(FlatCol::ShortName, (*it)->text(FlatCol::LongName).section(".", -2));
    }
}

// Setup progress dialog
void MainWindow::setProgressDialog() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_progress = new QProgressDialog(this);
    m_bar      = new QProgressBar(m_progress);
    m_bar->setMaximum(100);
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

void MainWindow::setSearchFocus() {
    if (m_ui->tabRepo->isVisible())
        m_ui->searchBoxRepo->setFocus();
    else if (m_ui->tabFlatpak->isVisible())
        m_ui->searchBoxFlatpak->setFocus();
}

// Display Popular Apps in the treePopularApps
void MainWindow::displayPopularApps() const {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    QTreeWidgetItem* topLevelItem = nullptr;
    QTreeWidgetItem* childItem;

    const auto& top_level_item_emplace = [&](auto&& searchtext) {
        // add package search text if treePopularApps doesn't already have it
        if (m_ui->treePopularApps->findItems(searchtext, Qt::MatchFixedString, PopCol::Name).isEmpty()) {
            topLevelItem = new QTreeWidgetItem();
            topLevelItem->setText(PopCol::Name, searchtext);
            m_ui->treePopularApps->addTopLevelItem(topLevelItem);
            // topLevelItem look
            QFont font;
            font.setBold(true);
            topLevelItem->setFont(PopCol::Name, font);
            topLevelItem->setIcon(PopCol::Icon, QIcon::fromTheme("folder"));
        } else {
            topLevelItem = m_ui->treePopularApps->findItems(searchtext, Qt::MatchFixedString, PopCol::Name).at(0);  // find first match; add the child there
        }
    };

    const auto& tree_widget_find_item = [](auto&& widget, auto&& category) -> QTreeWidgetItem* {
        const auto& topLevelItemChildCount = widget->childCount();
        for (int i = 0; i < topLevelItemChildCount; ++i) {
            auto topLevelItemChild = widget->child(i);
            auto childText         = topLevelItemChild->text(PopCol::Name);
            if (childText == category) { return topLevelItemChild; }
        }
        return nullptr;
    };

    for (const QStringList& list : m_popular_apps) {
        const auto& category        = list.at(Popular::Category);
        const auto& name            = list.at(Popular::Name);
        const auto& description     = list.at(Popular::Description);
        const auto& install_names   = list.at(Popular::InstallNames);
        const auto& uninstall_names = list.at(Popular::UninstallNames);
        const auto& group           = list.at(Popular::Group);

        QTreeWidgetItem* topLevelChildItem = nullptr;
        if (group != category) {
            top_level_item_emplace(group);

            topLevelChildItem = tree_widget_find_item(topLevelItem, category);

            if (topLevelChildItem == nullptr) {
                topLevelChildItem = new QTreeWidgetItem(topLevelItem);
                topLevelChildItem->setText(PopCol::Name, category);
                topLevelItem->addChild(topLevelChildItem);
                // childItem look
                QFont font;
                font.setBold(true);
                topLevelChildItem->setFont(PopCol::Name, font);
                topLevelChildItem->setIcon(PopCol::Icon, QIcon::fromTheme("folder"));
            }
        }

        // add package name as childItem to treePopularApps
        if (group != category) {
            childItem = new QTreeWidgetItem(topLevelChildItem);
        } else {
            top_level_item_emplace(category);
            childItem = new QTreeWidgetItem(topLevelItem);
        }
        childItem->setText(PopCol::Name, name);
        childItem->setIcon(PopCol::Info, QIcon::fromTheme("dialog-information"));
        childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
        childItem->setCheckState(PopCol::Check, Qt::Unchecked);
        childItem->setText(PopCol::Description, description);
        childItem->setText(PopCol::InstallNames, install_names);
        childItem->setText(PopCol::UninstallNames, uninstall_names);  // not displayed

        // gray out installed items
        if (checkInstalled(name)) {
            childItem->setForeground(PopCol::Name, QBrush(Qt::gray));
            childItem->setForeground(PopCol::Description, QBrush(Qt::gray));
        }
    }
    for (int i = 0; i < m_ui->treePopularApps->columnCount(); ++i)
        m_ui->treePopularApps->resizeColumnToContents(i);

    m_ui->treePopularApps->sortItems(2, Qt::AscendingOrder);
    connect(m_ui->treePopularApps, &QTreeWidget::itemClicked, this, &MainWindow::displayInfo, Qt::UniqueConnection);
}

// Display only the listed apps (Flatpak only)
void MainWindow::displayFilteredFP(QStringList list, bool raw) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->treeFlatpak->blockSignals(true);

    QMutableStringListIterator i(list);
    if (raw) {  // raw format that needs to be edited
        if (m_fp_ver < VersionNumber("1.2.4"))
            while (i.hasNext())
                i.setValue(i.next().section("\t", 0, 0));  // remove size
        else
            while (i.hasNext())
                i.setValue(i.next().section("\t", 1, 1).section("/", 1));  // remove version and size
    }

    int total = 0;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (list.contains((*it)->text(FlatCol::FullName))) {
            ++total;
            (*it)->setHidden(false);
            (*it)->setText(FlatCol::Displayed, QStringLiteral("true"));  // Displayed flag
            if ((*it)->checkState(FlatCol::Check) == Qt::Checked && (*it)->text(FlatCol::Status) == QLatin1String("installed")) {
                m_ui->pushUninstall->setEnabled(true);
                m_ui->pushInstall->setEnabled(false);
            } else {
                m_ui->pushUninstall->setEnabled(false);
                m_ui->pushInstall->setEnabled(true);
            }
        } else {
            (*it)->setHidden(true);
            (*it)->setText(FlatCol::Displayed, QStringLiteral("false"));
            if ((*it)->checkState(FlatCol::Check) == Qt::Checked) {
                (*it)->setCheckState(FlatCol::Check, Qt::Unchecked);  // uncheck hidden item
                m_change_list.removeOne((*it)->text(FlatCol::FullName));
            }
        }
        if (m_change_list.isEmpty()) {  // reset comboFilterFlatpak if nothing is selected
            m_ui->pushUninstall->setEnabled(false);
            m_ui->pushInstall->setEnabled(false);
        }
    }
    m_ui->labelNumAppFP->setText(QString::number(total));
    m_ui->treeFlatpak->blockSignals(false);
}

// Display available packages
void MainWindow::displayPackages() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    // for m_ui-treeRepo, m_ui->treePopularApps, m_ui->treeFlatpak
    QTreeWidget* newtree{m_ui->treeRepo};  // use this to not overwrite current "tree"
    std::map<QString, QStringList> list{m_repo_list};

    newtree->blockSignals(true);

    auto hashInstalled = listInstalledVersions();
    // create a list of apps, create a hash with app_name, app_info
    for (const auto& [key, value] : list) {
        auto widget_item = new QTreeWidgetItem(newtree);
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
            const auto& app_ver = value.at(0);
            VersionNumber repo_candidate(app_ver.toStdString());  // candidate from the selected repo, might be different from the one from Stable
            // bool is_upgrade_avail = alpm_sync_get_new_version(alpm_db_get_pkg(app_name.toStdString().c_str()), alpm_get_syncdbs(m_handle)) != nullptr;
            if (installed >= repo_candidate) {
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
    for (int i = 0; i < newtree->columnCount(); ++i)
        newtree->resizeColumnToContents(i);

    updateInterface();
    newtree->blockSignals(false);
}

void MainWindow::displayFlatpaks(bool force_update) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    setCursor(QCursor(Qt::BusyCursor));
    m_ui->treeFlatpak->clear();
    m_ui->treeFlatpak->blockSignals(true);
    m_change_list.clear();

    if (m_flatpaks.isEmpty() || force_update) {
        m_progress->show();
        blockInterfaceFP(true);
        m_flatpaks = listFlatpaks(m_ui->comboRemote->currentText());
        m_flatpaks_apps.clear();
        flatpaks_runtimes.clear();

        // list installed packages
        m_installed_apps_fp = listInstalledFlatpaks("--app");

        // add runtimes (needed for older flatpak versions)
        m_installed_runtimes_fp = listInstalledFlatpaks("--runtime");
    }
    int total_count = 0;
    QTreeWidgetItem* widget_item;

    QString short_name, long_name, version, size;
    for (QString item : m_flatpaks) {
        if (m_fp_ver < VersionNumber("1.2.4")) {
            size    = item.section("\t", 1, 1);
            item    = item.section("\t", 0, 0);  // strip size
            version = item.section("/", -1);
        } else {  // Buster and higher versions
            size    = item.section("\t", -1);
            version = item.section("\t", 0, 0);
            item    = item.section("\t", 1, 1).section("/", 1);
        }
        long_name  = item.section("/", 0, 0);
        short_name = long_name.section(".", -1);
        if (short_name == QLatin1String("Locale") || short_name == QLatin1String("Sources")
            || short_name == QLatin1String("Debug"))  // skip Locale, Sources, Debug
            continue;
        ++total_count;
        widget_item = new QTreeWidgetItem(m_ui->treeFlatpak);
        widget_item->setCheckState(FlatCol::Check, Qt::Unchecked);
        widget_item->setText(FlatCol::ShortName, short_name);
        widget_item->setText(FlatCol::LongName, long_name);
        widget_item->setText(FlatCol::Version, version);
        widget_item->setText(FlatCol::Size, size);
        widget_item->setText(FlatCol::FullName, item);  // Full string
        QStringList installed_all{m_installed_apps_fp + m_installed_runtimes_fp};
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

    int total = 0;
    if (m_installed_apps_fp != QStringList(""))
        total = m_installed_apps_fp.count();

    m_ui->labelNumInstFP->setText(QString::number(total));

    m_ui->treeFlatpak->sortByColumn(FlatCol::ShortName, Qt::AscendingOrder);

    removeDuplicatesFP();

    for (int i = 0; i < m_ui->treeFlatpak->columnCount(); ++i)
        m_ui->treeFlatpak->resizeColumnToContents(i);

    m_ui->treeFlatpak->blockSignals(false);
    filterChanged(m_ui->comboFilterFlatpak->currentText());
    blockInterfaceFP(false);
    m_ui->searchBoxFlatpak->setFocus();
    m_progress->hide();
}

// Display warning
void MainWindow::displayWarning(const QString& repo) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    bool* displayed = nullptr;
    QString msg;

    if (repo == "flatpaks") {
        displayed = &m_warning_flatpaks;
        msg       = tr("CachyOS includes this repository of flatpaks for the users' convenience only, and "
                             "is not responsible for the functionality of the individual flatpaks themselves. "
                             "For more, consult flatpaks in the Wiki.");
    }
    if (!displayed || *displayed || (m_settings.value("disableWarning", false).toBool()))
        return;

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), msg);
    msgBox.addButton(QMessageBox::Close);
    auto* cb = new QCheckBox();
    msgBox.setCheckBox(cb);
    cb->setText(tr("Do not show this message again"));
    connect(cb, &QCheckBox::clicked, [&](bool clicked) { this->disableWarning(clicked); });
    msgBox.exec();
    *displayed = true;
}

// If download fails hide progress bar and show first tab
void MainWindow::ifDownloadFailed() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_progress->hide();
    m_ui->tabWidget->setCurrentWidget(m_ui->tabPopular);
}

// List the flatpak remote and load them into combobox
void MainWindow::listFlatpakRemotes() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->comboRemote->blockSignals(true);
    m_ui->comboRemote->clear();
    QStringList list = m_cmd.getCmdOut("runuser -s /bin/bash -l $(logname) -c \"flatpak remote-list " + m_user + "| cut -f1\"").remove(" ").split("\n");
    m_ui->comboRemote->addItems(list);
    // set flathub default
    m_ui->comboRemote->setCurrentIndex(m_ui->comboRemote->findText("flathub"));
    m_ui->comboRemote->blockSignals(false);
}

// Display warning
bool MainWindow::confirmActions(const QString& names, const QString& action, bool& is_ok) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    std::vector<std::string> change_list(size_t(m_change_list.size()));
    for (size_t i = 0; i < change_list.size(); ++i) {
        change_list[i] = m_change_list[int(i)].toStdString();
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
        m_lockfile.unlock();
        const char* delim     = (names.contains("\n")) ? "\n" : " ";
        const auto& name_list = ::utils::make_multiline(names.toStdString(), false, delim);
        if (action == "install") {
            add_targets_to_install(m_handle, name_list);
        } else {
            is_ok = true;
            add_targets_to_remove(m_handle, name_list);
        }
        detailed_names = display_targets(m_handle, true, summary).c_str();
        alpm_trans_release(m_handle);

        if (action == "install") {
            m_lockfile.unlock();
            refresh_alpm(&m_handle, &m_alpm_err);
            is_ok = (sync_trans(m_handle, name_list, 0, msg_ok_status) == 0);
        }
    }

    if ((m_tree != m_ui->treeFlatpak) && (!is_ok)) {
        QMessageBox msgBox;
        msg = "<b>The following packages have conflicts.</b>";
        msgBox.setText(msg);
        msgBox.setInformativeText("\n" + names + "\n\n" + msg_ok_status.c_str());

        msgBox.addButton("Replace", QMessageBox::ButtonRole::AcceptRole);
        msgBox.addButton("Ignore", QMessageBox::ButtonRole::RejectRole);

        // make it wider
        auto horizontalSpacer = new QSpacerItem(600, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        auto layout           = qobject_cast<QGridLayout*>(msgBox.layout());
        layout->addItem(horizontalSpacer, 0, 1);

        if (msgBox.exec() != QMessageBox::AcceptRole) {
            return false;
        }
    }

    if (m_tree != m_ui->treeFlatpak) {
        m_lockfile.lock();
    }

    if (m_tree != m_ui->treeFlatpak) {
        if (action == "install") {
            detailed_to_install = detailed_names;
        } else {
            detailed_removed_names = detailed_names;
        }
        if (!detailed_removed_names.isEmpty())
            detailed_removed_names.prepend(tr("Remove") + "\n");
        if (!detailed_to_install.isEmpty())
            detailed_to_install.prepend(tr("Install") + "\n");
    } else {
        if (action == "remove") {
            detailed_removed_names = m_change_list.join("\n");
            detailed_to_install.clear();
        }
        if (action == "install") {
            detailed_to_install = m_change_list.join("\n");
            detailed_removed_names.clear();
        }
    }

    msg = "<b>" + tr("The following packages were selected. Click Show Details for list of changes.") + "</b>";

    QMessageBox msgBox;
    msgBox.setText(msg);
    msgBox.setInformativeText("\n" + names + "\n\n" + summary.c_str());

    if (action == "install")
        msgBox.setDetailedText(detailed_to_install + "\n" + detailed_removed_names);
    else
        msgBox.setDetailedText(detailed_removed_names + "\n" + detailed_to_install);

    msgBox.addButton(QMessageBox::Ok);
    msgBox.addButton(QMessageBox::Cancel);

    // make it wider
    auto horizontalSpacer = new QSpacerItem(600, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto layout           = qobject_cast<QGridLayout*>(msgBox.layout());
    layout->addItem(horizontalSpacer, 0, 1);
    return msgBox.exec() == QMessageBox::Ok;
}

// Install the list of apps
bool MainWindow::install(const QString& names) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    m_lockfile.unlock();
    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Installing packages..."));

    // simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    bool is_ok{};
    if (!confirmActions(names, "install", is_ok))
        return true;

    displayOutput();
    bool success = false;
    if (is_ok) {
        success = m_cmd.run(fmt::format("pacman -S {}", names.toStdString()).c_str());
    } else {
        success = m_cmd.run(fmt::format("yes | pacman -S {}", names.toStdString()).c_str());
    }
    m_lockfile.lock();

    return success;
}

// install a list of application and run postprocess for each of them.
bool MainWindow::installBatch(const QStringList& name_list) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    QString install_names;
    bool result = true;

    // load all the
    for (const QString& name : name_list) {
        for (const QStringList& list : m_popular_apps) {
            if (list.at(Popular::Name) == name) {
                install_names += list.at(Popular::InstallNames) + QStringLiteral(" ");
            }
        }
    }

    if (!install_names.isEmpty())
        if (!install(install_names))
            result = false;

    displayOutput();
    m_lockfile.lock();
    return result;
}

// install named app
bool MainWindow::installPopularApp(const QString& name) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    bool result = true;
    QString install_names;

    // get all the app info
    for (const QStringList& list : m_popular_apps) {
        if (list.at(Popular::Name) == name) {
            install_names = list.at(Popular::InstallNames);
        }
    }
    displayOutput();
    // install
    if (!install_names.isEmpty()) {
        m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Installing ") + name);
        result = install(install_names);
    }
    displayOutput();
    m_lockfile.lock();
    return result;
}

// Process checked items to install
bool MainWindow::installPopularApps() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    QStringList batch_names;
    bool result = true;

    if (!m_updated_once)
        update();

    // make a list of apps to be installed together
    for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it) {
        if ((*it)->checkState(PopCol::Check) == Qt::Checked) {
            QString name = (*it)->text(2);
            for (const QStringList& list : m_popular_apps) {
                if (list.at(Popular::Name) == name) {
                    batch_names << name;
                    (*it)->setCheckState(PopCol::Check, Qt::Unchecked);
                }
            }
        }
    }

    if (!installBatch(batch_names))
        result = false;

    // install the rest of the apps
    for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it) {
        if ((*it)->checkState(PopCol::Check) == Qt::Checked) {
            if (!installPopularApp((*it)->text(PopCol::Name)))
                result = false;
        }
    }
    setCursor(QCursor(Qt::ArrowCursor));
    return result;
}

// Install selected items
bool MainWindow::installSelected() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);
    QString names = m_change_list.join(" ");

    bool result = install(names);
    m_change_list.clear();
    m_installed_packages = listInstalled();
    return result;
}

// check if the name is filtered (lib, dev, dbg, etc.)
bool MainWindow::isFilteredName(const QString& name) {
    return ((name.startsWith(QLatin1String("lib")) && !name.startsWith(QLatin1String("libreoffice")))
        || name.endsWith(QLatin1String("-dev")) || name.endsWith(QLatin1String("-dbg")) || name.endsWith(QLatin1String("-dbgsym"))
        || name.endsWith(QLatin1String("-devel")));
}

// Build the list of available packages from various source
bool MainWindow::buildPackageLists(bool force_download) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    clearUi();
    if (!downloadPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    displayPackages();
    return true;
}

// Download the Packages.gz from sources
bool MainWindow::downloadPackageList(bool force_download) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    m_progress->setLabelText(tr("Downloading package info..."));
    m_pushCancel->setEnabled(true);

    if (m_repo_list.empty() || force_download) {
        if (force_download) {
            m_progress->show();
            if (!update())
                return false;
        }
        m_progress->show();
        PacmanCache cache(m_handle);
        m_repo_list = cache.get_candidates();
        if (m_repo_list.empty()) {
            update();
            cache.refresh_list();
            m_repo_list = cache.get_candidates();
        }
    }

    return true;
}

void MainWindow::enableTabs(bool enable) {
    for (int tab = 0; tab < m_ui->tabWidget->count() - 1; ++tab)  // enable all except last (Console)
        m_ui->tabWidget->setTabEnabled(tab, enable);
}

// Cancel download
void MainWindow::cancelDownload() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_cmd.terminate();
}

void MainWindow::centerWindow() {
    const auto screenGeometry = qApp->screens().first()->geometry();
    const auto x              = (screenGeometry.width() - this->width()) / 2;
    const auto y              = (screenGeometry.height() - this->height()) / 2;
    this->move(x, y);
}

// Clear UI when building package list
void MainWindow::clearUi() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    blockSignals(true);
    m_ui->pushCancel->setEnabled(true);
    m_ui->pushInstall->setEnabled(false);
    m_ui->pushUninstall->setEnabled(false);

    if (m_tree == m_ui->treeRepo || m_tree == m_ui->treePopularApps) {
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

// Copy QTreeWidgets
void MainWindow::copyTree(QTreeWidget* from, QTreeWidget* to) const {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    to->clear();
    QTreeWidgetItem* item;

    for (QTreeWidgetItemIterator it(from); *it; ++it) {
        item = (*it)->clone();
        to->addTopLevelItem(item);
    }
}

// Cleanup environment when window is closed
void MainWindow::cleanup() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    m_lockfile.unlock();

    m_cmd.halt();
    m_settings.setValue("geometry", saveGeometry());
}

// Get version of the program
QString MainWindow::getVersion(const std::string_view& name) {
    return m_cmd.getCmdOut(fmt::format("pacman -Si {} | grep 'Version' | {} | head -1", name, "awk '{print $3}'").c_str());
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QString& names) const {
    if (names.isEmpty())
        return false;

    return ranges::all_of(names.split("\n"), [this](auto&& name) { return m_installed_packages.contains(name.trimmed()); });
}

// Return true if all the packages in the list are installed
bool MainWindow::checkInstalled(const QStringList& name_list) const {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    if (name_list.isEmpty())
        return false;

    return ranges::all_of(name_list, [this](auto&& name) { return m_installed_packages.contains(name); });
}

// return true if all the items in the list are upgradable
bool MainWindow::checkUpgradable(const QStringList& name_list) const {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    if (name_list.isEmpty())
        return false;

    return ranges::all_of(name_list, [this](auto&& name) {
        auto item_list = m_tree->findItems(name, Qt::MatchExactly, TreeCol::Name);
        return !(item_list.isEmpty() || item_list.at(0)->text(TreeCol::Status) != QLatin1String("upgradable"));
    });
}

// Returns list of all installed packages
QStringList MainWindow::listInstalled() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    disconnect(m_conn);
    QString str = m_cmd.getCmdOut("pacman -Qq");
    m_conn      = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& out) { spdlog::debug("{}", out.trimmed().toStdString()); });
    return str.split("\n");
}

// Return list flatpaks from current remote
QStringList MainWindow::listFlatpaks(const QString& remote, const QString& type) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    static bool updated = false;

    bool success = false;
    QString out;
    QStringList list;

    // need to specify arch for older version (flatpak takes different format than dpkg)
    QString arch_fp;
    if (m_arch == "amd64")
        arch_fp = "--arch=x86_64 ";
    else if (m_arch == "i386")
        arch_fp = "--arch=i386 ";
    else if (m_arch == "armhf")
        arch_fp = "--arch=arm ";
    else
        return {};

    disconnect(m_conn);
    if (m_fp_ver < VersionNumber("1.0.1")) {
        // list packages, strip first part remote/ or app/ no size for old flatpak
        success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"set -o pipefail; flatpak -d remote-ls " + m_user
                + remote + " " + arch_fp + type + R"( 2>/dev/null| cut -f1 | tr -s ' ' | cut -f1 -d' '|sed 's/^[^\/]*\///g' ")",
            out);
        list    = out.split("\n");
    } else if (m_fp_ver < VersionNumber("1.2.4")) {  // lower than Buster version
        // list size too
        success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"set -o pipefail; flatpak -d remote-ls " + m_user
                + remote + " " + arch_fp + type + R"( 2>/dev/null| cut -f1,3 |tr -s ' ' | sed 's/^[^\/]*\///g' ")",
            out);
        list    = out.split("\n");
    } else {  // Buster version and above
        if (!updated) {
            success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"flatpak update --appstream\"");
            updated = true;
        }
        // list version too
        if (type == "--app" || type.isEmpty()) {
            success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"set -o pipefail; flatpak remote-ls " + m_user
                    + remote + " " + arch_fp + " --app --columns=ver,ref,installed-size 2>/dev/null\"",
                out);
            list    = out.split("\n");
            if (list == QStringList(""))
                list = QStringList();
        }
        if (type == "--runtime" || type.isEmpty()) {
            success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"set -o pipefail; flatpak remote-ls " + m_user
                    + remote + " " + arch_fp + " --runtime --columns=branch,ref,installed-size 2>/dev/null\"",
                out);
            list += out.split("\n");
            if (list == QStringList(""))
                list = QStringList();
        }
    }
    m_conn = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& cmd_out) { spdlog::debug("{}", cmd_out.trimmed().toStdString()); });

    if (!success || list == QStringList("")) {
        spdlog::error("Could not list packages from {} remote, or remote doesn't contain packages", remote.toStdString());
        return {};
    }
    return list;
}

// list installed flatpaks by type: apps, runtimes, or all (if no type is provided)
QStringList MainWindow::listInstalledFlatpaks(const std::string_view& type) {
    QStringList list;
    std::string flatpak_cmd;
    if (m_fp_ver < VersionNumber("1.2.4")) {
        flatpak_cmd = fmt::format("runuser -s /bin/bash -l $(logname) -c \"flatpak -d list {} {} 2>/dev/null |cut -f1|cut -f1 -d' '\"", m_user.toStdString(), type);
    } else {
        flatpak_cmd = fmt::format("runuser -s /bin/bash -l $(logname) -c \"flatpak list {} {} --columns=ref 2>/dev/null\"", m_user.toStdString(), type);
    }
    list << m_cmd.getCmdOut(flatpak_cmd.c_str())
                .remove(" ")
                .split("\n");

    if (list == QStringList(""))
        list = QStringList();
    return list;
}

// return the visible tree
void MainWindow::setCurrentTree() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    const QList<QTreeWidget*> list({m_ui->treePopularApps, m_ui->treeRepo, m_ui->treeFlatpak});

    for (auto item : list) {
        if (item->isVisible()) {
            m_tree = item;
            return;
        }
    }
}

std::unordered_map<QString, VersionNumber> MainWindow::listInstalledVersions() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    disconnect(m_conn);
    QString out = m_cmd.getCmdOut("pacman -Q", true);
    m_conn      = connect(&m_cmd, &Cmd::outputAvailable, [](const QString& cmd_out) { spdlog::debug("{}", cmd_out.trimmed().toStdString()); });

    QString name;
    std::string ver_str;
    QStringList item;
    std::unordered_map<QString, VersionNumber> result;

    const QStringList list = out.split("\n");
    for (const QString& line : list) {
        item         = line.simplified().split(" ");
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

// Display info when clicking the "info" icon of the package
void MainWindow::displayInfo(const QTreeWidgetItem* item, int column) {
    if (column != PopCol::Info || item->childCount() > 0)
        return;

    QString desc          = item->text(PopCol::Description);
    QString install_names = item->text(PopCol::InstallNames);
    QString title         = item->text(PopCol::Name);
    QString msg           = "<b>" + title + "</b><p>" + desc + "<p>";
    if (!install_names.isEmpty())
        msg += tr("Packages to be installed: ") + install_names;
    QMessageBox info(QMessageBox::NoIcon, tr("Package info"), msg, QMessageBox::Close);
    info.exec();
}

void MainWindow::displayPackageInfo(const QTreeWidgetItem* item) {
    QString msg     = m_cmd.getCmdOut("pacman -Si " + item->text(2));
    QString details = m_cmd.getCmdOut("pacman -Siv " + item->text(2));

    auto detail_list  = details.split("\n");
    auto msg_list     = msg.split("\n");
    auto max_no_chars = 2000;         // around 15-17 lines
    auto max_no_lines = 17;           // cut message after these many lines
    if (msg.size() > max_no_chars) {  // split msg into details if too large
        msg         = msg_list.mid(0, max_no_lines).join("\n");
        detail_list = msg_list.mid(max_no_lines, msg_list.length()) + QStringList{""} + detail_list;
        details     = detail_list.join("\n");
    }
    msg += "\n\n" + detail_list.at(detail_list.size() - 2);  // add info about space needed/freed

    QMessageBox info(QMessageBox::NoIcon, tr("Package info"), msg.trimmed(), QMessageBox::Close);
    info.setDetailedText(details.trimmed());

    // make it wider
    auto horizontalSpacer = new QSpacerItem(this->width(), 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto layout           = qobject_cast<QGridLayout*>(info.layout());
    layout->addItem(horizontalSpacer, 0, 1);
    info.exec();
}

// Find package in view
void MainWindow::findPopular() const {
    QString word = m_ui->searchPopular->text();
    if (word.length() == 1)
        return;

    if (word.isEmpty()) {
        for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it)
            (*it)->setExpanded(false);
        m_ui->treePopularApps->reset();
        for (int i = 0; i < m_ui->treePopularApps->columnCount(); ++i)
            m_ui->treePopularApps->resizeColumnToContents(i);
        return;
    }
    auto found_items = m_ui->treePopularApps->findItems(word, Qt::MatchContains | Qt::MatchRecursive, 2);
    found_items << m_ui->treePopularApps->findItems(word, Qt::MatchContains | Qt::MatchRecursive, 4);

    // hide/show items
    for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it) {
        if ((*it)->childCount() == 0) {  // if child
            if (found_items.contains(*it)) {
                (*it)->setHidden(false);
            } else {
                (*it)->parent()->setHidden(true);
                (*it)->setHidden(true);
            }
        }
    }

    // process found items
    for (auto item : found_items) {
        if (item->childCount() == 0) {  // if child, expand parent
            item->parent()->setExpanded(true);
            item->parent()->setHidden(false);
        } else {  // if parent, expand children
            item->setExpanded(true);
            item->setHidden(false);
            int count = item->childCount();
            for (int i = 0; i < count; ++i)
                item->child(i)->setHidden(false);
        }
    }
    for (int i = 0; i < m_ui->treePopularApps->columnCount(); ++i)
        m_ui->treePopularApps->resizeColumnToContents(i);
}

// Find packages in other sources
void MainWindow::findPackageOther() {
    QString word;
    if (m_tree == m_ui->treeRepo)
        word = m_ui->searchBoxRepo->text();
    else if (m_tree == m_ui->treeFlatpak)
        word = m_ui->searchBoxFlatpak->text();
    if (word.length() == 1)
        return;

    auto found_items = m_tree->findItems(word, Qt::MatchContains, TreeCol::Name);
    if (m_tree != m_ui->treeFlatpak)  // not for treeFlatpak as it has a different column structure
        found_items << m_tree->findItems(word, Qt::MatchContains, TreeCol::Description);

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        (*it)->setHidden((*it)->text(TreeCol::Displayed) != QLatin1String("true") || !found_items.contains(*it));
        // Hide libs
        if (isFilteredName((*it)->text(TreeCol::Name)) && m_ui->checkHideLibs->isChecked())
            (*it)->setHidden(true);
    }
}

void MainWindow::showOutput() {
    m_ui->outputBox->clear();
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);
    m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);
    enableTabs(false);
}

// Install button clicked
void MainWindow::on_pushInstall_clicked() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    // qDebug() << "change list"  << .join(" ");
    showOutput();

    if (m_tree == m_ui->treePopularApps) {
        bool success = installPopularApps();
        if (!m_repo_list.empty())  // clear cache to update list if it already exists
            buildPackageLists();
        if (success) {
            refreshPopularApps();
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
        } else {
            refreshPopularApps();
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    } else if (m_tree == m_ui->treeFlatpak) {
        // confirmation dialog
        bool is_ok{};
        if (!confirmActions(m_change_list.join(" "), "install", is_ok)) {
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
        if (m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"socat SYSTEM:'flatpak install -y " + m_user
                + m_ui->comboRemote->currentText() + " " + m_change_list.join(" ") + "',stderr STDIO\"")) {
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    } else {
        bool success = installSelected();
        buildPackageLists();
        refreshPopularApps();
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
void MainWindow::on_pushAbout_clicked() {
    displayAboutMsgBox(tr("About %1").arg(this->windowTitle()), "<p align=\"center\"><b><h2>" + this->windowTitle() + "</h2></b></p><p align=\"center\">" + tr("Version: ") + VERSION + "</p><p align=\"center\"><h3>" + tr("Package Installer for CachyOS") + R"(</h3></p><p align="center"><a href="http://cachyos.org">http://cachyos.org</a><br /></p><p align="center">)" + tr("Copyright (c) CachyOS") + "<br /><br /></p>",
        "/usr/share/doc/cachyos-packageinstaller/license.html", true);
}
// Help button clicked
void MainWindow::on_pushHelp_clicked() {
    QString url = "/usr/share/doc/cachyos-packageinstaller/cachyos-pi.html";
    displayDoc(url, true);
}

// Resize columns when expanding
void MainWindow::on_treePopularApps_expanded() {
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Name);
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Description);
}

// Tree item expanded
void MainWindow::on_treePopularApps_itemExpanded(QTreeWidgetItem* item) {
    item->setIcon(PopCol::Icon, QIcon::fromTheme("folder-open"));
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Name);
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Description);
}

// Tree item collapsed
void MainWindow::on_treePopularApps_itemCollapsed(QTreeWidgetItem* item) {
    item->setIcon(PopCol::Icon, QIcon::fromTheme("folder"));
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Name);
    m_ui->treePopularApps->resizeColumnToContents(PopCol::Description);
}

// Uninstall clicked
void MainWindow::on_pushUninstall_clicked() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);

    showOutput();

    QString names;

    if (m_tree == m_ui->treePopularApps) {
        for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it) {
            if ((*it)->checkState(PopCol::Check) == Qt::Checked) {
                names += (*it)->text(PopCol::UninstallNames).replace("\n", " ") + " ";
            }
        }
    } else if (m_tree == m_ui->treeFlatpak) {
        bool success = true;

        // new version of flatpak takes a "-y" confirmation
        QString conf = "-y ";
        if (m_fp_ver < VersionNumber("1.0.1"))
            conf = QString();
        // confirmation dialog
        bool is_ok{};
        if (!confirmActions(m_change_list.join(" "), "remove", is_ok)) {
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            listFlatpakRemotes();
            m_ui->comboRemote->setCurrentIndex(0);
            on_comboRemote_activated(m_ui->comboRemote->currentIndex());
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
            enableTabs(true);
            return;
        }

        setCursor(QCursor(Qt::BusyCursor));
        for (const QString& app : m_change_list) {
            displayOutput();
            if (!m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"socat SYSTEM:'flatpak uninstall " + conf
                    + app + "',stderr STDIO\""))  // success if all processed successfully, failure if one failed
                success = false;
        }
        if (success) {  // success if all processed successfully, failure if one failed
            displayFlatpaks(true);
            m_indexFilterFP.clear();
            listFlatpakRemotes();
            m_ui->comboRemote->setCurrentIndex(0);
            on_comboRemote_activated(m_ui->comboRemote->currentIndex());
            m_ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling, please check output"));
        }
        enableTabs(true);
        return;
    } else {
        names = m_change_list.join(" ");
    }

    if (uninstall(names)) {
        if (!m_repo_list.empty())  // update list if it already exists
            buildPackageLists();
        refreshPopularApps();
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        if (!m_repo_list.empty())  // update list if it already exists
            buildPackageLists();
        refreshPopularApps();
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
}

// Actions on switching the tabs
void MainWindow::on_tabWidget_currentChanged(int index) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Console Output"));
    m_ui->pushInstall->setEnabled(false);
    m_ui->pushUninstall->setEnabled(false);

    // reset checkboxes when tab changes
    if (m_tree != m_ui->treePopularApps) {
        m_tree->blockSignals(true);
        m_tree->clearSelection();

        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            (*it)->setCheckState(0, Qt::Unchecked);
        m_tree->blockSignals(false);
    }

    // save the search text
    QString search_str;
    int filter_idx = 0;
    if (m_tree == m_ui->treePopularApps) {
        search_str = m_ui->searchPopular->text();
    } else if (m_tree == m_ui->treeRepo) {
        search_str = m_ui->searchBoxRepo->text();
        filter_idx = m_ui->comboFilterRepo->currentIndex();
    } else if (m_tree == m_ui->treeFlatpak) {
        search_str = m_ui->searchBoxFlatpak->text();
    }

    bool success = false;
    switch (index) {
    case Tab::Popular:
        m_ui->searchPopular->setText(search_str);
        enableTabs(true);
        setCurrentTree();
        findPopular();
        m_ui->searchPopular->setFocus();
        break;
    case Tab::Repo:
        m_ui->searchBoxRepo->setText(search_str);
        m_ui->pushRemoveOrphan->setVisible(system("test -n \"$(pacman -Qtdq)\"") == 0);
        enableTabs(true);
        setCurrentTree();
        m_change_list.clear();
        if (m_tree->topLevelItemCount() == 0)
            buildPackageLists();
        m_ui->comboFilterRepo->setCurrentIndex(filter_idx);
        findPackageOther();
        m_ui->searchBoxRepo->setFocus();
        break;
    case Tab::Flatpak:
        m_ui->searchBoxFlatpak->setText(search_str);
        enableTabs(true);
        setCurrentTree();
        displayWarning("flatpaks");
        blockInterfaceFP(true);

        if (!checkInstalled("flatpak")) {
            int ans = QMessageBox::question(this, tr("Flatpak not installed"), tr("Flatpak is not currently installed.\nOK to go ahead and install it?"));
            if (ans == QMessageBox::No) {
                m_ui->tabWidget->setCurrentIndex(Tab::Popular);
                break;
            }
            m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->tabOutput), true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabOutput);
            setCursor(QCursor(Qt::BusyCursor));
            install("flatpak");
            m_installed_packages = listInstalled();
            if (!checkInstalled("flatpak")) {
                QMessageBox::critical(this, tr("Flatpak not installed"), tr("Flatpak was not installed"));
                m_ui->tabWidget->setCurrentIndex(Tab::Popular);
                setCursor(QCursor(Qt::ArrowCursor));
                break;
            }
            if (m_ui->treeRepo) {  // mark flatpak installed in stable tree
                auto hashInstalled      = listInstalledVersions();
                VersionNumber installed = hashInstalled.at("flatpak");
                const auto found_items  = m_ui->treeRepo->findItems("flatpak", Qt::MatchExactly, TreeCol::Name);
                for (auto item : found_items) {
                    for (int i = 0; i < m_ui->treeRepo->columnCount(); ++i) {
                        item->setForeground(TreeCol::Name, QBrush(Qt::gray));
                        item->setForeground(TreeCol::Description, QBrush(Qt::gray));
                        item->setToolTip(i, tr("Latest version ") + installed.toQString() + tr(" already installed"));
                    }
                    item->setText(TreeCol::Status, "installed");
                }
            }
            displayOutput();
            success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
            if (!success) {
                QMessageBox::critical(this, tr("Flathub remote failed"), tr("Flathub remote could not be added"));
                m_ui->tabWidget->setCurrentIndex(Tab::Popular);
                setCursor(QCursor(Qt::ArrowCursor));
                break;
            }
            listFlatpakRemotes();
            displayFlatpaks(false);
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::warning(this, tr("Needs re-login"), tr("You might need to logout/login to see installed items in the menu"));
            m_ui->tabWidget->blockSignals(true);
            m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
            m_ui->tabWidget->blockSignals(false);
            m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->tabOutput), tr("Console Output"));
            break;
        }
        setCursor(QCursor(Qt::BusyCursor));
        displayOutput();
        success = m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
        if (!success) {
            QMessageBox::critical(this, tr("Flathub remote failed"), tr("Flathub remote could not be added"));
            m_ui->tabWidget->setCurrentIndex(Tab::Popular);
            setCursor(QCursor(Qt::ArrowCursor));
            break;
        }
        setCursor(QCursor(Qt::ArrowCursor));
        if (m_ui->comboRemote->currentText().isEmpty())
            listFlatpakRemotes();
        displayFlatpaks(false);
        break;
    case Tab::Output:
        m_ui->searchPopular->clear();
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
            if (m_flatpaks_apps.isEmpty())
                m_flatpaks_apps = listFlatpaks(m_ui->comboRemote->currentText(), "--app");
            displayFilteredFP(m_flatpaks_apps, true);
        } else if (arg1 == tr("All runtimes")) {
            if (flatpaks_runtimes.isEmpty())
                flatpaks_runtimes = listFlatpaks(m_ui->comboRemote->currentText(), "--runtime");
            displayFilteredFP(flatpaks_runtimes, true);
        } else if (arg1 == tr("All available")) {
            int total = 0;
            for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                ++total;
                (*it)->setText(FlatCol::Displayed, QStringLiteral("true"));
                (*it)->setHidden(false);
            }
            m_ui->labelNumAppFP->setText(QString::number(total));
        } else if (arg1 == tr("All installed")) {
            displayFilteredFP(m_installed_apps_fp + m_installed_runtimes_fp);
        } else if (arg1 == tr("Not installed")) {
            found_items = m_tree->findItems("not installed", Qt::MatchExactly, FlatCol::Status);
            QStringList new_list;
            for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                if (found_items.contains(*it))
                    new_list << (*it)->text(FlatCol::FullName);
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
            (*it)->setText(TreeCol::Displayed, QStringLiteral("true"));
            (*it)->setHidden(false);
        }
        findPackageOther();
        setSearchFocus();
        m_tree->blockSignals(false);
        return;
    }

    if (arg1 == tr("Upgradable"))
        found_items = m_tree->findItems(QLatin1String("upgradable"), Qt::MatchExactly, TreeCol::Status);
    else if (arg1 == tr("Installed"))
        found_items = m_tree->findItems(QLatin1String("installed"), Qt::MatchExactly, TreeCol::Status);
    else if (arg1 == tr("Not installed"))
        found_items = m_tree->findItems(QLatin1String("not installed"), Qt::MatchExactly, TreeCol::Status);

    m_change_list.clear();
    m_ui->pushUninstall->setEnabled(false);
    m_ui->pushInstall->setEnabled(false);

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        (*it)->setCheckState(TreeCol::Check, Qt::Unchecked);  // uncheck all items
        if (found_items.contains(*it)) {
            (*it)->setHidden(false);
            (*it)->setText(TreeCol::Displayed, QLatin1String("true"));
        } else {
            (*it)->setHidden(true);
            (*it)->setText(TreeCol::Displayed, QLatin1String("false"));
        }
    }
    findPackageOther();
    setSearchFocus();
    m_tree->blockSignals(false);
}

// When selecting on item in the list
void MainWindow::on_treeRepo_itemChanged(QTreeWidgetItem* item) {
    if (item->checkState(TreeCol::Check) == Qt::Checked)
        m_ui->treeRepo->setCurrentItem(item);
    buildChangeList(item);
}

void MainWindow::on_treeFlatpak_itemChanged(QTreeWidgetItem* item) {
    if (item->checkState(FlatCol::Check) == Qt::Checked)
        m_ui->treeFlatpak->setCurrentItem(item);
    buildChangeList(item);
}

// Build the change_list when selecting on item in the tree
void MainWindow::buildChangeList(QTreeWidgetItem* item) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    /* if all apps are uninstalled (or some installed) -> enable Install, disable Uinstall
     * if all apps are installed or upgradable -> enable Uninstall, enable Install
     * if all apps are upgradable -> change Install label to Upgrade;
     */

    QString newapp;
    if (m_tree == m_ui->treeFlatpak) {
        if (m_change_list.isEmpty() && m_indexFilterFP.isEmpty())  // remember the Flatpak combo location first time this is called
            m_indexFilterFP = m_ui->comboFilterFlatpak->currentText();
        newapp = item->text(FlatCol::FullName);
    } else {
        newapp = item->text(TreeCol::Name);
    }

    if (item->checkState(0) == Qt::Checked) {
        m_ui->pushInstall->setEnabled(true);
        m_change_list.append(newapp);
    } else {
        m_change_list.removeOne(newapp);
    }

    if (m_tree != m_ui->treeFlatpak) {
        m_ui->pushUninstall->setEnabled(checkInstalled(m_change_list));
        m_ui->pushInstall->setText(checkUpgradable(m_change_list) ? tr("Upgrade") : tr("Install"));
    } else {  // for Flatpaks allow selection only of installed or not installed items so one clicks on an installed item only installed items should be displayed and the other way round
        m_ui->pushInstall->setText(tr("Install"));
        if (item->text(FlatCol::Status) == QLatin1String("installed")) {
            if (item->checkState(FlatCol::Check) == Qt::Checked && m_ui->comboFilterFlatpak->currentText() != tr("All installed"))
                m_ui->comboFilterFlatpak->setCurrentText(tr("All installed"));
            m_ui->pushUninstall->setEnabled(true);
            m_ui->pushInstall->setEnabled(false);
        } else {
            if (item->checkState(FlatCol::Check) == Qt::Checked && m_ui->comboFilterFlatpak->currentText() != tr("Not installed"))
                m_ui->comboFilterFlatpak->setCurrentText(tr("Not installed"));
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
void MainWindow::on_pushForceUpdateRepo_clicked() {
    m_ui->searchBoxRepo->clear();
    m_ui->comboFilterRepo->setCurrentIndex(0);
    buildPackageLists(true);
}

// Hide/unhide lib/-dev packages
void MainWindow::on_checkHideLibs_toggled(bool checked) {
    for (QTreeWidgetItemIterator it(m_ui->treeRepo); *it; ++it)
        (*it)->setHidden(isFilteredName((*it)->text(TreeCol::Name)) && checked);
    filterChanged(m_ui->comboFilterRepo->currentText());
}

// Upgrade all packages (from Stable repo only)
void MainWindow::on_pushUpgradeAll_clicked() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();

    auto found_items = m_ui->treeRepo->findItems(QLatin1String("upgradable"), Qt::MatchExactly, 5);

    QString names;
    for (QTreeWidgetItemIterator it(m_ui->treeRepo); *it; ++it) {
        if (found_items.contains(*it))
            names += (*it)->text(TreeCol::Name) + " ";
    }

    if (install(names)) {
        buildPackageLists();
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        buildPackageLists();
        QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
    }

    enableTabs(true);
}

// Pressing Enter or buttonEnter should do the same thing
void MainWindow::on_pushEnter_clicked() {
    if (m_tree == m_ui->treeFlatpak && m_ui->lineEdit->text().isEmpty())
        m_cmd.write("y");
    on_lineEdit_returnPressed();
}

// Send the response to terminal process
void MainWindow::on_lineEdit_returnPressed() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    m_cmd.write(m_ui->lineEdit->text().toUtf8() + "\n");
    m_ui->outputBox->appendPlainText(m_ui->lineEdit->text() + "\n");
    m_ui->lineEdit->clear();
    m_ui->lineEdit->setFocus();
}

void MainWindow::on_pushCancel_clicked() {
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

// on change flatpack remote
void MainWindow::on_comboRemote_activated(int) {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    displayFlatpaks(true);
}

void MainWindow::on_pushUpgradeFP_clicked() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    displayOutput();
    if (m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"socat SYSTEM:'flatpak update " + m_user.trimmed() + "',pty STDIO\"")) {
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

void MainWindow::on_pushRemotes_clicked() {
    auto* dialog = new ManageRemotes(this);
    dialog->exec();
    if (dialog->isChanged()) {
        listFlatpakRemotes();
        displayFlatpaks(true);
    }
    if (!dialog->getInstallRef().isEmpty()) {
        showOutput();
        setCursor(QCursor(Qt::BusyCursor));
        displayOutput();
        if (m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"socat SYSTEM:'flatpak install -y " + dialog->getUser() + "--from " + dialog->getInstallRef().replace(":", "\\:") + "',stderr STDIO\"")) {
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

void MainWindow::on_comboUser_activated(int index) {
    static bool updated = false;
    if (index == 0) {
        m_user = "--system ";
    } else {
        m_user = "--user ";
        if (!updated) {
            setCursor(QCursor(Qt::BusyCursor));
            displayOutput();
            m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
            if (m_fp_ver >= VersionNumber("1.2.4")) {
                displayOutput();
                m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"flatpak update --appstream\"");
            }
            setCursor(QCursor(Qt::ArrowCursor));
            updated = true;
        }
    }
    listFlatpakRemotes();
    displayFlatpaks(true);
}

void MainWindow::on_treePopularApps_customContextMenuRequested(const QPoint& pos) {
    auto t_widget = qobject_cast<QTreeWidget*>(focusWidget());
    if (t_widget->currentItem()->childCount() > 0)
        return;
    auto action = new QAction(QIcon::fromTheme("dialog-information"), tr("More &info..."), this);
    QMenu menu(this);
    menu.addAction(action);
    connect(action, &QAction::triggered, [t_widget] { displayInfo(t_widget->currentItem(), 3); });
    menu.exec(m_ui->treePopularApps->mapToGlobal(pos));
    action->deleteLater();
}

void MainWindow::on_treeRepo_customContextMenuRequested(const QPoint& pos) {
    auto t_widget = qobject_cast<QTreeWidget*>(focusWidget());
    auto action   = new QAction(QIcon::fromTheme("dialog-information"), tr("More &info..."), this);
    QMenu menu(this);
    menu.addAction(action);
    connect(action, &QAction::triggered, [this, t_widget] { displayPackageInfo(t_widget->currentItem()); });
    menu.exec(m_ui->treePopularApps->mapToGlobal(pos));
    action->deleteLater();
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape)
        on_pushCancel_clicked();
}

void MainWindow::on_treePopularApps_itemChanged(QTreeWidgetItem* item) {
    if (item->checkState(1) == Qt::Checked)
        m_ui->treePopularApps->setCurrentItem(item);
    bool checked   = false;
    bool installed = true;

    for (QTreeWidgetItemIterator it(m_ui->treePopularApps); *it; ++it) {
        if ((*it)->checkState(PopCol::Check) == Qt::Checked) {
            checked = true;
            if ((*it)->foreground(PopCol::Name) != Qt::gray)
                installed = false;
        }
    }
    m_ui->pushInstall->setEnabled(checked);
    m_ui->pushUninstall->setEnabled(checked && installed);
    if (checked && installed)
        m_ui->pushInstall->setText(tr("Reinstall"));
    else
        m_ui->pushInstall->setText(tr("Install"));
}

void MainWindow::on_pushRemoveUnused_clicked() {
    spdlog::debug("+++ {} +++", __PRETTY_FUNCTION__);
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    displayOutput();
    // new version of flatpak takes a "-y" confirmation
    QString conf = "-y ";
    if (m_fp_ver < VersionNumber("1.0.1"))
        conf = QString();
    if (m_cmd.run("runuser -s /bin/bash -l $(logname) -c \"socat SYSTEM:'flatpak uninstall --unused " + conf + m_user + "',pty STDIO\"")) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        m_ui->tabWidget->blockSignals(true);
        m_ui->tabWidget->setCurrentWidget(m_ui->tabFlatpak);
        m_ui->tabWidget->blockSignals(false);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"), tr("Problem detected during last operation, please inspect the console output."));
    }
    enableTabs(true);
}

void MainWindow::on_pushRemoveOrphan_clicked() {
    QString names = m_cmd.getCmdOut("pacman -Qdtq | tr '\\n' ' '");
    QMessageBox::warning(this, tr("Warning"), tr("Potentially dangerous operation.\nPlease make sure you check carefully the list of packages to be removed."));
    showOutput();
    if (uninstall(names)) {
        if (!m_repo_list.empty())  // update list if it already exists
            buildPackageLists();
        refreshPopularApps();
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        m_ui->tabWidget->setCurrentWidget(m_tree->parentWidget());
    } else {
        if (!m_repo_list.empty())  // update list if it already exists
            buildPackageLists();
        refreshPopularApps();
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
    m_ui->tabWidget->setCurrentIndex(Tab::Repo);
}
