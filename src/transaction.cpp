// SPDX-License-Identifier: GPL-3.0-or-later
#include "transaction.hpp"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {

QString helperPath() {
    const auto override = qEnvironmentVariable("CACHYOS_PI_HELPER");
    return override.isEmpty() ? QStringLiteral(PI_HELPER_PATH) : override;
}

}  // namespace

Transaction::Transaction(QObject* parent) : QObject(parent) {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        if (m_state == Running) {
            appendLog(QString::fromUtf8(m_process.readAllStandardOutput()));
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_state == Planning) {
            onPlanFinished(exitCode, status);
        } else if (m_state == Running) {
            onRunFinished(exitCode, status);
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        if (m_state == Planning) {
            m_planError = tr("Could not start %1").arg(m_process.program());
            emit planChanged();
            setState(Confirm);
        } else if (m_state == Running) {
            appendLog(tr("Could not start %1\n").arg(m_process.program()));
            m_success = false;
            setState(Finished);
            emit finished(false);
        }
    });
}

void Transaction::setState(State state) {
    m_state = state;
    emit stateChanged();
}

void Transaction::appendLog(const QString& text) {
    QString chunk = text;
    chunk.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    m_log += chunk;
    const auto lines = QStringView(m_log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    m_lastLine       = lines.isEmpty() ? QString() : lines.last().trimmed().toString();
    emit logChanged();
}

void Transaction::prepareAur(const QStringList& names, const QStringList& directories, const QString& workingDirectory) {
    if (m_state != Idle || names.isEmpty() || directories.isEmpty()) {
        return;
    }
    m_directories      = directories;
    m_workingDirectory = workingDirectory;
    begin(AurInstall, names);
}

void Transaction::prepare(Kind kind, const QStringList& targets) {
    // AUR packages are only built from reviewed checkouts, see prepareAur()
    if (m_state != Idle || kind == AurInstall) {
        return;
    }
    m_directories.clear();
    begin(kind, targets);
}

void Transaction::begin(Kind kind, const QStringList& targets) {
    m_kind    = kind;
    m_targets = targets;
    m_plan.clear();
    m_planError.clear();
    m_downloadSize = 0;
    m_log.clear();
    m_lastLine.clear();
    m_success = false;
    m_failureReason.clear();
    emit planChanged();
    emit logChanged();

    if (kind == Upgrade || kind == AurInstall) {
        // A full upgrade refreshes the databases first, and paru resolves AUR
        // dependencies itself; neither can be previewed without doing the work.
        setState(Confirm);
        return;
    }

    QStringList args;
    if (kind == Install) {
        args = {QStringLiteral("-S"), QStringLiteral("--needed")};
    } else {
        args = {QStringLiteral("-Rs")};
    }
    args << QStringLiteral("--print") << QStringLiteral("--print-format") << QStringLiteral("%n\t%v\t%r\t%s")
         << QStringLiteral("--") << targets;

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    m_process.setProcessEnvironment(env);
    setState(Planning);
    m_process.start(QStringLiteral("pacman"), args);
}

void Transaction::onPlanFinished(int exitCode, QProcess::ExitStatus status) {
    const auto output = QString::fromUtf8(m_process.readAllStandardOutput());
    QStringList messages;
    for (const auto& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() == 4) {
            const auto size = fields.at(3).toLongLong();
            m_downloadSize += size;
            m_plan << QVariantMap{
                {QStringLiteral("name"), fields.at(0)},
                {QStringLiteral("version"), fields.at(1)},
                {QStringLiteral("repo"), fields.at(2)},
                {QStringLiteral("size"), size},
                {QStringLiteral("requested"), m_targets.contains(fields.at(0))},
            };
        } else if (!line.startsWith(QLatin1String("warning:")) || !line.contains(QLatin1String("skipping"))) {
            messages << line.trimmed();
        }
    }
    std::ranges::stable_sort(m_plan, [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("requested")).toBool() && !b.toMap().value(QStringLiteral("requested")).toBool();
    });
    if (status != QProcess::NormalExit || exitCode != 0) {
        // ":: removing qt6-base breaks dependency 'qt6-base' required by dolphin", once per dependent
        static const QRegularExpression requiredBy(QStringLiteral("breaks dependency '.*' required by (\\S+)$"));
        QStringList dependents;
        for (const auto& message : std::as_const(messages)) {
            if (const auto match = requiredBy.match(message); match.hasMatch() && !dependents.contains(match.captured(1))) {
                dependents << match.captured(1);
            }
        }
        if (!dependents.isEmpty() && m_kind == Remove) {
            const auto total = static_cast<int>(dependents.size());
            const auto shown = dependents.mid(0, 8).join(QStringLiteral(", "));
            m_planError = tr("%1 cannot be removed: %n installed package(s) depend on it.", nullptr, total).arg(m_targets.join(QStringLiteral(", ")))
                + QLatin1Char('\n')
                + (total > 8 ? tr("Among them: %1, …").arg(shown) : tr("Needed by: %1").arg(shown));
        } else {
            m_planError = messages.isEmpty() ? tr("pacman could not resolve this operation.") : messages.join(QLatin1Char('\n'));
        }
    } else if (m_plan.isEmpty()) {
        m_planError = m_kind == Install ? tr("Already installed and up to date.") : tr("Nothing to do.");
    }
    emit planChanged();
    setState(Confirm);
}

