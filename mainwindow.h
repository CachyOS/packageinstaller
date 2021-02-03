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

#include <QDomDocument>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidgetItem>

#include "cmd.h"
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
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QString version;
    bool buildPackageLists(bool force_download = false);
    bool checkInstalled(const QString &names) const;
    bool checkInstalled(const QStringList &name_list) const;
    bool checkUpgradable(const QStringList &name_list) const;
    bool confirmActions(QString names, QString action);
    bool downloadPackageList(bool force_download = false);
    bool install(const QString &names);
    bool installBatch(const QStringList &name_list);
    bool installPopularApp(const QString &name);
    bool installPopularApps();
    bool installSelected();
    bool isFilteredName(const QString &name) const;
    bool readPackageList(bool force_download = false);
    bool uninstall(const QString &names, const QString &preuninstall = "", const QString &postuninstall = "");
    bool update();

    double convert(const double &number, const QString &unit) const;

    int getDebianVerNum();

    void blockInterfaceFP(bool block);
    void buildChangeList(QTreeWidgetItem *item);
    void cancelDownload();
    void centerWindow();
    void clearUi();
    void copyTree(QTreeWidget *, QTreeWidget *) const;
    void displayFiltered(QStringList list, bool raw = false) const;
    void displayFlatpaks(bool force_update = false);
    void displayPackages();
    void displayPopularApps() const;
    void displayWarning(QString repo);
    void enableTabs(bool enable);
    void ifDownloadFailed();
    void listFlatpakRemotes();
    void listSizeInstalledFP();
    void loadPmFiles();
    void processDoc(const QDomDocument &doc);
    void refreshPopularApps();
    void removeDuplicatesFP();
    void setCurrentTree();
    void setProgressDialog();
    void setSearchFocus();
    void setup();
    void updateInterface();

    QString addSizes(QString arg1, QString arg2);
    QString getDebianVerName();
    QString getLocalizedName(const QDomElement element) const;
    QString getTranslation(const QString item);
    QString getVersion(const QString name);
    QStringList listFlatpaks(const QString remote, const QString type = "");
    QStringList listInstalled();
    QStringList listInstalledFlatpaks(const QString type = "");

protected:
    void keyPressEvent(QKeyEvent* event);

private slots:
    void cleanup();
    void cmdDone();
    void cmdStart();
    void disableOutput();
    void disableWarning(bool checked, QString file);
    void displayInfo(const QTreeWidgetItem *item, int column);
    void displayOutput();
    void filterChanged(const QString &arg1);
    void findPackageOther();
    void findPopular() const;
    void outputAvailable(const QString &output);
    void showOutput();
    void updateBar();
    void checkUnckeckItem();

    void on_buttonAbout_clicked();
    void on_buttonHelp_clicked();
    void on_buttonInstall_clicked();
    void on_buttonUninstall_clicked();
    void on_tabWidget_currentChanged(int index);
    void on_treePopularApps_expanded();
    void on_treePopularApps_itemClicked();
    void on_treePopularApps_itemCollapsed(QTreeWidgetItem *item);
    void on_treePopularApps_itemExpanded(QTreeWidgetItem *item);

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
    Ui::MainWindow *ui;

    QString indexFilterFP;
    bool test_initially_enabled;
    bool updated_once;
    bool warning_backports;
    bool warning_flatpaks;
    bool warning_test;
    int height_app;

    Cmd cmd;
    LockFile *lock_file;
    QHash<QString, VersionNumber> listInstalledVersions();
    QList<QStringList> popular_apps;
    QLocale locale;
    QMap<QString, QStringList> backports_list;
    QMap<QString, QStringList> mx_list;
    QMap<QString, QStringList> stable_list;
    QMetaObject::Connection conn;
    QProgressBar *bar;
    QProgressDialog *progress;
    QPushButton *progCancel;
    QSettings dictionary;
    QString arch;
    QString stable_raw;
    QString user;
    QString ver_name;
    QStringList change_list;
    QStringList flatpaks;
    QStringList flatpaks_apps;
    QStringList flatpaks_runtimes;
    QStringList installed_apps_fp;
    QStringList installed_packages;
    QStringList installed_runtimes_fp;
    QTemporaryDir tmp_dir;
    QTimer timer;
    QTreeWidget *tree; // current/calling tree
    VersionNumber fp_ver;

    QNetworkAccessManager manager;
    QNetworkReply *reply;
    bool checkOnline();
    bool downloadFile(const QString &url, QFile &file);
    bool downloadAndUnzip(const QString &url, QFile &file);
    bool downloadAndUnzip(const QString &url, const QString &repo_name, const QString &branch, const QString &format, QFile &file);


};


#endif


