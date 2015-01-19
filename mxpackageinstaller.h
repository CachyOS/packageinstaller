/*****************************************************************************
 * mxpackageinstaller.h
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MX & MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This file is part of MX Package Installer.
 *
 * MX Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#ifndef MXPACKAGEINSTALLER_H
#define MXPACKAGEINSTALLER_H

#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QHash>

namespace Ui {
class mxpackageinstaller;
}

class mxpackageinstaller : public QDialog
{
    Q_OBJECT

protected:
    QProcess *proc;
    QTimer *timer;
    void keyPressEvent(QKeyEvent* event);

public:
    explicit mxpackageinstaller(QWidget *parent = 0);
    ~mxpackageinstaller();

    QString getCmdOut(QString cmd);

    void setup();
    void install();
    void listPackages();
    void preProc(QString preprocess);
    void aptget(QString package);
    void postProc(QString postprocess);

    QString getVersion(QString name);
    QStringList listInstalled();
    bool checkInstalled(QString filename, QString name);

public slots:
    void procStart();
    void procTime();
    void updateDone(int exitCode);
    void preProcDone(int exitCode);
    void aptgetDone(int exitCode);
    void postProcDone(int exitCode);
    void setConnections(QTimer* timer, QProcess* proc);
    void onStdoutAvailable();
    void displayInfo(QTreeWidgetItem* item, int column);

    virtual void on_buttonInstall_clicked();
    virtual void on_buttonAbout_clicked();
    virtual void on_buttonHelp_clicked();
    virtual void on_treeWidget_expanded();
    virtual void on_treeWidget_itemClicked();
    virtual void on_treeWidget_itemExpanded();
    virtual void on_treeWidget_itemCollapsed();

private:
    Ui::mxpackageinstaller *ui;
    QHash<QString, bool> hashPackages;
    QStringList installedPackages;

};

#endif // MXPACKAGEINSTALLER_H
