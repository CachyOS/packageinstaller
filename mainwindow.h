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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QSettings>
#include <QFile>
#include <QDomDocument>
#include <QProgressDialog>
#include <QTreeWidgetItem>

#include <cmd.h>
#include "remotes.h"
#include "lockfile.h"
#include "versionnumber.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QString version;
    bool checkInstalled(const QString &names) const;
    bool checkInstalled(const QStringList &name_list) const;
    bool checkUpgradable(const QStringList &name_list) const;
    bool checkOnline() const;
    bool buildPackageLists(bool force_download = false);
    bool downloadPackageList(bool force_download = false);
    bool install(const QString &names);
    bool installBatch(const QStringList &name_list);
    bool installPopularApp(const QString &name);
    bool installPopularApps();
    bool installSelected();
    bool isFilteredName(const QString &name) const;
    bool readPackageList(bool force_download = false);
    bool uninstall(const QString &names);
    bool update();

    void buildChangeList(QTreeWidgetItem *item);
    void cancelDownload();
    void clearUi();
    void copyTree(QTreeWidget *, QTreeWidget *) const;
    void displayPopularApps() const;
    void displayFiltered(const QStringList &list) const;
    void displayFlatpaks();
    void displayPackages();
    void displayWarning();
    void enableTabs(bool enable);
    void ifDownloadFailed();
    void listFlatpakRemotes();
    void loadPmFiles();
    void processDoc(const QDomDocument &doc);
    void refreshPopularApps();
    void setCurrentTree();
    void setProgressDialog();
    void setSearchFocus();
    void setup();
    void updateInterface();

    QString getDebianVersion() const;
    QString getLocalizedName(const QDomElement element) const;
    QString getTranslation(const QString item);
    QString getVersion(const QString name) const;
    QStringList listInstalled() const;
    QStringList listFlatpaks(const QString remote) const;
    QStringList listInstalledFlatpaks(const QString type = "") const;


public slots:

private slots:
    void cleanup() const;
    void cmdStart();
    void cmdDone();
    void disableWarning(bool checked);
    void displayInfo(const QTreeWidgetItem *item, int column);
    void filterChanged(const QString &arg1);
    void findPopular() const;
    void findPackageOther();
    void showOutput();
    void updateBar(int, int); // updates progressBar when tick signal is emited
    void updateOutput(const QString out) const;

    void on_buttonInstall_clicked();
    void on_buttonAbout_clicked();
    void on_buttonHelp_clicked();
    void on_treePopularApps_expanded();
    void on_treePopularApps_itemClicked();
    void on_treePopularApps_itemExpanded(QTreeWidgetItem *item);
    void on_treePopularApps_itemCollapsed(QTreeWidgetItem *item);
    void on_buttonUninstall_clicked();
    void on_tabWidget_currentChanged(int index);

    void on_treeStable_itemChanged(QTreeWidgetItem *item);
    void on_treeMXtest_itemChanged(QTreeWidgetItem *item);
    void on_treeBackports_itemChanged(QTreeWidgetItem *item);
    void on_treeFlatpak_itemChanged(QTreeWidgetItem *item);

    void on_buttonForceUpdateStable_clicked();
    void on_buttonForceUpdateMX_clicked();
    void on_buttonForceUpdateBP_clicked();

    void on_checkHideLibs_toggled(bool checked);
    void on_checkHideLibsMX_clicked(bool checked);
    void on_checkHideLibsBP_clicked(bool checked);

    void on_buttonUpgradeAll_clicked();
    void on_buttonEnter_clicked();
    void on_lineEdit_returnPressed();
    void on_buttonCancel_clicked();

    void on_comboRemote_activated(int);
    void on_buttonUpgradeFP_clicked();
    void on_buttonRemotes_clicked();
    void on_comboUser_activated(int index);

private:
    bool updated_once;
    bool warning_displayed;
    int height_app;
    QString indexFilterFP;

    Cmd *cmd;
    LockFile *lock_file;
    QPushButton *progCancel;
    QList<QStringList> popular_apps;
    QLocale locale;
    QProgressBar *bar;
    QProgressDialog *progress;
    QString arch;
    QString stable_raw;
    QString tmp_dir;
    QString ver_name;
    QString user;
    QStringList installed_packages;
    QStringList change_list;
    QMap<QString, QStringList> backports_list;
    QMap<QString, QStringList> mx_list;
    QMap<QString, QStringList> stable_list;
    QTimer *timer;
    QTreeWidget *tree; // current/calling tree
    Ui::MainWindow *ui;
    QSettings dictionary;
    QHash<QString, VersionNumber> listInstalledVersions();

    void setConnections() const;

};


#endif


