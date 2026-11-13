// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalog.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

/// What adding an AppImage came to.
struct AppImageResult {
    QString error;  ///< empty on success
    QString name;   ///< what the app calls itself
};

/// One AppImage in the list, read from the menu entry this app wrote for it.
struct AppImageItem {
    Q_GADGET
    QML_VALUE_TYPE(appImageItem)
    Q_PROPERTY(QString id MEMBER id FINAL)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
    Q_PROPERTY(QString version MEMBER version FINAL)
    Q_PROPERTY(QString path MEMBER path FINAL)
    Q_PROPERTY(QString size MEMBER size FINAL)
    Q_PROPERTY(bool missing MEMBER missing FINAL)
    Q_PROPERTY(bool owned MEMBER owned FINAL)
    Q_PROPERTY(bool terminal MEMBER terminal FINAL)

 public:
    QString id;  ///< name of the menu entry without ".desktop"
    QString name;
    QString summary;
    QString version;
    QString path;  ///< the AppImage file the entry starts
    QString size;  ///< for display; empty when the file is gone
    bool missing  = false;
    bool owned    = false;  ///< the file is one this app put into the install folder
    bool terminal = false;  ///< a command line tool
};

/// A file the user picked, before anything is run or copied.
struct AppImageCandidate {
    Q_GADGET
    QML_VALUE_TYPE(appImageCandidate)
    Q_PROPERTY(QUrl url MEMBER url FINAL)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString size MEMBER size FINAL)
    Q_PROPERTY(QString reason MEMBER reason FINAL)
    Q_PROPERTY(bool ok MEMBER ok FINAL)

 public:
    QUrl url;
    QString name;
    QString size;    ///< for display
    QString reason;  ///< why it cannot be added; empty when `ok`
    bool ok = false;
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
    /// Sorted by name.
    Q_PROPERTY(QList<AppImageItem> apps READ apps NOTIFY appsChanged FINAL)
    /// An AppImage is being added.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    /// Why the last add or remove failed; empty after a success.
    Q_PROPERTY(QString error READ error NOTIFY messageChanged FINAL)
    /// Name of the app that was added last; empty after a failure.
    Q_PROPERTY(QString added READ added NOTIFY messageChanged FINAL)
    /// Where the files are kept, for display.
    Q_PROPERTY(QString directory READ directory CONSTANT FINAL)
    /// libfuse 2 is installed; most AppImages need it to start.
    Q_PROPERTY(bool fuseAvailable READ fuseAvailable NOTIFY appsChanged FINAL)

 public:
    explicit AppImageClient(QObject* parent = nullptr);
    ~AppImageClient() override;

    QList<AppImageItem> apps() const { return m_apps; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString added() const { return m_added; }
    QString directory() const;
    bool fuseAvailable() const { return m_fuseAvailable; }

    /// A short description of a file the user picked. Nothing is run or copied yet.
    Q_INVOKABLE AppImageCandidate inspect(const QUrl& file) const;
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
    QList<AppImageItem> m_apps;
    QString m_error;
    QString m_added;
    bool m_busy          = false;
    bool m_fuseAvailable = false;
};
