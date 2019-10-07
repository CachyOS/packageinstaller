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
#include <QDate>
#include <QDebug>
#include <QIcon>
#include <QLocale>
#include <QScopedPointer>
#include <QTranslator>

#include "mainwindow.h"
#include "lockfile.h"
#include <unistd.h>


QScopedPointer<QFile> logFile;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

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

        QString log_name = "/var/log/mxpi.log";
        if (QFile(log_name).exists()) {
            system("echo '-----------------------------------------------------------\nMXPI SESSION\
                   \n-----------------------------------------------------------' >> " + log_name.toUtf8() + ".old");
            system("cat " + log_name.toUtf8() + " >> " + log_name.toUtf8() + ".old");
            system("rm " + log_name.toUtf8());
        }
        // Set the logging files
        logFile.reset(new QFile(log_name));
        // Open the file logging
        logFile.data()->open(QFile::Append | QFile::Text);
        // Set handler
        qInstallMessageHandler(messageHandler);

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


// The implementation of the handler
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Write to terminal
    QTextStream term_out(stdout);
    term_out << msg << endl;

    // Open stream file writes
    QTextStream out(logFile.data());

    // Write the date of recording
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    // By type determine to what level belongs message
    switch (type)
    {
    case QtInfoMsg:     out << "INF "; break;
    case QtDebugMsg:    out << "DBG "; break;
    case QtWarningMsg:  out << "WRN "; break;
    case QtCriticalMsg: out << "CRT "; break;
    case QtFatalMsg:    out << "FTL "; break;
    default:            out << "OTH"; break;
    }
    // Write to the output category of the message and the message itself
    out << context.category << ": "
        << msg << endl;
    out.flush();    // Clear the buffered data
}
