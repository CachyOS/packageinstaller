// SPDX-License-Identifier: GPL-3.0-or-later
#include "transaction.hpp"
#include "process.hpp"

#include <QCoreApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QWindow>

#include <csignal>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array HELPER_KINDS{Transaction::Install, Transaction::Remove, Transaction::Upgrade};
constexpr std::array FLATPAK_KINDS{Transaction::FlatpakInstall, Transaction::FlatpakRemove, Transaction::FlatpakUpdate, Transaction::FlatpakSetup};

/// pacman redraws progress bars after '\r': only the last drawing of a line counts.
QStringView latest(QStringView line) {
    while (line.endsWith(u'\r')) {
        line.chop(1);
    }
    return line.sliced(line.lastIndexOf(u'\r') + 1);
}

}  // namespace

QString Transaction::helperPath() {
    if (auto path = qEnvironmentVariable("CACHYOS_PI_HELPER"); !path.isEmpty()) {
        return path;
    }
    // an uninstalled build runs its own helper, behind polkit's generic prompt
    const auto installed = u"" PI_HELPER_PATH ""_s;
    return QFileInfo::exists(installed) ? installed : QCoreApplication::applicationDirPath() + u"/cachyos-pi-helper"_s;
}

Transaction::Transaction(QObject* parent) : QObject(parent) {}

Transaction::~Transaction() {
    if (m_process && m_state == Running) {
        // ~QProcess would SIGKILL it and wait: let it go and finish on its own
        m_process->setParent(nullptr);
    }
}

bool Transaction::isFlatpak() const {
    return std::ranges::contains(FLATPAK_KINDS, m_kind);
}

QString Transaction::title() const {
    const auto target = m_targets.value(0);
    switch (m_kind) {
    case Install:
    case FlatpakInstall: return tr("Install %1").arg(target);
    case Remove:
    case FlatpakRemove: return tr("Remove %1").arg(target);
    case Upgrade: return tr("Update the system");
    case FlatpakUpdate: return tr("Update Flatpak apps");
    case FlatpakSetup: return tr("Add Flathub");
    default:
        return m_targets.size() > 1 ? tr("Build %n package(s) from the AUR", "", static_cast<int>(m_targets.size()))
                                    : tr("Build %1 from the AUR").arg(target);
    }
}

QString Transaction::summary() const {
    switch (m_kind) {
    case FlatpakInstall:
        return tr("Flatpak will download the app and the runtime it needs, and install them for all users. It asks for permission itself if that is required.");
    case FlatpakRemove:
        return tr("The app will be removed, and with it the runtimes that no other app needs. Its data in your home directory is kept.");
    case FlatpakUpdate: return tr("Every installed Flatpak app and runtime will be updated.");
    case FlatpakSetup: return tr("Flathub will be added as an app source for all users.");
    case Upgrade: return tr("The package databases will be refreshed and every available update installed.");
    case AurInstall:
        return m_targets.size() > 1
            ? tr("paru will download the sources, build %1 as your user and then ask for permission to install them. This can take a while.").arg(m_targets.join(", "_L1))
            : tr("paru will download the sources, build the package as your user and then ask for permission to install it. This can take a while.");
    case Remove: return tr("%n package(s) will be removed:", "", planCount());
    default:
        return tr("%n package(s) will be installed (%1 to download):", "", planCount())
            .arg(QLocale().formattedDataSize(m_downloadSize, 1, QLocale::DataSizeIecFormat));
    }
}

QString Transaction::confirmLabel() const {
    switch (m_kind) {
    case Remove:
    case FlatpakRemove: return tr("Remove");
    case Upgrade:
    case FlatpakUpdate: return tr("Update");
    case FlatpakSetup: return tr("Add");
    default: return tr("Install");
    }
}

bool Transaction::usesHelper() const {
    return std::ranges::contains(HELPER_KINDS, m_kind);
}

void Transaction::setState(State state) {
    if (state == Running) {
        // a closed window must not end the process while pacman runs
        m_quitOnLastWindowClosed = QGuiApplication::quitOnLastWindowClosed();
        QGuiApplication::setQuitOnLastWindowClosed(false);
    } else if (m_state == Running) {
        QGuiApplication::setQuitOnLastWindowClosed(m_quitOnLastWindowClosed);
    }
    m_state = state;
    emit stateChanged();
}

