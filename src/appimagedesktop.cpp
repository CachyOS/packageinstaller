// SPDX-License-Identifier: GPL-3.0-or-later
#include "appimagedesktop.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace appimage {

namespace {

bool isGroupHeader(const QString& line) {
    return line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'));
}

/// Length of the first argument of an Exec value, quotes included.
qsizetype firstArgumentLength(QStringView exec) {
    bool quoted = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const auto ch = exec.at(i);
        if (ch == QLatin1Char('\\') && quoted) {
            ++i;
        } else if (ch == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (ch == QLatin1Char(' ') && !quoted) {
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
    for (const auto& raw : desktopFile.split(QLatin1Char('\n'))) {
        const auto line = raw.trimmed();  // also drops the \r of DOS line ends
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
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
        const auto equals = line.indexOf(QLatin1Char('='));
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
    return std::ranges::none_of(value, [](QChar ch) { return ch.unicode() < 0x20 && ch != QLatin1Char('\t'); });
}

/// Keys whose value is text for people, with or without a locale: "Name", "Name[de]".
bool isTextKey(const QString& key, const QStringList& bases) {
    static const QRegularExpression localized(QStringLiteral("^([A-Za-z]+)(\\[[A-Za-z0-9_@.-]+\\])?$"));
    const auto match = localized.match(key);
    return match.hasMatch() && bases.contains(match.captured(1));
}

}  // namespace

QHash<QString, QString> mainGroup(const QString& desktopFile) {
    for (const auto& group : parse(desktopFile)) {
        if (group.name == QLatin1String("Desktop Entry")) {
            return group.values;
        }
    }
    return {};
}

QString quoteExec(const QString& path) {
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./+@:-]+$"));
    if (plain.match(path).hasMatch()) {
        return path;
    }
    // Quoted argument: " ` $ \ are escaped with a backslash, and as the value is a desktop
    // file string every backslash is written twice. A literal percent sign is doubled.
    QString quoted = QStringLiteral("\"");
    for (const auto ch : path) {
        if (ch == QLatin1Char('\\')) {
            quoted += QStringLiteral("\\\\\\\\");
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('`') || ch == QLatin1Char('$')) {
            quoted += QStringLiteral("\\\\") + ch;
        } else if (ch == QLatin1Char('%')) {
            quoted += QStringLiteral("%%");
        } else {
            quoted += ch;
        }
    }
    return quoted + QLatin1Char('"');
}

QString replaceProgram(const QString& exec, const QString& program) {
    static const QRegularExpression assignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    QStringView rest = QStringView(exec).trimmed();
    const auto drop = [&rest] { rest = rest.mid(firstArgumentLength(rest)).trimmed(); };
    if (rest.left(firstArgumentLength(rest)) == QLatin1String("env")) {
        drop();
        while (assignment.matchView(rest.left(firstArgumentLength(rest))).hasMatch()) {
            drop();
        }
    }
    drop();  // the program itself
    return rest.isEmpty() ? program : program + QLatin1Char(' ') + rest;
}

QStringList commandLine(const QString& exec) {
    // first the escapes every desktop file string has, then the quoting of Exec
    QString text;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        if (exec.at(i) != QLatin1Char('\\') || i + 1 == exec.size()) {
            text += exec.at(i);
            continue;
        }
        const auto next = exec.at(++i);
        text += next == QLatin1Char('s') ? QLatin1Char(' ') : next == QLatin1Char('n') ? QLatin1Char('\n')
            : next == QLatin1Char('t') ? QLatin1Char('\t') : next == QLatin1Char('r') ? QLatin1Char('\r') : next;
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
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                word += text.at(++i);
            } else if (ch == QLatin1Char('"')) {
                quoted = false;
            } else if (ch == QLatin1Char('%') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('%')) {
                word += text.at(++i);  // a literal percent sign is written twice, in quotes too
            } else {
                word += ch;
            }
        } else if (ch == QLatin1Char('"')) {
            quoted = true;
            inWord = true;
        } else if (ch == QLatin1Char(' ')) {
            finish();
        } else if (ch == QLatin1Char('%') && i + 1 < text.size()) {
            const auto code = text.at(++i);
            inWord          = true;
            if (code == QLatin1Char('%')) {
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
        if (group.name == QLatin1String("Desktop Entry")) {
            main = &group;
        }
    }
    if (main == nullptr || main->values.value(QStringLiteral("Name")).isEmpty() || main->values.value(QStringLiteral("Exec")).isEmpty()
        || main->values.value(QStringLiteral("Type"), QStringLiteral("Application")) != QLatin1String("Application")
        || !printable(appImagePath) || !printable(iconPath)) {
        return {};
    }
    const auto program = quoteExec(appImagePath);

    static const QStringList text{QStringLiteral("Name"), QStringLiteral("GenericName"), QStringLiteral("Comment"), QStringLiteral("Keywords")};
    static const QStringList plain{QStringLiteral("Categories"), QStringLiteral("MimeType"), QStringLiteral("Terminal"),
        QStringLiteral("StartupWMClass"), QStringLiteral("StartupNotify"), QStringLiteral("X-AppImage-Version")};
    static const QRegularExpression actionName(QStringLiteral("^[A-Za-z0-9-]+$"));

    // Only actions that have a group of their own, a name and a command are offered.
    QStringList actions;
    for (const auto& action : main->values.value(QStringLiteral("Actions")).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const auto wanted = QStringLiteral("Desktop Action ") + action;
        const bool usable = actionName.match(action).hasMatch() && !actions.contains(action)
            && std::ranges::any_of(groups, [&wanted](const Group& group) {
                   return group.name == wanted && !group.values.value(QStringLiteral("Name")).isEmpty()
                       && !group.values.value(QStringLiteral("Exec")).isEmpty();
               });
        if (usable) {
            actions << action;
        }
    }

    QStringList out{QStringLiteral("[Desktop Entry]"), QStringLiteral("Type=Application")};
    for (const auto& key : main->order) {
        const auto value = main->values.value(key);
        if (!printable(value)) {
            continue;
        }
        if (key == QLatin1String("Exec")) {
            out << QStringLiteral("Exec=") + replaceProgram(value, program);
        } else if (key == QLatin1String("Icon")) {
            out << QStringLiteral("Icon=") + (iconPath.isEmpty() ? value : iconPath);
        } else if (isTextKey(key, text) || plain.contains(key)) {
            out << key + QLatin1Char('=') + value;
        }
    }
    if (!actions.isEmpty()) {
        out << QStringLiteral("Actions=") + actions.join(QLatin1Char(';')) + QLatin1Char(';');
    }
    out << QLatin1String(PATH_KEY) + QLatin1Char('=') + appImagePath;

    for (const auto& action : std::as_const(actions)) {
        const auto wanted = QStringLiteral("Desktop Action ") + action;
        const auto group  = std::ranges::find_if(groups, [&wanted](const Group& candidate) { return candidate.name == wanted; });
        out << QString() << QLatin1Char('[') + wanted + QLatin1Char(']');
        for (const auto& key : group->order) {
            const auto value = group->values.value(key);
            if (!printable(value)) {
                continue;
            }
            if (key == QLatin1String("Exec")) {
                out << QStringLiteral("Exec=") + replaceProgram(value, program);
            } else if (key == QLatin1String("Icon") || isTextKey(key, {QStringLiteral("Name")})) {
                out << key + QLatin1Char('=') + value;
            }
        }
    }
    const auto entry = out.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return entry.toUtf8().size() > MAX_ENTRY_BYTES ? QString() : entry;
}

QString safeName(const QString& name) {
    QString safe;
    for (const auto ch : name) {
        const bool keep = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('.') || ch == QLatin1Char('_')
            || ch == QLatin1Char('+') || ch == QLatin1Char('-');
        safe += keep ? ch : QLatin1Char('_');
    }
    while (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('-'))) {
        safe.remove(0, 1);
    }
    return safe.left(120);
}

QString safeDesktopName(const QString& name) {
    auto stem = name.endsWith(QLatin1String(".desktop")) ? name.chopped(8) : name;
    stem      = safeName(stem).left(100);
    while (stem.endsWith(QLatin1Char('.'))) {
        stem.chop(1);
    }
    return (stem.isEmpty() ? QStringLiteral("app") : stem) + QStringLiteral(".desktop");
}

bool isManagedPath(const QString& path, const QString& directory) {
    if (directory.isEmpty() || !path.startsWith(directory + QLatin1Char('/'))) {
        return false;
    }
    const auto name = path.mid(directory.size() + 1);
    return !name.isEmpty() && name == safeName(name) && name != QLatin1String("..") && name != QLatin1String(".");
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
