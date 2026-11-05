// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// One package operation: plan (unprivileged `pacman --print`), confirm, run.
///
/// Repository operations run through the pkexec helper; AUR installs are
/// delegated to paru, which runs as the user and elevates through pkexec itself.
class Transaction : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Kind kind READ kind NOTIFY stateChanged)
    Q_PROPERTY(QStringList targets READ targets NOTIFY stateChanged)
    Q_PROPERTY(QVariantList plan READ plan NOTIFY planChanged)
    Q_PROPERTY(QString planError READ planError NOTIFY planChanged)
    Q_PROPERTY(qint64 downloadSize READ downloadSize NOTIFY planChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(QString lastLine READ lastLine NOTIFY logChanged)
    Q_PROPERTY(bool success READ success NOTIFY stateChanged)
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY stateChanged)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY stateChanged)

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
        AurInstall };
    Q_ENUM(Kind)

    explicit Transaction(QObject* parent = nullptr);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] Kind kind() const { return m_kind; }
    [[nodiscard]] QStringList targets() const { return m_targets; }
    [[nodiscard]] QVariantList plan() const { return m_plan; }
    [[nodiscard]] QString planError() const { return m_planError; }
    [[nodiscard]] qint64 downloadSize() const { return m_downloadSize; }
    [[nodiscard]] QString log() const { return m_log; }
    [[nodiscard]] QString lastLine() const { return m_lastLine; }
    [[nodiscard]] bool success() const { return m_success; }
    /// A plain explanation for well-known failures; empty when only the log can tell.
    [[nodiscard]] QString failureReason() const { return m_failureReason; }
    [[nodiscard]] bool cancellable() const { return m_state == Running && m_kind == AurInstall; }

    /// Starts a new operation and computes what it would do. Ignored while one is active.
    Q_INVOKABLE void prepare(Kind kind, const QStringList& targets);
    Q_INVOKABLE void confirm();
    /// Aborts planning/confirmation, or a running AUR build.
    Q_INVOKABLE void cancel();
    /// Returns to Idle after the result has been shown.
    Q_INVOKABLE void dismiss();

 signals:
    void stateChanged();
    void planChanged();
    void logChanged();
    void finished(bool success);

 private:
    void setState(State state);
    void appendLog(const QString& text);
    void onPlanFinished(int exitCode, QProcess::ExitStatus status);
    void onRunFinished(int exitCode, QProcess::ExitStatus status);

    QProcess m_process;
    State m_state = Idle;
    Kind m_kind   = Install;
    QStringList m_targets;
    QVariantList m_plan;
    QString m_planError;
    qint64 m_downloadSize = 0;
    QString m_log;
    QString m_lastLine;
    QString m_failureReason;
    bool m_success = false;
};