void Transaction::appendLog(const QString& text) {
    m_partial += text;
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype end = 0; (end = m_partial.indexOf(u'\n', start)) >= 0; start = end + 1) {
        lines << latest(QStringView(m_partial).sliced(start, end - start)).toString();
    }
    // keep a trailing '\r': whatever comes next redraws the line
    const auto rest = QStringView(m_partial).sliced(start);
    const auto cr   = rest.size() > 1 ? rest.lastIndexOf(u'\r', rest.size() - 2) : -1;
    m_partial       = rest.sliced(cr + 1).toString();

    if (!lines.isEmpty()) {
        const auto row = m_log.rowCount();
        m_log.insertRows(row, static_cast<int>(lines.size()));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            m_log.setData(m_log.index(static_cast<int>(row + i)), lines.at(i));
        }
        if (const auto excess = m_log.rowCount() - MAX_LOG_LINES; excess > 0) {
            m_log.removeRows(0, static_cast<int>(excess));
        }
    }
    auto last = latest(m_partial).trimmed();
    for (auto it = lines.crbegin(); last.isEmpty() && it != lines.crend(); ++it) {
        last = QStringView(*it).trimmed();
    }
    if (!last.isEmpty() && last != m_lastLine) {
        m_lastLine = last.toString();
        emit lastLineChanged();
    }
}

void Transaction::prepare(Kind kind, const QStringList& targets) {
    // AUR packages are only built from reviewed checkouts, see prepareAur()
    if (m_state != Idle || !std::ranges::contains(HELPER_KINDS, kind)) {
        return;
    }
    m_program = u"pkexec"_s;
    m_args    = {helperPath(), kind == Install ? u"install"_s : kind == Remove ? u"remove"_s : u"upgrade"_s};
    if (kind != Upgrade) {
        m_args << targets;
    }
    m_workingDirectory = u"/"_s;
    begin(kind, targets);
}

void Transaction::prepareAur(const QStringList& names, const QStringList& directories, const QString& workingDirectory) {
    if (m_state != Idle || names.isEmpty() || directories.isEmpty()) {
        return;
    }
    // paru builds the checkouts that were reviewed in our own UI and nothing else: with
    // an AUR address that cannot be reached it stops when a dependency would have to
    // come from the AUR, instead of downloading and building a recipe nobody looked at.
    // Its install steps go through our helper, under one polkit action that remembers
    // the authentication, so the password is asked for once and not once per step.
    m_program = u"paru"_s;
    m_args    = QStringList{u"-B"_s, u"-i"_s, u"--noconfirm"_s, u"--nosudoloop"_s, u"--sudo"_s, u"pkexec"_s,
                   u"--sudoflags"_s, helperPath(), u"--mode=pkgbuilds"_s,
                   u"--aururl"_s, u"http://127.0.0.1:1"_s, u"--aurrpcurl"_s, u"http://127.0.0.1:1/rpc"_s}
        + directories;
    m_workingDirectory = workingDirectory;
    begin(AurInstall, names);
}

void Transaction::prepareFlatpak(Kind kind, const QStringList& names, const QStringList& arguments, const QStringList& cleanup) {
    if (m_state != Idle || !std::ranges::contains(FLATPAK_KINDS, kind) || arguments.isEmpty()) {
        return;
    }
    m_program          = u"flatpak"_s;
    m_args             = arguments;
    m_workingDirectory = u"/"_s;
    begin(kind, names);
    m_cleanupArgs = cleanup;
}

void Transaction::begin(Kind kind, const QStringList& targets) {
    m_kind    = kind;
    m_cleanupArgs.clear();
    m_cleaning = false;
    m_targets = targets;
    m_plan.clear();
    m_planError.clear();
    m_downloadSize = 0;
    m_log.setStringList({});
    m_decoder.resetState();
    m_partial.clear();
    m_lastLine.clear();
    m_success = false;
    m_failureReason.clear();
    emit lastLineChanged();

    if ((usesHelper() || kind == AurInstall) && !QFileInfo(helperPath()).isExecutable()) {
        m_planError = tr("%1 is missing. Reinstall the package installer.").arg(helperPath());
    }
    // A full upgrade refreshes the databases first, paru resolves AUR dependencies itself
    // and flatpak picks the runtimes; none can be previewed without doing the work.
    if (!m_planError.isEmpty() || kind == Upgrade || kind == AurInstall || isFlatpak()) {
        emit planChanged();
        setState(Confirm);
        return;
    }
    emit planChanged();

    setState(Planning);
    launch(u"pacman"_s,
        (kind == Install ? QStringList{u"-S"_s, u"--needed"_s} : QStringList{u"-Rs"_s}) + QStringList{u"--print"_s, u"--print-format"_s, u"%n\t%v\t%r\t%s"_s, u"--"_s}
            + targets,
        u"/"_s, plainEnvironment());
}

void Transaction::launch(const QString& program, const QStringList& args, const QString& workingDirectory, const QProcessEnvironment& env) {
    auto* process = new QProcess(this);
    m_process     = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(workingDirectory);
    process->setProcessEnvironment(env);
    // a group of its own, so one SIGINT reaches everything it started (makepkg, ...)
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (process == m_process && m_state == Running) {
            appendLog(m_decoder(process->readAllStandardOutput()));
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (process != m_process) {
            return;  // planning that was cancelled
        }
        m_process = nullptr;
        if (m_state == Planning) {
            onPlanFinished(process->readAllStandardOutput(), exitCode, status);
        } else if (m_state == Running) {
            appendLog(m_decoder(process->readAllStandardOutput()));
            onRunFinished(exitCode, status);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != m_process) {
            return;
        }
        process->deleteLater();
        m_process          = nullptr;
        const auto message = tr("Could not start %1").arg(process->program());
        if (m_state == Planning) {
            m_planError = message;
            emit planChanged();
            setState(Confirm);
        } else if (m_state == Running) {
            appendLog(message + u'\n');
            finish(false);
        }
    });
    process->start(program, args);
}

