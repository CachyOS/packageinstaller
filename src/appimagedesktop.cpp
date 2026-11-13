// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

namespace appimage {

namespace {

bool isGroupHeader(const QString& line) {
    return line.startsWith(u'[') && line.endsWith(u']');
}

/// Length of the first argument of an Exec value, quotes included.
qsizetype firstArgumentLength(QStringView exec) {
    bool quoted = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const auto ch = exec.at(i);
        if (ch == u'\\' && quoted) {
            ++i;
        } else if (ch == u'"') {
            quoted = !quoted;
        } else if (ch == u' ' && !quoted) {
            return i;
        }
    }
    return exec.size();
}

/// One group of a desktop file: its keys in the order they appear, first value wins.
struct Group {
    QString name;
    QStringList order;
    QHash<QString, QString> values;
};

QList<Group> parse(const QString& desktopFile) {
    QList<Group> groups;
    QSet<QString> seen;
    bool skip = true;  // until the first header, and inside a repeated group
    for (const auto& raw : desktopFile.split(u'\n')) {
        const auto line = raw.trimmed();  // also drops the \r of DOS line ends
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (isGroupHeader(line)) {
            const auto name = line.mid(1, line.size() - 2);
            skip            = seen.contains(name);
            if (!skip) {
                seen.insert(name);
                groups << Group{name, {}, {}};
            }
            continue;
        }
        const auto equals = line.indexOf(u'=');
        if (skip || equals <= 0) {
            continue;
        }
        const auto key = line.left(equals).trimmed();
        auto& group    = groups.last();
        if (!group.values.contains(key)) {
            group.order << key;
            group.values.insert(key, line.mid(equals + 1).trimmed());
        }
    }
    return groups;
}

/// A value is written back only if it cannot start a new line or end the text early.
bool printable(const QString& value) {
    return std::ranges::none_of(value, [](QChar ch) { return ch.unicode() < 0x20 && ch != u'\t'; });
}

/// Keys whose value is text for people, with or without a locale: "Name", "Name[de]".
bool isTextKey(const QString& key, const QStringList& bases) {
    static const QRegularExpression localized(u"^([A-Za-z]+)(\\[[A-Za-z0-9_@.-]+\\])?$"_s);
    const auto match = localized.match(key);
    return match.hasMatch() && bases.contains(match.captured(1));
}

}  // namespace

QHash<QString, QString> mainGroup(const QString& desktopFile) {
    for (const auto& group : parse(desktopFile)) {
        if (group.name == "Desktop Entry"_L1) {
            return group.values;
        }
    }
    return {};
}

QString quoteExec(const QString& path) {
    static const QRegularExpression plain(u"^[A-Za-z0-9_./+@:-]+$"_s);
    if (plain.match(path).hasMatch()) {
        return path;
    }
    // Quoted argument: " ` $ \ are escaped with a backslash, and as the value is a desktop
    // file string every backslash is written twice. A literal percent sign is doubled.
    QString quoted = u"\""_s;
    for (const auto ch : path) {
        if (ch == u'\\') {
            quoted += u"\\\\\\\\"_s;
        } else if (ch == u'"' || ch == u'`' || ch == u'$') {
            quoted += u"\\\\"_s + ch;
        } else if (ch == u'%') {
            quoted += u"%%"_s;
        } else {
            quoted += ch;
        }
    }
    return quoted + u'"';
}

QString replaceProgram(const QString& exec, const QString& program) {
    static const QRegularExpression assignment(u"^[A-Za-z_][A-Za-z0-9_]*="_s);
    QStringView rest = QStringView(exec).trimmed();
    const auto drop = [&rest] { rest = rest.mid(firstArgumentLength(rest)).trimmed(); };
    if (rest.left(firstArgumentLength(rest)) == "env"_L1) {
        drop();
        while (assignment.matchView(rest.left(firstArgumentLength(rest))).hasMatch()) {
            drop();
        }
    }
    drop();  // the program itself
    return rest.isEmpty() ? program : program + u' ' + rest;
}

QStringList commandLine(const QString& exec) {
    // first the escapes every desktop file string has, then the quoting of Exec
    QString text;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        if (exec.at(i) != u'\\' || i + 1 == exec.size()) {
            text += exec.at(i);
            continue;
        }
        const auto next = exec.at(++i);
        text += next == u's' ? u' ' : next == u'n' ? u'\n'
            : next == u't' ? u'\t' : next == u'r' ? u'\r' : next;
    }

    QStringList words;
    QString word;
    bool inWord = false;
    bool quoted = false;
    bool field  = false;  // the word is a field code such as %U
    const auto finish = [&] {
        if (inWord && !field) {
            words << word;
        }
        word.clear();
        inWord = false;
        field  = false;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto ch = text.at(i);
        if (quoted) {
            if (ch == u'\\' && i + 1 < text.size()) {
                word += text.at(++i);
            } else if (ch == u'"') {
                quoted = false;
            } else if (ch == u'%' && i + 1 < text.size() && text.at(i + 1) == u'%') {
                word += text.at(++i);  // a literal percent sign is written twice, in quotes too
            } else {
                word += ch;
            }
        } else if (ch == u'"') {
            quoted = true;
            inWord = true;
        } else if (ch == u' ') {
            finish();
        } else if (ch == u'%' && i + 1 < text.size()) {
            const auto code = text.at(++i);
            inWord          = true;
            if (code == u'%') {
                word += code;
            } else if (word.isEmpty()) {
                field = true;  // nothing was opened, so there is nothing to pass
            }
        } else {
            word += ch;
            inWord = true;
        }
    }
    finish();
    return words;
}

