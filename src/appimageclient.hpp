// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "appimagesource.hpp"
#include "catalog.hpp"

#include <QFutureWatcher>
#include <QNetworkAccessManager>
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
    Q_PROPERTY(QString source MEMBER source FINAL)
    Q_PROPERTY(QString update MEMBER update FINAL)

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
    QString source;         ///< "owner/repo" on GitHub when the file came from there
    QString update;         ///< the newer release that can be installed; empty when there is none known
};

/// One AppImage file of a release, for the user to pick.
struct AppImageChoice {
    Q_GADGET
    QML_VALUE_TYPE(appImageChoice)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString size MEMBER size FINAL)

 public:
    QString name;
    QString size;  ///< for display

    bool operator==(const AppImageChoice&) const = default;
};

/// What a GitHub link led to, before anything is downloaded.
struct AppImageOffer {
    Q_GADGET
    QML_VALUE_TYPE(appImageOffer)
    Q_PROPERTY(QString source MEMBER source FINAL)
    Q_PROPERTY(QString release MEMBER release FINAL)
    Q_PROPERTY(QList<AppImageChoice> files MEMBER files FINAL)
    Q_PROPERTY(int preferred MEMBER preferred FINAL)
    Q_PROPERTY(QString error MEMBER error FINAL)

 public:
    QString source;   ///< "owner/repo"; empty when nothing was looked up
    QString release;  ///< name of the newest release
    QList<AppImageChoice> files;
    int preferred = 0;  ///< the file the link named, else the first
    QString error;      ///< why there is nothing to add; `files` is empty then
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
/// icon, all as the user. There is no catalogue; a newer file that is added replaces the
/// older one of the same app.
///
/// A file can also be taken from the newest release of a GitHub repository. Its menu
/// entry then remembers the repository and the upload, newer releases are looked for and
/// offered as updates. Nothing is ever replaced without the user asking for it.
///
/// The list is not kept in a database of its own: the menu entries written here carry a
/// mark and are the record.
class AppImageClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Backend")
    /// Sorted by name.
    Q_PROPERTY(QList<AppImageItem> apps READ apps NOTIFY appsChanged FINAL)
    /// An AppImage is being downloaded or added.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    /// Name of the file that is being downloaded or added.
    Q_PROPERTY(QString subject READ subject NOTIFY busyChanged FINAL)
    /// The file is still being downloaded.
    Q_PROPERTY(bool downloading READ downloading NOTIFY busyChanged FINAL)
    /// How much of the download is there, in percent.
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged FINAL)
    /// A GitHub link is being looked up.
    Q_PROPERTY(bool lookingUp READ lookingUp NOTIFY offerChanged FINAL)
    /// What the last link that was looked up led to.
    Q_PROPERTY(AppImageOffer offer READ offer NOTIFY offerChanged FINAL)
    /// Newer releases are being looked for.
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged FINAL)
    /// At least one app came from a repository.
    Q_PROPERTY(bool tracking READ tracking NOTIFY appsChanged FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY appsChanged FINAL)
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
    QString subject() const { return m_subject; }
    bool downloading() const { return m_download != nullptr; }
    int progress() const { return m_progress; }
    bool lookingUp() const { return m_lookup != nullptr; }
    AppImageOffer offer() const { return m_offer; }
    bool checking() const { return m_checks > 0; }
    bool tracking() const;
    int updateCount() const;

    /// A short description of a file the user picked. Nothing is run or copied yet.
    Q_INVOKABLE AppImageCandidate inspect(const QUrl& file) const;
    /// Copies the file, reads its menu entry and icon (which runs the file's own extractor)
    /// and adds it to the menu.
    Q_INVOKABLE void add(const QUrl& file);
    /// Asks GitHub for the newest release of the repository `link` names; `offer` has the answer.
    Q_INVOKABLE void lookUp(const QString& link);
    /// Forgets the answer, and the question if it is still on its way.
    Q_INVOKABLE void clearOffer();
    /// Downloads file number `file` of `offer` and adds it like a file the user picked.
    Q_INVOKABLE void addOffered(int file);
    /// Looks for newer releases of the apps that came from a repository. Without `now`,
    /// GitHub is only asked when the last answers are some hours old.
    Q_INVOKABLE void checkUpdates(bool now = true);
    /// Downloads the newer release of an app and replaces its file.
    Q_INVOKABLE void update(const QString& id);
    /// Stops the download that is running.
    Q_INVOKABLE void cancelDownload();
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
    void progressChanged();
    void offerChanged();
    void checkingChanged();
    /// New icons for `image://pkgicon/appimage/<id>`.
    void iconsLoaded(const IconIndex& icons);

 private:
    struct Download;

    void setMessage(const QString& error, const QString& added);
    void setBusy(bool busy, const QString& subject = {});
    void download(const appimage::Source& source, const appimage::Asset& asset, const QString& replaces = {});
    void onDownloaded();

    QFutureWatcher<AppImageResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;  ///< set at shutdown to stop the worker
    QList<AppImageItem> m_apps;
    QString m_error;
    QString m_added;
    QString m_subject;
    QString m_checkError;  ///< why the last look for newer releases failed
    QNetworkAccessManager m_network;
    QNetworkReply* m_lookup = nullptr;
    AppImageOffer m_offer;
    appimage::Source m_offeredSource;
    QList<appimage::Asset> m_offeredAssets;
    std::unique_ptr<Download> m_download;
    /// The newest release of each repository that was asked about, by sourceValue().
    QHash<QString, appimage::Release> m_releases;
    int m_checks         = 0;  ///< questions about newer releases that are on their way
    int m_progress       = 0;
    bool m_busy          = false;
    bool m_fuseAvailable = false;
};
