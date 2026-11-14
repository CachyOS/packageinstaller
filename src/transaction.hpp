// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringDecoder>
#include <QStringListModel>
#include <QtQml/qqmlregistration.h>

/// One package that a planned repository operation installs or removes.
struct PlanEntry {
    Q_GADGET
    QML_VALUE_TYPE(planEntry)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(bool requested MEMBER requested FINAL)

 public:
    QString name;
    QString version;
    bool requested = false;  ///< one of the targets rather than a dependency
};

/// One operation on the system, most of them on packages: plan (unprivileged `pacman --print`), confirm, run.
///
/// Repository operations and the mirror ranking run through the pkexec helper; AUR installs are
/// delegated to paru, which runs as the user and elevates through the helper too.
/// Flatpak operations run the flatpak command as the user.
///
/// pacman is never interrupted once it runs. Only work done as the user (planning, paru
/// builds, flatpak) can be stopped, the way Ctrl+C in a terminal stops it: SIGINT to its
/// own process group. The helper puts pacman in a session of its own, out of that reach.
class Transaction : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(Kind kind READ kind NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged FINAL)
    /// What will happen, shown before it is confirmed.
    Q_PROPERTY(QString summary READ summary NOTIFY planChanged FINAL)
    Q_PROPERTY(QString confirmLabel READ confirmLabel NOTIFY stateChanged FINAL)
    Q_PROPERTY(QList<PlanEntry> plan READ plan NOTIFY planChanged FINAL)
    Q_PROPERTY(int planCount READ planCount NOTIFY planChanged FINAL)
    Q_PROPERTY(QString planError READ planError NOTIFY planChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* log READ log CONSTANT FINAL)
    Q_PROPERTY(QString lastLine READ lastLine NOTIFY lastLineChanged FINAL)
    Q_PROPERTY(bool success READ success NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isFlatpak READ isFlatpak NOTIFY stateChanged FINAL)

 public:
    enum State { Idle,
        Planning,
        Confirm,
        Running,
        Finished };
    Q_ENUM(State)
    enum Kind { Install,
        Remove,
        Upgrade,
        AurInstall,
        FlatpakInstall,
        FlatpakRemove,
        FlatpakUpdate,
        FlatpakSetup,
        RateMirrors };
    Q_ENUM(Kind)

    explicit Transaction(QObject* parent = nullptr);
    ~Transaction() override;

    State state() const { return m_state; }
    Kind kind() const { return m_kind; }
    QString title() const;
    QString summary() const;
    QString confirmLabel() const;
    QList<PlanEntry> plan() const { return m_plan; }
    int planCount() const { return static_cast<int>(m_plan.size()); }
    QString planError() const { return m_planError; }
    /// The output, one row per line ("display" role), capped to the last MAX_LOG_LINES.
    QAbstractItemModel* log() { return &m_log; }
    QString lastLine() const { return m_lastLine; }
    bool success() const { return m_success; }
    /// A plain explanation for well-known failures; empty when only the log can tell.
    QString failureReason() const { return m_failureReason; }
    /// A flatpak operation; it runs as the user and Flatpak asks for permission itself.
    bool isFlatpak() const;
    /// Only work done as the user can be stopped; pacman is never interrupted.
    bool cancellable() const { return m_state == Running && (m_kind == AurInstall || isFlatpak()); }

    /// Starts a new operation and computes what it would do. Ignored while one is active.
    Q_INVOKABLE void prepare(Kind kind, const QStringList& targets);
    /// Builds reviewed AUR checkouts. `directories` hold the PKGBUILDs of `names`;
    /// `workingDirectory` must be empty, paru also looks for PKGBUILDs below it.
    void prepareAur(const QStringList& names, const QStringList& directories, const QString& workingDirectory);
    /// Runs `flatpak` with `arguments`; `names` is what the dialog calls the operation.
    /// `cleanup` is a second run that follows a successful first one and may fail.
    void prepareFlatpak(Kind kind, const QStringList& names, const QStringList& arguments, const QStringList& cleanup = {});
    Q_INVOKABLE void confirm();
    /// Aborts planning/confirmation, or interrupts a cancellable running operation.
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void copyLog() const;
    /// Returns to Idle after the result has been shown.
    Q_INVOKABLE void dismiss();

    static constexpr qsizetype MAX_LOG_LINES = 5000;

 signals:
    void stateChanged();
    void planChanged();
    void lastLineChanged();
    void finished(bool success);

 private:
    /// The root helper. An uninstalled build uses the one next to it.
    static QString helperPath();
    void setState(State state);
    void begin(Kind kind, const QStringList& targets);
    void launch(const QString& program, const QStringList& args, const QString& workingDirectory,
        const QProcessEnvironment& env = QProcessEnvironment::systemEnvironment());
    void interrupt();
    void appendLog(const QString& text);
    void onPlanFinished(const QByteArray& output, int exitCode, QProcess::ExitStatus status);
    void onRunFinished(int exitCode, QProcess::ExitStatus status);
    void finish(bool success);
    bool usesHelper() const;

    QPointer<QProcess> m_process;  ///< released, never destroyed, while it runs
    State m_state = Idle;
    Kind m_kind   = Install;
    QStringList m_targets;
    QString m_program;
    QStringList m_args;
    QStringList m_cleanupArgs;  ///< a run of `m_program` after the main one, if not empty
    QString m_workingDirectory;
    QList<PlanEntry> m_plan;
    QString m_planError;
    qint64 m_downloadSize = 0;
    bool m_cleaning       = false;  ///< the cleanup run is the one that is running
    QStringListModel m_log;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QString m_partial;  ///< the line being written, before its '\n'
    QString m_lastLine;
    QString m_failureReason;
    bool m_success                = false;
    bool m_quitOnLastWindowClosed = true;
};
