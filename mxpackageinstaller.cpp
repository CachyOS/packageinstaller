/*****************************************************************************
 * mxpackageinstaller.cpp
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


#include "mxpackageinstaller.h"
#include "ui_mxpackageinstaller.h"

#include <QWebView>
#include <QFileDialog>
#include <QScrollBar>
#include <QFormLayout>
#include <QKeyEvent>

mxpackageinstaller::mxpackageinstaller(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::mxpackageinstaller)
{
    ui->setupUi(this);
    setup();
}

mxpackageinstaller::~mxpackageinstaller()
{
    delete ui;
}

// setup versious items first time program runs
void mxpackageinstaller::setup() {
    proc = new QProcess(this);
    timer = new QTimer(this);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonInstall->setEnabled(true);
    QStringList columnNames;
    columnNames << "" << "" << "Package" << "Info" << "Description";
    ui->treeWidget->setHeaderLabels(columnNames);
    ui->treeWidget->resizeColumnToContents(0);
    ui->treeWidget->resizeColumnToContents(1);
    ui->treeWidget->resizeColumnToContents(2);
    ui->treeWidget->resizeColumnToContents(3);
    installedPackages = listInstalled();
    listPackages();
}

// Util function
QString mxpackageinstaller::getCmdOut(QString cmd) {
    proc = new QProcess(this);
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    proc->waitForFinished(-1);
    return proc->readAllStandardOutput().trimmed();
}

// parse '/usr/share/mx-packageinsataller/bm' for .bm files and add info in treeWidget
void mxpackageinstaller::listPackages(void) {
    QStringList filter("*.bm");
    QDir dir("/usr/share/mx-packageinstaller/bm");
    QStringList bmfilelist = dir.entryList(filter);
    QTreeWidgetItem *topLevelItem = NULL;

    for (int i = 0; i < bmfilelist.size(); ++i) {
        QStringList info = bmfilelist.at(i).split("-");
        QString name = info.at(1);
        name.chop(3);
        QString category = info.at(0);

        // add package category if treeWidget doesn't already have it
        if (ui->treeWidget->findItems(category, Qt::MatchFixedString, 2).isEmpty()) {
            topLevelItem = new QTreeWidgetItem;
            topLevelItem->setText(2, category);
            ui->treeWidget->addTopLevelItem(topLevelItem);
            // topLevelItem look
            QFont font;
            font.setBold(true);
            topLevelItem->setForeground(2, QBrush(Qt::darkGreen));
            topLevelItem->setFont(2, font);
            topLevelItem->setIcon(0, QIcon("/usr/share/mx-packageinstaller/icons/folder.png"));
        }
        // add package name as childItem to treeWidget
        QTreeWidgetItem *childItem = new QTreeWidgetItem(topLevelItem);
        childItem->setText(2, name);
        childItem->setIcon(3, QIcon("/usr/share/mx-packageinstaller/icons/info.png"));

        // add checkboxes
        childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
        childItem->setCheckState(1, Qt::Unchecked);


        // add description from file
        QString filename = "/usr/share/mx-packageinstaller/bm/" +  bmfilelist.at(i);
        QString cmd = QString("grep FLL_DESCRIPTION= %1 | cut -f 2 -d '='").arg(filename);
        QString out = getCmdOut(cmd);
        out.remove(QChar('"'));
        childItem->setText(4, out);

        // add full name of file in treeWidget, but don't display it
        childItem->setText(5, filename);

        // gray out installed items
        if (checkInstalled(filename, name)) {
            childItem->setDisabled(true);
        }
    }
    ui->treeWidget->resizeColumnToContents(0);
    ui->treeWidget->resizeColumnToContents(2);
    ui->buttonInstall->setEnabled(false);
    connect(ui->treeWidget, SIGNAL(itemClicked(QTreeWidgetItem*, int)), SLOT(displayInfo(QTreeWidgetItem*,int)));
}

// get list of packages to install and start install processes
void mxpackageinstaller::install() {
    ui->buttonCancel->setEnabled(false);
    ui->buttonInstall->setEnabled(false);
    ui->outputBox->setText("");
    QString preprocess = "";

    //run apt-get update
    setCursor(QCursor(Qt::WaitCursor));
    update();

    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        (*it)->setSelected(false); // deselect each item
        if ((*it)->checkState(1) == Qt::Checked) {
            QString filename =  (*it)->text(5);
            (*it)->setSelected(true);                // select current item for passing to other functions
            QString cmd_preprocess = "source " + filename + " && printf '%s\\n' \"${FLL_PRE_PROCESSING[@]}\"";
            preprocess = getCmdOut(cmd_preprocess);
            preProc(preprocess);
        }
        ++it;
    }
    // process done at the end of cycle
    setCursor(QCursor(Qt::ArrowCursor));
    if (proc->exitCode() == 0) {
        ui->outputLabel->setText(tr("Installation done."));
        if (QMessageBox::information(this, tr("Success"),
                                     tr("Process finished with success.<p><b>Do you want to exit MX Package Installer?</b>"),
                                     tr("Yes"), tr("No")) == 0){
            qApp->exit(0);
        }
    } else {
        QMessageBox::critical(this, tr("Error"),
                              tr("Postprocess finished. Errors have occurred."));
    }
    ui->buttonCancel->setEnabled(true);
    ui->buttonInstall->setEnabled(true);
    ui->buttonInstall->setText(tr("< Back"));
    ui->buttonInstall->setIcon(QIcon());
}

// run update
void mxpackageinstaller::update() {
    QString outLabel = tr("Running apt-get update... ");
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    ui->progressBar->setValue(0);
    ui->outputLabel->setText(outLabel);
    setConnections(timer, proc);
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
    connect(proc, SIGNAL(finished(int)), this, SLOT(updateDone(int)));
    QEventLoop loop;
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    QString cmd = "apt-get update";
    proc->start(cmd);
    QString out = ui->outputBox->toPlainText() + "# " + cmd + "\n";
    ui->outputBox->setPlainText(out);
    loop.exec();
}

// run preprocess
void mxpackageinstaller::preProc(QString preprocess) {
    QString outLabel = tr("Pre-processing... ");
    ui->outputLabel->setText(outLabel);
    setConnections(timer, proc);
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
    connect(proc, SIGNAL(finished(int)), this, SLOT(preProcDone(int)));
    QEventLoop loop;
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("/bin/bash", QStringList() << "-c" << preprocess);
    loop.exec();
}

// run apt-get install
void mxpackageinstaller::aptget(QString package) {
    QString cmd;
    QString outLabel = tr("Installing: ") + package;
    ui->outputLabel->setText(outLabel);
    setConnections(timer, proc);
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
    connect(proc, SIGNAL(finished(int)), SLOT(aptgetDone(int)));
    if (ui->yesCheckBox->isChecked()) {
        cmd = QString("apt-get -y install %1").arg(package);
    } else {
        cmd = QString("apt-get install %1").arg(package);
    }
    QString out = ui->outputBox->toPlainText() + "# " + cmd + "\n";
    ui->outputBox->setPlainText(out);
    QEventLoop loop;
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    loop.exec();
}

// run postprocess
void mxpackageinstaller::postProc(QString postprocess) {
    QString outLabel = tr("Post-processing... ");
    ui->outputLabel->setText(outLabel);
    setConnections(timer, proc);
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
    connect(proc, SIGNAL(finished(int)), SLOT(postProcDone(int)));
    QEventLoop loop;
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("/bin/bash", QStringList() << "-c" << postprocess);
    loop.exec();
}

// returns list of all install packages
QStringList mxpackageinstaller::listInstalled() {
    QString str = getCmdOut("dpkg --get-selections | grep -v deinstall | cut -f1");
    return str.split("\n");
}

// checks if a specific package is already installed
bool mxpackageinstaller::checkInstalled(QString filename, QString name) {
    QString cmd_package = "source " + filename + " && echo ${FLL_PACKAGES[@]}";
    QString packages = getCmdOut(cmd_package);
    QStringList list = packages.split(" ");
    // if no packages listed compare to the name of the program
    if (list.contains("")) {
        if (!installedPackages.contains(name, Qt::CaseInsensitive)) {
            return false;
        }
    } else {
        for (int i = 0; i < list.size(); ++i) {
            if (!installedPackages.contains(list.at(i))) {
                return false;
            }
        }
    }
    return true;
}


//// sync process events ////

void mxpackageinstaller::procStart() {
    timer->start(100);
}

void mxpackageinstaller::procTime() {
    int i = ui->progressBar->value() + 1;
    if (i > 100) {
        i = 0;
    }
    ui->progressBar->setValue(i);
}

void mxpackageinstaller::updateDone(int exitCode) {
    timer->stop();
    ui->progressBar->setValue(100);
}


void mxpackageinstaller::preProcDone(int exitCode) {
    timer->stop();
    ui->progressBar->setValue(100);
    QString package = "";
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->isSelected()) {
            QString filename =  (*it)->text(5);
            QString cmd_package = "source " + filename + " && echo ${FLL_PACKAGES[@]}";
            package = getCmdOut(cmd_package);
            (*it)->setCheckState(1, Qt::Unchecked);
            (*it)->setDisabled(true);

        }
        ++it;
    }
    if (exitCode == 0) {
        ui->outputLabel->setText(tr("Preprocessing done."));
        aptget(package);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"),
                              tr("Pre-process finished. Errors have occurred installing: ") + package);
        setCursor(QCursor(Qt::WaitCursor));
    }
}


void mxpackageinstaller::aptgetDone(int exitCode) {
    timer->stop();
    ui->progressBar->setValue(100);
    QString package;
    QString postprocess = "";
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->isSelected()) {
            QString filename =  (*it)->text(5);
            package = (*it)->text(2);
            QString cmd_postprocess = "source " + filename + " && printf '%s\\n' \"${FLL_POST_PROCESSING[@]}\"";
            postprocess = getCmdOut(cmd_postprocess);
            (*it)->setCheckState(1, Qt::Unchecked);
            (*it)->setSelected(false);
            (*it)->setDisabled(true);
        }
        ++it;
    }
    if (exitCode == 0) {
        ui->outputLabel->setText(tr("Installation done."));
        postProc(postprocess);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"),
                              tr("Errors have occurred installing: ") + package);
        setCursor(QCursor(Qt::WaitCursor));
    }
}

void mxpackageinstaller::postProcDone(int exitCode) {
    timer->stop();
    ui->progressBar->setValue(100);    
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->isSelected()) {
            (*it)->setCheckState(1, Qt::Unchecked);
            (*it)->setSelected(false);
            (*it)->setDisabled(true);
        }
        ++it;
    }
}


// set proc and timer connections
void mxpackageinstaller::setConnections(QTimer* timer, QProcess* proc) {
    disconnect(timer, SIGNAL(timeout()), 0, 0);
    connect(timer, SIGNAL(timeout()), SLOT(procTime()));
    disconnect(proc, SIGNAL(started()), 0, 0);
    connect(proc, SIGNAL(started()), SLOT(procStart()));
    disconnect(proc, SIGNAL(readyReadStandardOutput()), 0, 0);
    connect(proc, SIGNAL(readyReadStandardOutput()), SLOT(onStdoutAvailable()));
}


//// events ////

// process keystrokes
void mxpackageinstaller::keyPressEvent(QKeyEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QString text = event->text();
        const char *c = text.toStdString().c_str();
        proc->write(c);
        proc->closeWriteChannel();
    }
}


//// slots ////

// update output box on Stdout
void mxpackageinstaller::onStdoutAvailable() {
    QByteArray output = proc->readAllStandardOutput();
    QString out = ui->outputBox->toPlainText() + QString::fromUtf8(output);
    ui->outputBox->setPlainText(out);
    QScrollBar *sb = ui->outputBox->verticalScrollBar();
    sb->setValue(sb->maximum());
}



void mxpackageinstaller::displayInfo(QTreeWidgetItem * item, int column) {    
    if (column == 3 && item->childCount() == 0) {
        QString desc = item->text(4);
        QString filename = item->text(5);
        QString cmd_package = "source " + filename + " && echo ${FLL_PACKAGES[@]}";
        QString package = getCmdOut(cmd_package);
        QString title = item->text(2);
        QString msg = "<b>" + title + "</b><p>" + desc + "<p>" + tr("Packages to be installed: ") + package;
        QMessageBox::information(this, tr("Info"), msg, tr("Cancel"));
    }
}

// resize columns when expanding
void mxpackageinstaller::on_treeWidget_expanded() {
    ui->treeWidget->resizeColumnToContents(2);
    ui->treeWidget->resizeColumnToContents(4);
}

void mxpackageinstaller::on_treeWidget_itemClicked() {
    bool checked = false;
    QTreeWidgetItemIterator it(ui->treeWidget);

    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            checked = true;
        }
        ++it;
   }
   if (checked) {
        ui->buttonInstall->setEnabled(true);
   } else {
        ui->buttonInstall->setEnabled(false);
   }
}

void mxpackageinstaller::on_treeWidget_itemExpanded() {
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->isExpanded()) {
            (*it)->setIcon(0, QIcon("/usr/share/mx-packageinstaller/icons/folder-open.png"));
        }
        ++it;
    }
    ui->treeWidget->resizeColumnToContents(4);
}

void mxpackageinstaller::on_treeWidget_itemCollapsed() {
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if (!(*it)->isExpanded()) {
            (*it)->setIcon(0, QIcon("/usr/share/mx-packageinstaller/icons/folder.png"));
        }
        ++it;
    }
    ui->treeWidget->resizeColumnToContents(4);
}

// Install button clicked
void mxpackageinstaller::on_buttonInstall_clicked() {
    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {
        install();
    // on output page
    } else if (ui->stackedWidget->currentWidget() == ui->outputPage) {
        ui->stackedWidget->setCurrentIndex(0);
        ui->buttonInstall->setText(tr("Install"));
        ui->buttonInstall->setIcon(QIcon("/usr/share/mx-packageinstaller/icons/dialog-ok.png"));
        on_treeWidget_itemClicked();
    } else {
        qApp->exit(0);
    }
}


// About button clicked
void mxpackageinstaller::on_buttonAbout_clicked() {
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Package Installer"), "<p align=\"center\"><b><h2>" +
                       tr("MX Package Installer") + "</h2></b></p><p align=\"center\">MX14+git20140806</p><p align=\"center\"><h3>" +
                       tr("Simple package installer for additional packages for antiX MX") + "</h3></p><p align=\"center\"><a href=\"http://www.mepiscommunity.org/mx\">http://www.mepiscommunity.org/mx</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) antiX") + "<br /><br /></p>", 0, this);
    msgBox.addButton(tr("License"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("Cancel"), QMessageBox::DestructiveRole);
    if (msgBox.exec() == QMessageBox::AcceptRole)
        displaySite("file:///usr/share/doc/mx-packageinstaller/license.html");
}


// Help button clicked
void mxpackageinstaller::on_buttonHelp_clicked() {
    displaySite("file:///usr/local/share/doc/mxapps.html#packageinstaller");
}

// pop up a window and display website
void mxpackageinstaller::displaySite(QString site) {
    QWidget *window = new QWidget(this, Qt::Dialog);
    window->setWindowTitle(this->windowTitle());
    window->resize(800, 500);
    QWebView *webview = new QWebView(window);
    webview->load(QUrl(site));
    webview->show();
    window->show();
}
