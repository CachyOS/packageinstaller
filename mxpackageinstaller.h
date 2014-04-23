/*****************************************************************************
 * mxpackageinstaller.h
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/


#ifndef MXPACKAGEINSTALLER_H
#define MXPACKAGEINSTALLER_H

#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QTreeWidgetItem>

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
    void displaySite(QString site);

    void setup();
    void install();
    void listPackages(void);
    void update(void);
    void preProc(QString preprocess);
    void aptget(QString package);
    void postProc(QString postprocess);

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

};

#endif // MXPACKAGEINSTALLER_H
