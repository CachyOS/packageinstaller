// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <expected>
#include <functional>
#include <memory>

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

/// Why `readOutput()` passes on no output.
enum class OutputFailure { Failed, TooLarge };

/// Starts `program` without waiting for it, with nothing on its standard input. Once it ended, `done` gets what it printed, unless it did not start, did not end
/// with 0, or printed more than `maxSize` bytes; it is stopped then. What it writes to
/// standard error is dropped.
inline void readOutput(QObject* context, const QString& program, const QStringList& arguments, const QProcessEnvironment& env,
    qsizetype maxSize, std::function<void(std::expected<QByteArray, OutputFailure>)> done) {
    struct Run {
        QByteArray output;
        bool tooLarge = false;
    };
    auto run      = std::make_shared<Run>();
    auto* process = new QProcess(context);
    process->setProcessEnvironment(env);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    QObject::connect(process, &QProcess::readyReadStandardOutput, context, [process, run, maxSize] {
        if (run->tooLarge) {
            process->readAllStandardOutput();
            return;
        }
        run->output += process->readAllStandardOutput();
        if (run->output.size() > maxSize) {
            run->tooLarge = true;
            run->output.clear();
            process->kill();
        }
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {  // finished() is not emitted then
            process->deleteLater();
            done(std::unexpected(OutputFailure::Failed));
        }
    });
    QObject::connect(process, &QProcess::finished, context, [process, run, done, maxSize](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!run->tooLarge) {
            run->output += process->readAllStandardOutput();
            run->tooLarge = run->output.size() > maxSize;
        }
        if (run->tooLarge) {
            done(std::unexpected(OutputFailure::TooLarge));
        } else if (status != QProcess::NormalExit || exitCode != 0) {
            done(std::unexpected(OutputFailure::Failed));
        } else {
            done(std::move(run->output));
        }
    });
    process->start(program, arguments);
}
