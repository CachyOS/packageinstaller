/**********************************************************************
 *  cmd.cpp
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://forum.mxlinux.org>
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

#include "cmd.h"

#include <QEventLoop>

#include <QDebug>

Cmd::Cmd(QObject *parent) :
    QObject(parent)
{
    proc = new QProcess(this);
    timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, &Cmd::tick);
    connect(proc, static_cast<void (QProcess::*)(int)>(&QProcess::finished), timer, &QTimer::stop);
}

Cmd::~Cmd()
{
}

// this function is running the command, takes cmd_str and optional estimated completion time
int Cmd::run(const QString &cmd_str, int est_duration)
{
    this->est_duration = est_duration;
    if (proc->state() != QProcess::NotRunning) {
        return -1; // allow only one process at a time
    }

    counter = 0; // init time counter
    output = "";

    proc->start("/bin/bash", QStringList() << "-c" << cmd_str);

    // start timer
    if (proc->state() != QProcess::NotRunning) { // running or starting
      emit started();
      timer->start(100);
    }

    QEventLoop loop;
    connect(proc, static_cast<void (QProcess::*)(int)>(&QProcess::finished), &loop, &QEventLoop::quit);
    connect(proc, &QProcess::readyReadStandardOutput, this, &Cmd::onStdoutAvailable);
    qDebug() << "running cmd:" << proc->arguments().at(1);
    loop.exec();

    emit finished(proc->exitCode(), proc->exitStatus());
    if (proc->exitCode() != 0) {
        qDebug() << "exit code:" << proc->exitCode();
        return proc->exitCode();
    }
    if (proc->exitStatus() != 0) {
        qDebug() << "exit status:" << proc->exitStatus();
        return proc->exitStatus();
    }
    return 0;
}

// kill process, return true for success
bool Cmd::kill()
{
    qDebug() << "kill cmd called";
    if (!this->isRunning()) {
        return true; // returns true because process is not running
    }
    qDebug() << "killing parent process:" << proc->pid();
    proc->kill();
    proc->deleteLater();
    emit finished(proc->exitCode(), proc->exitStatus());
    return (!this->isRunning());
}

// terminate process, return true for success
bool Cmd::terminate()
{
    qDebug() << "terminate cmd called";
    if (!this->isRunning()) {
        return true; // returns true because process is not running
    }
    qDebug() << "terminating parent process:" << proc->pid();
    proc->terminate();
    emit finished(proc->exitCode(), proc->exitStatus());
    return (!this->isRunning());
}

// pause process
void Cmd::pause()
{
    if (!this->isRunning()) {
        return;
    }
    qDebug() << "pausing process";
    qint64 id = proc->processId();
    system("kill -STOP " + id);
}

// resume process
void Cmd::resume()
{
    qDebug() << "restarting process";
    qint64 id = proc->processId();
    system("kill -CONT " + id);
}

// get the output of the command
QString Cmd::getOutput()
{
    return this->output.trimmed();
}

// runs the command passed as argument and return output
QString Cmd::getOutput(const QString &cmd_str)
{
    this->run(cmd_str);
    return this->output.trimmed();
}

// on std out available emit the output
void Cmd::onStdoutAvailable()
{
    QByteArray line_out = proc->readAllStandardOutput();
    if (line_out != "") {
        emit outputAvailable(line_out);
    }
    this->output += line_out;
}

// slot called by timer that emits a counter and the estimated duration to be used by progress bar
void Cmd::tick()
{
    emit runTime(counter, est_duration);
    counter++;
}

// check if process is starting or running
bool Cmd::isRunning()
{
    return (proc->state() != QProcess::NotRunning) ? true : false;
}
