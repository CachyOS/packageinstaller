// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class QDir;
class QJsonObject;

/// Static review aid for AUR packages.
///
/// Everything here works on text only: a PKGBUILD is never sourced or executed. The
/// findings point a human reviewer at lines worth reading; an empty list does not
/// mean that a package is safe.
namespace pkgscan {

/// Scripts next to the PKGBUILD that are read for review.
constexpr qsizetype MAX_FILES  = 8;
constexpr qint64 MAX_FILE_SIZE = 512 * 1024;

enum class Severity { Info,
    Warning,
    Danger };

struct Finding {
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id FINAL)
    Q_PROPERTY(QString severity READ severityText FINAL)
    Q_PROPERTY(QString file MEMBER file FINAL)
    Q_PROPERTY(int line MEMBER line FINAL)
    Q_PROPERTY(QString title MEMBER title FINAL)
    Q_PROPERTY(QString detail MEMBER detail FINAL)
    Q_PROPERTY(QString excerpt MEMBER excerpt FINAL)

 public:
    QString id;
    Severity severity = Severity::Info;
    /// File the finding refers to, empty for package metadata.
    QString file;
    /// 1-based line, 0 when the finding is not tied to a line.
    int line = 0;
    QString title;
    QString detail;
    QString excerpt;

    QString severityText() const;
};

struct Analysis {
    QList<Finding> findings;
    /// Hosts the sources are downloaded from.
    QStringList hosts;
    /// Files next to the PKGBUILD that run on the machine (install scriptlet, scripts, hooks, units).
    QStringList localFiles;
    /// Other local sources. One that starts with "#!" is a script as well, which only its content can tell.
    QStringList otherLocalFiles;
};

struct Metadata {
    QString name;
    QString maintainer;
    QString submitter;
    int votes         = 0;
    double popularity = 0;
    qint64 firstSubmitted = 0;
    qint64 lastModified   = 0;
};

struct ScriptFile {
    Q_GADGET
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)

 public:
    QString name;
    QString text;
};

struct LocalFiles {
    QList<ScriptFile> files;
    /// Script files that are missing, too large, symbolic links or beyond the file limit.
    QStringList unreadable;
};

/// With a `.SRCINFO`, sources, checksums, keys and install scriptlets are taken from it, and
/// PKGBUILD sources it does not list are flagged. Without one they are read from the PKGBUILD text.
[[nodiscard]] Analysis scanPkgbuild(const QString& pkgbuild, const QString& srcinfo = {});
/// Scans an install scriptlet, shell script, pacman hook or systemd unit shipped with the package.
[[nodiscard]] QList<Finding> scanScript(const QString& text, const QString& file);
[[nodiscard]] QList<Finding> scanMetadata(const Metadata& meta, const QDateTime& now);
/// Returns the name from `known` that `name` could be mistaken for, or an empty string.
[[nodiscard]] QString similarName(const QString& name, const QStringList& known);

/// Adds the other local files in `dir` that start with "#!" to `analysis.localFiles`, then
/// reads the first `maxFiles` of those.
[[nodiscard]] LocalFiles collectFiles(const QDir& dir, Analysis& analysis, qsizetype maxFiles = MAX_FILES,
    qint64 maxSize = MAX_FILE_SIZE);
/// Text of a regular file of at most `maxSize` bytes. Links are refused, they could point anywhere.
[[nodiscard]] std::optional<QString> readText(const QString& path, qint64 maxSize = MAX_FILE_SIZE);
/// Whether `name` is usable as an AUR package base, and so as a directory name and URL part.
[[nodiscard]] bool validPkgbase(QStringView name);

/// The version of the package base in a .SRCINFO as pacman shows it once built,
/// "[epoch:]pkgver-pkgrel". Empty when the file names none.
[[nodiscard]] QString srcinfoVersion(QStringView srcinfo);

/// The git repository a package that builds the newest source code follows, and its branch.
struct GitSource {
    QString url;
    QString ref;  ///< "HEAD" for the default branch, else "refs/heads/<branch>"

    bool operator==(const GitSource&) const = default;
};

/// The first version control source of the package base in a .SRCINFO, when it is a git
/// repository on an https address that follows a branch. None when it is pinned to a commit
/// or a tag, uses another version control system or another protocol, or there is none.
[[nodiscard]] std::optional<GitSource> trackedGitSource(QStringView srcinfo, QStringView arch = u"x86_64");

/// The parts of a version of a package that builds the newest source code that look like an
/// abbreviated commit id, like "d8dd24274" in "0.1.0.r352.gd8dd24274-1" or "c89f677" in
/// "1:0.1.0.r1613.c89f677-1". Usually the last part, but mesa-git ends in a checksum of its patches.
[[nodiscard]] QStringList versionCommits(QStringView version);

/// The commit of `ref` in what `git ls-remote` printed, or an empty string.
[[nodiscard]] QString remoteCommit(QStringView lsRemote, QStringView ref);

/// Whether a build of a package that builds the newest source code is behind its source.
struct SourceState {
    bool behind = false;
    QStringList record;  ///< to keep for the next check; empty when there is nothing to keep
};

/// Compares the installed build `version`, installed at `installDate`, with the commit `remote`
/// its source is at now. `record` is what the previous check kept.
/// - A build whose version names a commit is behind until that commit is `remote`.
/// - A build whose version names none counts from the commit its source was at when it was
///   first checked, like `paru --gendb`, and is behind once the source moved on.
/// - So does a build made after its version was found behind that still names another commit:
///   its version follows another source than the first one, and would never match.
[[nodiscard]] SourceState followSource(const QStringList& record, const QString& version, qint64 installDate, const QString& remote);

/// Most severe first, then by file and line.
void sort(QList<Finding>& findings);
[[nodiscard]] QString severityName(Severity severity);
[[nodiscard]] QJsonObject toJson(const Finding& finding);

inline QString Finding::severityText() const { return severityName(severity); }

}  // namespace pkgscan
