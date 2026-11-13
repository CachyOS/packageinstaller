// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

/// What adding an AppImage came to.
struct AppImageResult {
    QString error;  ///< empty on success
    QString name;   ///< what the app calls itself
};

/// AppImage files the user added: copied to ~/Applications and given a menu entry and an
/// icon, all as the user. There is no catalogue and no update check; a newer file that is
/// added replaces the older one of the same app.
///
/// The list is not kept in a database of its own: the menu entries written here carry a
/// mark and are the record.
class AppImageClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// [{id, name, summary, version, path, size, missing, owned, terminal}], sorted by name.
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    /// An AppImage is being added.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// Why the last add or remove failed; empty after a success.
    Q_PROPERTY(QString error READ error NOTIFY messageChanged)
    /// Name of the app that was added last; empty after a failure.
    Q_PROPERTY(QString added READ added NOTIFY messageChanged)
    /// Where the files are kept, for display.
    Q_PROPERTY(QString directory READ directory CONSTANT)
    /// libfuse 2 is installed; most AppImages need it to start.
    Q_PROPERTY(bool fuseAvailable READ fuseAvailable NOTIFY appsChanged)

 public:
    explicit AppImageClient(QObject* parent = nullptr);
    ~AppImageClient() override;

    [[nodiscard]] QVariantList apps() const { return m_apps; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString added() const { return m_added; }
    [[nodiscard]] QString directory() const;
    [[nodiscard]] bool fuseAvailable() const { return m_fuseAvailable; }

    /// A short description of a file the user picked: {ok, name, size, reason}. Nothing is
    /// run or copied yet.
    Q_INVOKABLE [[nodiscard]] QVariantMap inspect(const QUrl& file) const;
    /// Copies the file, reads its menu entry and icon (which runs the file's own extractor)
    /// and adds it to the menu.
    Q_INVOKABLE void add(const QUrl& file);
    /// Deletes the AppImage, its menu entry and its icon. `path` is the file the user was
    /// shown; nothing is removed if the entry names another one by now.
    Q_INVOKABLE void remove(const QString& id, const QString& path);
    Q_INVOKABLE void launch(const QString& id);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearMessage();

 signals:
    void appsChanged();
    void busyChanged();
    void messageChanged();
    /// New icons for `image://pkgicon/appimage/<id>`.
    void iconsLoaded(const IconIndex& icons);

 private:
    void setMessage(const QString& error, const QString& added);

    QFutureWatcher<AppImageResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;  ///< set at shutdown to stop the worker
    QVariantList m_apps;
    QString m_error;
    QString m_added;
    bool m_busy          = false;
    bool m_fuseAvailable = false;
};
