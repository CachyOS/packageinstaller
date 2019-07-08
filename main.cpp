/**********************************************************************
 *  main.cpp
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

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QIcon>

#include <QDebug>

#include "mainwindow.h"
#include "lockfile.h"
#include <unistd.h>




int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon::fromTheme("mx-packageinstaller"));

    QTranslator qtTran;
    qtTran.load(QString("qt_") + QLocale::system().name());
    a.installTranslator(&qtTran);

    QTranslator appTran;
    appTran.load(QString("mx-packageinstaller_") + QLocale::system().name(), "/usr/share/mx-packageinstaller/locale");
    a.installTranslator(&appTran);

    if (getuid() == 0) {
        // Don't start app if Synaptic/apt-get is running, lock dpkg otherwise while the program runs
        LockFile lock_file("/var/lib/dpkg/lock");
        if (lock_file.isLocked()) {
            QApplication::beep();
            QMessageBox::critical(0, QApplication::tr("Unable to get exclusive lock"),
                                  QApplication::tr("Another package management application (like Synaptic or apt-get), "\
                                                   "is already running. Please close that application first"));
            return EXIT_FAILURE;
        } else {
            lock_file.lock();
        }
        if (QFile("/var/log/mxpi.log").exists()) {
            system("echo '-----------------------------------------------------------\nMXPI SESSION\
                   \n-----------------------------------------------------------' >> /var/log/mxpi.log.old");
            system("cat /var/log/mxpi.log >> /var/log/mxpi.log.old");
            system("rm /var/log/mxpi.log");
        }
        MainWindow w;
        w.show();
        return a.exec();
    } else {
        QApplication::beep();
        QMessageBox::critical(0, QString::null,
                              QApplication::tr("You must run this program as root."));
        return EXIT_FAILURE;
    }
}
