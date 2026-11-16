// SPDX-License-Identifier: GPL-3.0-or-later
#include "gitoutput.hpp"

#include <QStringList>

#include <optional>

using namespace Qt::StringLiterals;

namespace git {

namespace {

/// What a line of a hunk is, by its first character; nothing for context.
std::optional<DiffRun::Kind> hunkLineKind(QStringView line) {
    switch (line.isEmpty() ? u' ' : line.front().unicode()) {
    case u'+': return DiffRun::Kind::Added;
    case u'-': return DiffRun::Kind::Removed;
    case u'@': return DiffRun::Kind::Hunk;
    default: return std::nullopt;
    }
}

}  // namespace

QString DiffRun::kindName() const {
    switch (kind) {
    case Kind::Added: return u"added"_s;
    case Kind::Removed: return u"removed"_s;
    case Kind::Hunk: return u"hunk"_s;
    case Kind::File: break;
    }
    return u"file"_s;
}

QString unquoted(QStringView path) {
    if (path.size() < 2 || !path.startsWith(u'"') || !path.endsWith(u'"')) {
        return path.toString();
    }
    // octal escapes stand for bytes of the UTF-8 name
    QByteArray bytes;
    const auto inner = path.sliced(1, path.size() - 2);
    for (qsizetype at = 0; at < inner.size();) {
        const auto slash = inner.indexOf(u'\\', at);
        bytes += inner.sliced(at, (slash < 0 ? inner.size() : slash) - at).toUtf8();
        if (slash < 0 || slash + 1 == inner.size()) {
            break;
        }
        at              = slash + 1;
        const auto next = inner[at];
        if (bool ok = false; at + 3 <= inner.size() && next >= u'0' && next <= u'3') {
            const auto value = inner.sliced(at, 3).toUInt(&ok, 8);
            if (ok) {
                bytes += static_cast<char>(value);
                at += 3;
                continue;
            }
        }
        constexpr auto ESCAPES = "a\ab\bf\fn\nr\rt\tv\v"_L1;
        const auto known       = ESCAPES.indexOf(next);
        bytes += known >= 0 && known % 2 == 0 ? ESCAPES[known + 1].toLatin1() : next.toLatin1();
        ++at;
    }
    return QString::fromUtf8(bytes);
}

QString diffFile(QStringView paths) {
    // "a/<path> b/<path>": the same path twice, quoted the same way, so the halves are equally long
    if (paths.size() % 2 == 0 || paths[paths.size() / 2] != u' ') {
        return {};
    }
    const auto before = unquoted(paths.first(paths.size() / 2));
    const auto after  = unquoted(paths.sliced(paths.size() / 2 + 1));
    if (!before.startsWith("a/"_L1) || !after.startsWith("b/"_L1) || QStringView(before).sliced(2) != QStringView(after).sliced(2)) {
        return {};
    }
    return after.sliced(2);
}

Diff readableDiff(QStringView diff) {
    QStringList lines;
    QList<DiffRun> runs;
    const auto add = [&](QString line, std::optional<DiffRun::Kind> kind) {
        lines << std::move(line);
        if (!kind) {
            return;
        }
        const auto number = static_cast<int>(lines.size());
        if (!runs.isEmpty() && runs.last().kind == *kind && runs.last().line + runs.last().count == number) {
            ++runs.last().count;
        } else {
            runs << DiffRun{.line = number, .count = 1, .kind = *kind};
        }
    };

    QString file;           // the file whose name has not been written yet
    QString state;          // what happened to it, if more than a change of its lines
    bool inHeader = false;  // between "diff --git" and its first hunk
    const auto flush = [&](const QString& fallback = {}) {
        if (!file.isEmpty()) {
            const auto note = !state.isEmpty() ? state : fallback;
            add(note.isEmpty() ? file : u"%1 (%2)"_s.arg(file, note), DiffRun::Kind::File);
            file.clear();
        }
    };
    for (const auto line : diff.tokenize(u'\n')) {
        if (line.startsWith("diff --git "_L1)) {
            flush(u"mode changed"_s);
            const auto paths = line.sliced(11);
            file             = diffFile(paths);
            if (file.isEmpty()) {
                file = paths.toString();
            }
            state.clear();
            inHeader = true;
        } else if (inHeader && line.startsWith("new file mode"_L1)) {
            state = u"new file"_s;
        } else if (inHeader && line.startsWith("deleted file mode"_L1)) {
            state = u"removed"_s;
        } else if (inHeader && line.startsWith("Binary files"_L1)) {
            flush();
            add(u"\\ binary file, not shown"_s, std::nullopt);
            inHeader = false;
        } else if (inHeader && !line.startsWith("@@"_L1)) {
            continue;  // index, ---, +++ and mode lines
        } else {
            flush();
            inHeader = false;
            add(line.toString(), hunkLineKind(line));
        }
    }
    flush(u"mode changed"_s);
    return {.text = lines.join(u'\n'), .runs = std::move(runs)};
}

QList<GrepMatch> grepByCommit(QStringView output) {
    QList<GrepMatch> matches;
    for (const auto line : output.tokenize(u'\n', Qt::SkipEmptyParts)) {
        // "<commit>:<path>:<line>"
        const auto commitEnd = line.indexOf(u':');
        const auto pathEnd   = commitEnd < 0 ? -1 : line.indexOf(u':', commitEnd + 1);
        if (pathEnd < 0) {
            continue;
        }
        const auto commit = line.first(commitEnd);
        if (matches.isEmpty() || matches.last().commit != commit) {
            matches << GrepMatch{.commit = commit.toString(), .lines = {}};
        }
        matches.last().lines += line.sliced(pathEnd + 1);
        matches.last().lines += u'\n';
    }
    return matches;
}

}  // namespace git
