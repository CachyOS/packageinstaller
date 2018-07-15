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
#include <lockfile.h>
#include <versionnumber.h>

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
    bool readPackageList(bool force_download = false);
    bool uninstall(const QString &names);
    bool update();

    void cancelDownload();
    void clearUi() const;
    void copyTree(QTreeWidget *, QTreeWidget *) const;
    void displayPopularApps() const;
    void displayPackages(bool force_refresh = false);
    void displayWarning();
    void ifDownloadFailed();
    void loadPmFiles();
    void processDoc(const QDomDocument &doc);
    void refreshPopularApps();
    void setProgressDialog();
    void setup();
    void updateInterface() const;

    QString getDebianVersion() const;
    QString getLocalizedName(const QDomElement element) const;
    QString getTranslation(const QString item);
    QString getVersion(const QString name) const;
    QStringList listInstalled() const;

public slots:

private slots:
    void cleanup() const;
    void clearCache();
    void cmdStart();
    void cmdDone();
    void disableWarning(bool checked);
    void displayInfo(const QTreeWidgetItem *item, int column);
    void findPackage() const;
    void findPackageOther() const;
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
    void on_comboFilter_activated(const QString &arg1);
    void on_treeOther_itemChanged(QTreeWidgetItem* item);
    void on_radioStable_toggled(bool checked);
    void on_radioMXtest_toggled(bool checked);
    void on_radioBackports_toggled(bool checked);
    void on_buttonForceUpdate_clicked();
    void on_checkHideLibs_clicked(bool checked);
    void on_buttonUpgradeAll_clicked();
    void on_buttonEnter_clicked();
    void on_lineEdit_returnPressed();
    void on_buttonCancel_clicked();

private:
    bool updated_once;
    bool warning_displayed;
    int height_app;

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
    QStringList installed_packages;
    QStringList change_list;
    QMap<QString, QStringList> backports_list;
    QMap<QString, QStringList> mx_list;
    QMap<QString, QStringList> stable_list;
    QTimer *timer;
    QTreeWidget *tree_stable;
    QTreeWidget *tree_mx_test;
    QTreeWidget *tree_backports;
    Ui::MainWindow *ui;
    QSettings dictionary;
    QHash<QString, VersionNumber> listInstalledVersions();

    void setConnections() const;

};


#endif


