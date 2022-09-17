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

#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include "cmd.hpp"
#include "lockfile.hpp"
#include "remotes.hpp"
#include "versionnumber.hpp"

#include <map>

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

    bool buildPackageLists(bool force_download = false);
    [[nodiscard]] bool checkInstalled(const QString& names) const;
    [[nodiscard]] bool checkInstalled(const QStringList& name_list) const;
    [[nodiscard]] bool checkUpgradable(const QStringList& name_list) const;
    bool confirmActions(const QString& names, const QString& action, bool& is_ok);
    bool downloadPackageList(bool force_download = false);
    bool install(const QString& names);
    bool installBatch(const QStringList& name_list);
    bool installPopularApp(const QString& name);
    bool installPopularApps();
    bool installSelected();
    [[nodiscard]] static bool isFilteredName(const QString& name);
    bool uninstall(const QString& names);
    bool update();

    void blockInterfaceFP(bool block);
    void buildChangeList(QTreeWidgetItem* item);
    void cancelDownload();
    void centerWindow();
    void clearUi();
    void copyTree(QTreeWidget*, QTreeWidget*) const;
    void displayFilteredFP(QStringList list, bool raw = false);
    void displayFlatpaks(bool force_update = false);
    void displayPackages();
    void displayPopularApps() const;
    void displayWarning(const QString& repo);
    void enableTabs(bool enable);
    void ifDownloadFailed();
    void listFlatpakRemotes();
    void listSizeInstalledFP();
    void loadTxtFiles();
    void processFile(const std::string& group, const std::string& category, const std::vector<std::string>& names);
    void refreshPopularApps();
    void removeDuplicatesFP();
    void setCurrentTree();
    void setProgressDialog();
    void setSearchFocus();
    void setup();
    void updateInterface();

    static QString addSizes(const QString& arg1, const QString& arg2);
    QString getVersion(const std::string_view& name);
    QStringList listFlatpaks(const QString& remote, const QString& type = "");
    QStringList listInstalled();
    QStringList listInstalledFlatpaks(const std::string_view& type = "");

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
    static void displayInfo(const QTreeWidgetItem* item, int column);
    void displayOutput();
    void displayPackageInfo(const QTreeWidgetItem* item);
    void filterChanged(const QString& arg1);
    void findPackageOther();
    void findPopular() const;
    void outputAvailable(const QString& output);
    void showOutput();
    void updateBar();

    void on_pushAbout_clicked();
    void on_pushHelp_clicked();
    void on_pushInstall_clicked();
    void on_pushUninstall_clicked();
    void on_tabWidget_currentChanged(int index);
    void on_treePopularApps_expanded();
    void on_treePopularApps_itemCollapsed(QTreeWidgetItem* item);
    void on_treePopularApps_itemExpanded(QTreeWidgetItem* item);

    void on_treeFlatpak_itemChanged(QTreeWidgetItem* item);
    void on_treePopularApps_itemChanged(QTreeWidgetItem* item);
    void on_treeRepo_itemChanged(QTreeWidgetItem* item);

    void on_pushForceUpdateRepo_clicked();

    void on_checkHideLibs_toggled(bool checked);

    void on_lineEdit_returnPressed();
    void on_pushCancel_clicked();
    void on_pushEnter_clicked();
    void on_pushUpgradeAll_clicked();

    void on_comboRemote_activated(int);
    void on_comboUser_activated(int index);
    void on_pushRemotes_clicked();
    void on_pushUpgradeFP_clicked();

    void on_treePopularApps_customContextMenuRequested(const QPoint& pos);
    void on_treeRepo_customContextMenuRequested(const QPoint& pos);

    void on_pushRemoveUnused_clicked();

    void on_pushRemoveOrphan_clicked();

 private:
    Ui::MainWindow* m_ui{};
    alpm_errno_t m_alpm_err{};
    alpm_handle_t* m_handle = alpm_initialize("/", "/var/lib/pacman/", &m_alpm_err);

    QString m_indexFilterFP{};
    bool m_updated_once{};
    bool m_warning_flatpaks{};
    bool m_setup_assistant_mode{};
    int m_height_app{};

    Cmd m_cmd{};
    LockFile m_lockfile{"/var/lib/pacman/db.lck"};
    QList<QStringList> m_popular_apps;
    QLocale m_locale{};
    std::map<QString, QStringList> m_repo_list{};
    QMetaObject::Connection m_conn{};
    QProgressBar* m_bar{};
    QProgressDialog* m_progress{};
    QPushButton* m_pushCancel{};
    QSettings m_settings{};
    QString m_arch{};
    QString m_repo_raw{};
    QString m_user{};
    QString m_ver_name{};
    QStringList m_change_list{};
    QStringList m_flatpaks{};
    QStringList m_flatpaks_apps{};
    QStringList flatpaks_runtimes{};
    QStringList m_installed_apps_fp{};
    QStringList m_installed_packages{};
    QStringList m_installed_runtimes_fp{};
    QTimer m_timer{};
    QTreeWidget* m_tree{};  // current/calling tree
    VersionNumber m_fp_ver{};

    std::unordered_map<QString, VersionNumber> listInstalledVersions();
};

#endif  // MAINWINDOW_HPP