void Transaction::confirm() {
    if (m_state != Confirm || !m_planError.isEmpty()) {
        return;
    }

    QString program = QStringLiteral("pkexec");
    QStringList args;
    if (m_kind == AurInstall) {
        program = QStringLiteral("paru");
        // paru builds the checkouts that were reviewed in our own UI and nothing else: with
        // an AUR address that cannot be reached it stops when a dependency would have to
        // come from the AUR, instead of downloading and building a recipe nobody looked at.
        args = {QStringLiteral("-B"), QStringLiteral("-i"), QStringLiteral("--noconfirm"),
            QStringLiteral("--nosudoloop"), QStringLiteral("--sudo"), QStringLiteral("pkexec"),
            QStringLiteral("--mode=pkgbuilds"),
            QStringLiteral("--aururl"), QStringLiteral("http://127.0.0.1:1"),
            QStringLiteral("--aurrpcurl"), QStringLiteral("http://127.0.0.1:1/rpc")};
        args << m_directories;
        m_process.setWorkingDirectory(m_workingDirectory);
    } else if (QFileInfo(helperPath()).isExecutable()) {
        m_process.setWorkingDirectory(QStringLiteral("/"));
        args << helperPath();
        switch (m_kind) {
        case Install: args << QStringLiteral("install") << m_targets; break;
        case Remove: args << QStringLiteral("remove") << m_targets; break;
        default: args << QStringLiteral("upgrade"); break;
        }
    } else {
        // Uninstalled development build: fall back to pacman behind the generic pkexec prompt.
        m_process.setWorkingDirectory(QStringLiteral("/"));
        args << QStringLiteral("pacman");
        switch (m_kind) {
        case Install: args << QStringLiteral("-S") << QStringLiteral("--noconfirm") << QStringLiteral("--needed") << QStringLiteral("--") << m_targets; break;
        case Remove: args << QStringLiteral("-Rs") << QStringLiteral("--noconfirm") << QStringLiteral("--") << m_targets; break;
        default: args << QStringLiteral("-Syu") << QStringLiteral("--noconfirm"); break;
        }
    }

    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    setState(Running);
    appendLog(QStringLiteral("$ %1 %2\n").arg(program, args.join(QLatin1Char(' '))));
    m_process.start(program, args);
}

void Transaction::onRunFinished(int exitCode, QProcess::ExitStatus status) {
    appendLog(QString::fromUtf8(m_process.readAllStandardOutput()));
    m_success = status == QProcess::NormalExit && exitCode == 0;
    if (!m_success) {
        // pkexec: 126 = dismissed by the user, 127 = not authorized
        if (m_kind != AurInstall && (exitCode == 126 || exitCode == 127)) {
            m_failureReason = tr("Authentication was cancelled or denied. Nothing was changed.");
        } else {
            appendLog(tr("\nThe operation failed (exit code %1).\n").arg(exitCode));
        }
    }
    setState(Finished);
    emit finished(m_success);
}

void Transaction::cancel() {
    switch (m_state) {
    case Planning:
        m_state = Idle;  // set first so the finished handler ignores the kill
        m_process.kill();
        m_process.waitForFinished(2000);
        emit stateChanged();
        break;
    case Confirm:
        setState(Idle);
        break;
    case Running:
        // Never interrupt pacman itself; only a user-level AUR build can be stopped.
        if (m_kind == AurInstall) {
            m_process.terminate();
        }
        break;
    default:
        break;
    }
}

void Transaction::dismiss() {
    if (m_state == Finished) {
        setState(Idle);
    }
}