QString integrate(const QString& desktopFile, const QString& appImagePath, const QString& iconPath) {
    const auto groups = parse(desktopFile);
    const Group* main = nullptr;
    for (const auto& group : groups) {
        if (group.name == "Desktop Entry"_L1) {
            main = &group;
        }
    }
    if (main == nullptr || main->values.value(u"Name"_s).isEmpty() || main->values.value(u"Exec"_s).isEmpty()
        || main->values.value(u"Type"_s, u"Application"_s) != "Application"_L1
        || !printable(appImagePath) || !printable(iconPath)) {
        return {};
    }
    const auto program = quoteExec(appImagePath);

    static const QStringList text{u"Name"_s, u"GenericName"_s, u"Comment"_s, u"Keywords"_s};
    static const QStringList plain{u"Categories"_s, u"MimeType"_s, u"Terminal"_s,
        u"StartupWMClass"_s, u"StartupNotify"_s, u"X-AppImage-Version"_s};
    static const QRegularExpression actionName(u"^[A-Za-z0-9-]+$"_s);

    // Only actions that have a group of their own, a name and a command are offered.
    QStringList actions;
    for (const auto& action : main->values.value(u"Actions"_s).split(u';', Qt::SkipEmptyParts)) {
        const auto wanted = u"Desktop Action "_s + action;
        const bool usable = actionName.match(action).hasMatch() && !actions.contains(action)
            && std::ranges::any_of(groups, [&wanted](const Group& group) {
                   return group.name == wanted && !group.values.value(u"Name"_s).isEmpty()
                       && !group.values.value(u"Exec"_s).isEmpty();
               });
        if (usable) {
            actions << action;
        }
    }

    QStringList out{u"[Desktop Entry]"_s, u"Type=Application"_s};
    for (const auto& key : main->order) {
        const auto value = main->values.value(key);
        if (!printable(value)) {
            continue;
        }
        if (key == "Exec"_L1) {
            out << u"Exec="_s + replaceProgram(value, program);
        } else if (key == "Icon"_L1) {
            out << u"Icon="_s + (iconPath.isEmpty() ? value : iconPath);
        } else if (isTextKey(key, text) || plain.contains(key)) {
            out << key + u'=' + value;
        }
    }
    if (!actions.isEmpty()) {
        out << u"Actions="_s + actions.join(u';') + u';';
    }
    out << PATH_KEY + u'=' + appImagePath;

    for (const auto& action : std::as_const(actions)) {
        const auto wanted = u"Desktop Action "_s + action;
        const auto group  = std::ranges::find_if(groups, [&wanted](const Group& candidate) { return candidate.name == wanted; });
        out << QString() << u'[' + wanted + u']';
        for (const auto& key : group->order) {
            const auto value = group->values.value(key);
            if (!printable(value)) {
                continue;
            }
            if (key == "Exec"_L1) {
                out << u"Exec="_s + replaceProgram(value, program);
            } else if (key == "Icon"_L1 || isTextKey(key, {u"Name"_s})) {
                out << key + u'=' + value;
            }
        }
    }
    const auto entry = out.join(u'\n') + u'\n';
    return entry.toUtf8().size() > MAX_ENTRY_BYTES ? QString() : entry;
}

QString safeName(const QString& name) {
    QString safe;
    for (const auto ch : name) {
        const bool keep = (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z')
            || (ch >= u'0' && ch <= u'9') || ch == u'.' || ch == u'_'
            || ch == u'+' || ch == u'-';
        safe += keep ? ch : u'_';
    }
    while (safe.startsWith(u'.') || safe.startsWith(u'-')) {
        safe.remove(0, 1);
    }
    return safe.left(120);
}

QString safeDesktopName(const QString& name) {
    auto stem = name.endsWith(".desktop"_L1) ? name.chopped(8) : name;
    stem      = safeName(stem).left(100);
    while (stem.endsWith(u'.')) {
        stem.chop(1);
    }
    return (stem.isEmpty() ? u"app"_s : stem) + u".desktop"_s;
}

bool isManagedPath(const QString& path, const QString& directory) {
    if (directory.isEmpty() || !path.startsWith(directory + u'/')) {
        return false;
    }
    const auto name = path.mid(directory.size() + 1);
    return !name.isEmpty() && name == safeName(name) && name != ".."_L1 && name != "."_L1;
}

bool isAppImage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const auto header = file.read(11);
    // ELF magic, then "AI" and the type (2) in the padding bytes of the ELF identification
    return header.size() == 11 && header.startsWith("\x7f"
                                                    "ELF")
        && header.mid(8, 3) == QByteArray("AI\x02", 3);
}

}  // namespace appimage
