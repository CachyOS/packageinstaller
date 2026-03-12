/**********************************************************************
 *  mxpackageinstaller.h
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          Dolphin_Oracle
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-packageinstaller.
 *
 * mx-packageinstaller is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-packageinstaller is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
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

#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "alpm_manager.hpp"
#include "cmd.hpp"
#include "remotes.hpp"
#include "versionnumber.hpp"

#include <map>
#include <string_view>

#include <QProgressDialog>
#include <QSettings>
#include <QTimer>
#include <QTreeWidgetItem>

namespace Ui {
class MainWindow;
}

namespace Tab {
enum { Popular,
    Repo,
    Flatpak,
    Output };
}
namespace PopCol {
enum { Icon,
    Check,
    Name,
    Info,
    Description,
    InstallNames,
    UninstallNames };
}
namespace TreeCol {
enum { Check,
    UpdateIcon,
    Name,
    Version,
    Description,
    Status,
    Displayed };
}
namespace FlatCol {
enum { Check,
    ShortName,
    LongName,
    Version,
    Size,
    Status,
    Displayed,
    Duplicate,
    FullName };
}
namespace Popular {
enum { Category,
    Name,
    Description,
    InstallNames,
    UninstallNames,
    Group };
}

class MainWindow : public QDialog {
    Q_OBJECT

 public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    auto buildPackageLists(bool force_download = false) noexcept -> bool;
    [[nodiscard]] auto checkInstalled(const QString& names) const noexcept -> bool;
    [[nodiscard]] auto checkInstalled(const QStringList& name_list) const noexcept -> bool;
    [[nodiscard]] auto checkUpgradable(const QStringList& name_list) const noexcept -> bool;
    auto confirmActions(const QString& names, std::string_view action, bool& is_ok) noexcept -> bool;
    auto fetchPackageList(bool force = false) noexcept -> bool;
    auto install(const QString& names) noexcept -> bool;
    auto installBatch(const QStringList& name_list) noexcept -> bool;
    auto installPopularApp(const QString& name) noexcept -> bool;
    auto installPopularApps() noexcept -> bool;
    auto installSelected() noexcept -> bool;
    [[nodiscard]] static auto isFilteredName(const QString& name) noexcept -> bool;
    bool uninstall(const QString& names) noexcept;

    void blockInterfaceFP(bool block) noexcept;
    void buildChangeList(QTreeWidgetItem* item) noexcept;
    void cancelDownload() noexcept;
    void centerWindow() noexcept;
    void clearUi() noexcept;
    void displayFilteredFP(QStringList list, bool raw = false) noexcept;
    void displayFlatpaks(bool force_update = false) noexcept;
    void displayPackages() noexcept;
    void displayPopularApps() const noexcept;
    void displayWarning(std::string_view repo) noexcept;
    void enableTabs(bool enable) noexcept;
    void ifDownloadFailed() noexcept;
    void listFlatpakRemotes() noexcept;
    void listSizeInstalledFP() noexcept;
    void fetch_net_pkglist() noexcept;
    void processFile(const std::string& group, const std::string& category, const std::vector<std::string>& names) noexcept;
    void refreshPopularApps() noexcept;
    void removeDuplicatesFP() noexcept;
    void setCurrentTree() noexcept;
    void setProgressDialog() noexcept;
    void setSearchFocus() noexcept;
    void setup() noexcept;
    void updateInterface() noexcept;

    void on_push_about() noexcept;
    void on_push_install() noexcept;
    void on_push_uninstall() noexcept;

    void on_push_remove_unused() noexcept;
    void on_push_remove_orphan() noexcept;

    void on_push_force_update_repo() noexcept;
    void on_push_cancel() noexcept;
    void on_push_enter() noexcept;
    void on_push_upgrade_all() noexcept;
    void on_push_remotes() noexcept;
    void on_push_upgrade_flatpak() noexcept;

    void on_current_tab_changed(int index) noexcept;

    static auto addSizes(const QString& arg1, const QString& arg2) noexcept -> QString;
    auto get_package_version(std::string_view name) noexcept -> std::string;
    auto listFlatpaks(const QString& remote, std::string_view type = "") noexcept -> QStringList;
    auto listInstalled() noexcept -> QStringList;
    auto listInstalledFlatpaks(std::string_view type = "") -> QStringList;

    QString m_version{};

 protected:
    void keyPressEvent(QKeyEvent* event) override;

 private slots:
    void checkUncheckItem();
    void cleanup();
    void cmdDone();
    void cmdStart();
    void disableOutput();
    void disableWarning(bool checked);
    void displayInfo(const QTreeWidgetItem* item, int column) const;  // NOLINT
    void displayOutput();
    void displayPackageInfo(const QTreeWidgetItem* item);
    void filterChanged(const QString& arg1);
    void findPackageOther();
    void findPopular() const;
    void outputAvailable(const QString& output);
    void showOutput();
    void updateBar();

    void on_treePopularApps_expanded() noexcept;

    void on_checkHideLibs_toggled(bool checked) noexcept;
    void on_lineEdit_returnPressed() noexcept;

    void on_comboRemote_activated(int) noexcept;
    void on_comboUser_activated(int index) noexcept;

    void on_treePopularApps_itemCollapsed(QTreeWidgetItem* item) noexcept;
    void on_treePopularApps_itemExpanded(QTreeWidgetItem* item) noexcept;
    void on_treePopularApps_itemChanged(QTreeWidgetItem* item) noexcept;

    void on_treeFlatpak_itemChanged(QTreeWidgetItem* item) noexcept;
    void on_treeRepo_itemChanged(QTreeWidgetItem* item) noexcept;

    void on_treePopularApps_customContextMenuRequested(const QPoint& pos) noexcept;
    void on_treeRepo_customContextMenuRequested(const QPoint& pos) noexcept;

 private:
    Ui::MainWindow* m_ui{};

    alpm::AlpmManagerPtr m_alpm_manager;

    QString m_indexFilterFP{};
    bool m_warning_flatpaks{};
    bool m_setup_assistant_mode{};
    int m_height_app{};

    Cmd m_cmd{};
    QList<QStringList> m_popular_apps;
    QLocale m_locale{};
    std::map<QString, QStringList> m_repo_list{};
    QMetaObject::Connection m_conn{};
    QProgressBar* m_bar{};
    QProgressDialog* m_progress{};
    QPushButton* m_pushCancel{};
    QSettings m_settings{};
    QString m_repo_raw{};
    QString m_user{};
    QStringList m_repo_upd_list{};
    QStringList m_change_list{};
    QStringList m_flatpaks{};
    QStringList m_flatpaks_apps{};
    QStringList flatpaks_runtimes{};
    QStringList m_installed_apps_fp{};
    QStringList m_installed_packages{};
    QStringList m_installed_runtimes_fp{};
    QTimer m_timer{};
    QTreeWidget* m_tree{};  // current/calling tree

    std::unordered_map<QString, VersionNumber> listInstalledVersions();
};

#endif  // MAINWINDOW_HPP
