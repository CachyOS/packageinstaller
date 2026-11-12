// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/// Static review aid for AUR packages.
///
/// Everything here works on text only: a PKGBUILD is never sourced or executed. The
/// findings point a human reviewer at lines worth reading; an empty list does not
/// mean that a package is safe.
namespace pkgscan {

enum class Severity { Info,
    Warning,
    Danger };

struct Finding {
    QString id;
    Severity severity = Severity::Info;
    /// File the finding refers to, empty for package metadata.
    QString file;
    /// 1-based line, 0 when the finding is not tied to a line.
    int line = 0;
    QString title;
    QString detail;
    QString excerpt;
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

[[nodiscard]] Analysis scanPkgbuild(const QString& text);
/// Scans an install scriptlet, shell script, pacman hook or systemd unit shipped with the package.
[[nodiscard]] QList<Finding> scanScript(const QString& text, const QString& file);
[[nodiscard]] QList<Finding> scanMetadata(const Metadata& meta, const QDateTime& now);
/// Returns the name from `known` that `name` could be mistaken for, or an empty string.
[[nodiscard]] QString similarName(const QString& name, const QStringList& known);

/// Most severe first, then by file and line.
void sort(QList<Finding>& findings);
[[nodiscard]] QString severityName(Severity severity);

}  // namespace pkgscan
