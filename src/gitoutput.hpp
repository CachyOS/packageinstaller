// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QObject>
#include <QString>

/// Reading what git prints. Text handling only, shared by the GUI and the tests.
namespace git {

/// The empty tree. Given as `--attr-source`, it keeps the .gitattributes of a repository
/// from changing what git prints, for example hiding a file's changes as binary.
inline constexpr auto EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";

/// Lines of one kind that follow each other in a readable diff.
struct DiffRun {
    Q_GADGET
    Q_PROPERTY(int line MEMBER line FINAL)
    Q_PROPERTY(int count MEMBER count FINAL)
    Q_PROPERTY(QString kind READ kindName FINAL)

 public:
    enum class Kind { Added,
        Removed,
        Hunk,
        File };

    int line  = 0;  ///< 1-based
    int count = 0;
    Kind kind = Kind::File;

    /// "added", "removed", "hunk" or "file"
    [[nodiscard]] QString kindName() const;
    bool operator==(const DiffRun&) const = default;
};

struct Diff {
    QString text;
    /// Every line that is not context; the kinds are known here, not guessed from the text.
    QList<DiffRun> runs;
};

/// Makes `git diff --no-renames` output readable: the lines git needs to apply it give way
/// to one line per file that names it.
[[nodiscard]] Diff readableDiff(QStringView diff);

/// A path as git prints it: as is, or in double quotes with C escapes.
[[nodiscard]] QString unquoted(QStringView path);

/// The file of a "diff --git a/<path> b/<path>" line, given what follows "diff --git ".
/// Empty when the two paths differ, which they do not without renames.
[[nodiscard]] QString diffFile(QStringView paths);

struct GrepMatch {
    QString commit;
    QString lines;  ///< the matching lines of that commit, each ending in '\n'
};

/// The output of `git grep` run over several commits, grouped by commit in the order printed.
[[nodiscard]] QList<GrepMatch> grepByCommit(QStringView output);

}  // namespace git
