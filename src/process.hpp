// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <functional>

/// The system environment with untranslated messages and UTF-8 output, for parsing.
inline QProcessEnvironment plainEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    return env;
}

/// Runs `program` in the plain environment and returns the lines it printed. The list is empty
/// when it failed, ran longer than `timeout` milliseconds or `cancelled` said so meanwhile.
inline QStringList outputLines(const QString& program, const QStringList& arguments, int timeout = 30000,
    const std::function<bool()>& cancelled = {}) {
    QProcess process;
    process.setProcessEnvironment(plainEnvironment());
    process.start(program, arguments);
    for (int waited = 0; !process.waitForFinished(200) && process.state() != QProcess::NotRunning; waited += 200) {
        if (waited >= timeout || (cancelled && cancelled())) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts);
}