void Transaction::interrupt() {
    if (m_process && m_process->processId() > 0) {
        ::kill(-static_cast<pid_t>(m_process->processId()), SIGINT);
    }
}

void Transaction::onPlanFinished(const QByteArray& output, int exitCode, QProcess::ExitStatus status) {
    QStringList messages;
    for (const auto& line : QString::fromUtf8(output).split(u'\n', Qt::SkipEmptyParts)) {
        const auto fields = line.split(u'\t');
        if (fields.size() == 4) {
            m_downloadSize += fields.at(3).toLongLong();
            m_plan << PlanEntry{fields.at(0), fields.at(1), m_targets.contains(fields.at(0))};
        } else if (!line.startsWith("warning:"_L1) || !line.contains("skipping"_L1)) {
            messages << line.trimmed();
        }
    }
    std::ranges::stable_sort(m_plan, std::greater{}, &PlanEntry::requested);
    if (status != QProcess::NormalExit || exitCode != 0) {
        // ":: removing qt6-base breaks dependency 'qt6-base' required by dolphin", once per dependent
        static const QRegularExpression requiredBy(uR"(breaks dependency '.*' required by (\S+)$)"_s);
        QStringList dependents;
        for (const auto& message : std::as_const(messages)) {
            if (const auto match = requiredBy.match(message); match.hasMatch() && !dependents.contains(match.captured(1))) {
                dependents << match.captured(1);
            }
        }
        if (!dependents.isEmpty() && m_kind == Remove) {
            const auto total = static_cast<int>(dependents.size());
            const auto shown = dependents.mid(0, 8).join(u", "_s);
            m_planError = tr("%1 cannot be removed: %n installed package(s) depend on it.", nullptr, total).arg(m_targets.join(u", "_s))
                + u'\n' + (total > 8 ? tr("Among them: %1, …").arg(shown) : tr("Needed by: %1").arg(shown));
        } else {
            m_planError = messages.isEmpty() ? tr("pacman could not resolve this operation.") : messages.join(u'\n');
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
    setState(Running);
    appendLog(u"$ %1 %2\n"_s.arg(m_program, m_args.join(u' ')));
    launch(m_program, m_args, m_workingDirectory);
}

void Transaction::onRunFinished(int exitCode, QProcess::ExitStatus status) {
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    if (m_cleaning) {
        // what was asked for is done; the cleanup is a courtesy
        m_cleaning = false;
        if (!success) {
            appendLog(tr("\nThe unused runtimes could not be removed (exit code %1).\n").arg(exitCode));
        }
        finish(true);
        return;
    }
    if (success && !m_cleanupArgs.isEmpty()) {
        m_cleaning = true;
        appendLog(u"$ %1 %2\n"_s.arg(m_program, m_cleanupArgs.join(u' ')));
        launch(m_program, m_cleanupArgs, m_workingDirectory);
        return;
    }
    if (!success && usesHelper() && (exitCode == 126 || exitCode == 127)) {
        // pkexec: 126 = dismissed by the user, 127 = not authorized
        m_failureReason = tr("Authentication was cancelled or denied. Nothing was changed.");
    } else if (!success) {
        appendLog(tr("\nThe operation failed (exit code %1).\n").arg(exitCode));
    }
    finish(success);
}

void Transaction::finish(bool success) {
    if (!m_partial.isEmpty()) {
        appendLog(u"\n"_s);
    }
    m_success = success;
    setState(Finished);
    emit finished(success);
    // the window may have been closed while the transaction kept the application alive
    if (QGuiApplication::quitOnLastWindowClosed() && std::ranges::none_of(QGuiApplication::topLevelWindows(), &QWindow::isVisible)) {
        QCoreApplication::quit();
    }
}

void Transaction::copyLog() const {
    QGuiApplication::clipboard()->setText(m_log.stringList().join(u'\n'));
}

void Transaction::cancel() {
    switch (m_state) {
    case Planning:
        interrupt();
        m_process = nullptr;  // its finish is ignored, it cleans up after itself
        setState(Idle);
        break;
    case Confirm: setState(Idle); break;
    case Running:
        // Never interrupt pacman itself; only work done as the user can be stopped.
        if (cancellable()) {
            interrupt();
        }
        break;
    default: break;
    }
}

void Transaction::dismiss() {
    if (m_state == Finished) {
        setState(Idle);
    }
}
