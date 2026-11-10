// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkgscan.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <array>
#include <vector>

namespace pkgscan {

namespace {

constexpr auto PKGBUILD   = "PKGBUILD";
constexpr int MAX_PER_RULE = 5;

enum Scope : unsigned { InPkgbuild = 1U,
    InScript                       = 2U,
    Everywhere                     = InPkgbuild | InScript };

QString tr(const char* text) {
    return QCoreApplication::translate("pkgscan", text);
}

/// Something at the position of a command, as opposed to an argument or a string.
#define CMD R"((?:^|[;&|({`]|\b(?:then|do|else|elif|if|while|exec|sudo|xargs|env|time|nice)\s|\bExec\s*=)\s*)"

struct Rule {
    const char* id;
    Severity severity;
    unsigned scope;
    /// Only the first matching rule of a group is reported for a line.
    const char* group;
    const char* pattern;
    const char* title;
    const char* detail;
};

// Ordered so that the most specific rule of a group comes first.
const std::array RULES = {
    Rule{"pipe-to-shell", Severity::Danger, Everywhere, "net",
        R"(\b(?:curl|wget|fetch|aria2c)\b[^|#]*\|\s*(?:sudo\s+)?(?:env\s+)?(?:(?:ba|z|da|k|fi)?sh|python[0-9.]*|perl|ruby|node)\b)"
        R"(|\b(?:ba|z|da)?sh\s+<\(\s*(?:curl|wget)\b|\b(?:ba|z|da)?sh\s+-c\s+["']?\$\(\s*(?:curl|wget)\b)"
        R"(|\b(?:source|\.)\s+<\(\s*(?:curl|wget)\b|\beval\b[^#]*\$\(\s*(?:curl|wget)\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Downloads a script and runs it directly"),
        QT_TRANSLATE_NOOP("pkgscan", "The downloaded code is executed without being stored or verified, so it can change at any time.")},
    Rule{"download", Severity::Warning, Everywhere, "net",
        CMD R"((?:curl|wget|aria2c)\s|)" CMD R"(git\s+(?:-\S+\s+)*clone\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Downloads something outside the source list"),
        QT_TRANSLATE_NOOP("pkgscan", "Only files listed in source=() are checked against a checksum. This download is not.")},
    Rule{"decode-exec", Severity::Danger, Everywhere, "decode",
        R"(\bbase64\s+(?:-d|-D|--decode)\b[^#]*\|\s*(?:sudo\s+)?(?:ba|z|da)?sh\b|\beval\b[^#]*\bbase64\b|\bxxd\s+-r\b[^#]*\|\s*(?:ba|z|da)?sh\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Decodes hidden text and runs it"),
        QT_TRANSLATE_NOOP("pkgscan", "Encoding a command before running it has no purpose other than hiding what it does.")},
    Rule{"decode", Severity::Warning, Everywhere, "decode",
        R"(\bbase64\s+(?:-d|-D|--decode)\b|\bopenssl\s+enc\s+-d\b|\bxxd\s+-r\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Decodes encoded data"),
        QT_TRANSLATE_NOOP("pkgscan", "Check what the decoded data is and what happens with it.")},
    Rule{"reverse-shell", Severity::Danger, Everywhere, "",
        R"(/dev/(?:tcp|udp)/|\b(?:nc|ncat|netcat)\b[^#]*\s-[a-zA-Z]*e\b|\bsocat\b[^#]*\bexec:|\bbash\s+-i\s*>&)",
        QT_TRANSLATE_NOOP("pkgscan", "Opens a network connection from the shell"),
        QT_TRANSLATE_NOOP("pkgscan", "This is how remote-control backdoors are started. A build or install script has no reason to do this.")},
    Rule{"credentials", Severity::Danger, Everywhere, "",
        R"(\.ssh/|\bid_(?:rsa|ed25519|ecdsa)\b|\.gnupg\b|/etc/shadow\b|\.aws/credentials|\.mozilla/|cookies\.sqlite|logins\.json|Login Data)"
        R"(|\.config/(?:google-chrome|chromium|BraveSoftware|discord)|wallet\.dat|\.kube/config|\.netrc\b|\.git-credentials|\.local/share/keyrings)",
        QT_TRANSLATE_NOOP("pkgscan", "Accesses keys, passwords or browser data"),
        QT_TRANSLATE_NOOP("pkgscan", "These files hold your logins. Building or installing a package never needs them.")},
    Rule{"root-access", Severity::Danger, Everywhere, "",
        R"(/etc/sudoers|\bvisudo\b|/etc/polkit-1/rules\.d|/etc/ld\.so\.preload|/etc/pam\.d)",
        QT_TRANSLATE_NOOP("pkgscan", "Changes who may become administrator"),
        QT_TRANSLATE_NOOP("pkgscan", "Editing sudo, polkit, PAM or the library preload list can give lasting, silent root access.")},
    Rule{"destructive", Severity::Danger, Everywhere, "",
        CMD R"(rm\s+(?:-\S+\s+)*["']?(?:/|~|\$HOME|\$\{HOME\})["']?/?\*?(?:\s|$))",
        QT_TRANSLATE_NOOP("pkgscan", "Deletes a whole directory tree"),
        QT_TRANSLATE_NOOP("pkgscan", "This removes the root or home directory.")},
    Rule{"obfuscated-escapes", Severity::Warning, Everywhere, "",
        R"((?:\\x[0-9a-fA-F]{2}){8,}|(?:\\[0-7]{3}){8,})",
        QT_TRANSLATE_NOOP("pkgscan", "Contains text hidden as escape codes"),
        QT_TRANSLATE_NOOP("pkgscan", "Readable scripts do not need to spell out text byte by byte.")},
    Rule{"eval", Severity::Warning, Everywhere, "",
        CMD R"(eval\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Runs text that is put together at run time"),
        QT_TRANSLATE_NOOP("pkgscan", "With eval the command that really runs is not visible in the script. Check where its input comes from.")},
    Rule{"privilege", Severity::Warning, InPkgbuild, "",
        CMD R"((?:sudo|doas|pkexec)\s|)" CMD R"(su\s+(?:-\S*\s+)*\S)",
        QT_TRANSLATE_NOOP("pkgscan", "Asks for administrator rights while building"),
        QT_TRANSLATE_NOOP("pkgscan", "A build runs as your user and must not need root.")},
    Rule{"home", Severity::Warning, Everywhere, "",
        R"((?<![A-Za-z0-9_])(?:\$HOME\b|\$\{HOME\}|~/)|(?<![A-Za-z0-9_.-])/home/|/root/)",
        QT_TRANSLATE_NOOP("pkgscan", "Touches a home directory"),
        QT_TRANSLATE_NOOP("pkgscan", "Packages install into system directories. Check what is read from or written to the home directory.")},
    Rule{"autostart", Severity::Warning, Everywhere, "",
        R"(\bcrontab\b|/etc/cron|\.config/autostart|/etc/xdg/autostart|\.(?:bashrc|zshrc|bash_profile|zprofile|profile)\b|/etc/profile\.d|\bLD_PRELOAD=)",
        QT_TRANSLATE_NOOP("pkgscan", "Sets something up to run automatically"),
        QT_TRANSLATE_NOOP("pkgscan", "Cron jobs, autostart entries and shell start-up files keep running after the installation.")},
    Rule{"service", Severity::Warning, Everywhere, "",
        CMD R"(systemctl\s+(?:-\S+\s+)*(?:enable|start|restart|mask|unmask)\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Enables or starts a service"),
        QT_TRANSLATE_NOOP("pkgscan", "Packages should leave it to you to enable their services.")},
    Rule{"setuid", Severity::Warning, Everywhere, "",
        CMD R"(chmod\s+(?:-\S+\s+)*(?:[4267][0-7]{3}\b|[ugoa]*\+[rwxX]*s))" "|" CMD R"(setcap\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Gives a program elevated rights"),
        QT_TRANSLATE_NOOP("pkgscan", "A setuid bit or capability lets the program act with more rights than the user who starts it.")},
    Rule{"tls-off", Severity::Warning, Everywhere, "",
        R"(--no-check-certificate|\bcurl\b[^#|;]*\s(?:-[a-zA-Z]*k[a-zA-Z]*|--insecure)(?:\s|$)|GIT_SSL_NO_VERIFY|sslVerify\s*=?\s*false|NODE_TLS_REJECT_UNAUTHORIZED)",
        QT_TRANSLATE_NOOP("pkgscan", "Turns off the check of the server's identity"),
        QT_TRANSLATE_NOOP("pkgscan", "Without certificate checks anybody on the network can replace the download.")},
    Rule{"system-state", Severity::Warning, InPkgbuild, "",
        CMD R"((?:modprobe|insmod|iptables|nft|chattr|mount|sysctl|useradd|usermod|passwd|groupadd|pacman|systemd-run)\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Changes the system while building"),
        QT_TRANSLATE_NOOP("pkgscan", "A build only creates files inside its own directory.")},
    Rule{"inline-script", Severity::Info, Everywhere, "",
        CMD R"((?:python[0-9.]*|perl|ruby|node)\s+(?:-\S+\s+)*-[ce]\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Runs an inline script in another language"),
        QT_TRANSLATE_NOOP("pkgscan", "Read the embedded code as carefully as the rest.")},
};

struct Compiled {
    const Rule* rule;
    QRegularExpression regex;
};

const std::vector<Compiled>& compiledRules() {
    static const auto rules = [] {
        std::vector<Compiled> out;
        out.reserve(RULES.size());
        for (const auto& rule : RULES) {
            out.push_back({&rule, QRegularExpression(QString::fromLatin1(rule.pattern))});
        }
        return out;
    }();
    return rules;
}

QString excerptOf(const QString& raw) {
    auto text = raw.trimmed();
    if (text.size() > 160) {
        text = text.left(157) + QStringLiteral("…");
    }
    return text;
}

Finding makeFinding(const char* id, Severity severity, const QString& file, int line, const QString& title, const QString& detail, const QString& excerpt = {}) {
    return {QString::fromLatin1(id), severity, file, line, title, detail, excerptOf(excerpt)};
}

/// Cuts an unquoted trailing comment. Quotes are tracked per line only.
QString stripComment(const QString& line) {
    bool single = false;
    bool dbl    = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const auto c = line.at(i);
        if (c == u'\\' && !single) {
            ++i;
        } else if (c == u'\'' && !dbl) {
            single = !single;
        } else if (c == u'"' && !single) {
            dbl = !dbl;
        } else if (c == u'#' && !single && !dbl && (i == 0 || line.at(i - 1).isSpace())) {
            return line.left(i);
        }
    }
    return line;
}

bool isHex(QStringView text) {
    return std::ranges::all_of(text, [](QChar c) { return c.isDigit() || (c.toLower() >= u'a' && c.toLower() <= u'f'); });
}

/// A long run of base64 characters that is not a hexadecimal checksum.
bool hasEncodedBlob(const QString& code) {
    static const QRegularExpression blob(QStringLiteral("[A-Za-z0-9+/]{160,}={0,2}"));
    auto it = blob.globalMatch(code);
    while (it.hasNext()) {
        const auto match = it.next();
        const auto text  = match.capturedView();
        // paths and URLs are long runs of the same alphabet, real base64 rarely has many slashes
        if (!isHex(text) && text.count(u'/') < text.size() / 12) {
            return true;
        }
    }
    return false;
}

/// Applies the line rules. `sanitized` is the line with harmless parts (package directory paths) masked.
void applyRules(const QString& sanitized, const QString& raw, const QString& file, int line, unsigned scope, QHash<QString, int>& hits, QList<Finding>& out) {
    QStringList groups;
    for (const auto& [rule, regex] : compiledRules()) {
        if ((rule->scope & scope) == 0U) {
            continue;
        }
        const auto group = QString::fromLatin1(rule->group);
        if (!group.isEmpty() && groups.contains(group)) {
            continue;
        }
        if (!regex.match(sanitized).hasMatch()) {
            continue;
        }
        if (!group.isEmpty()) {
            groups << group;
        }
        if (++hits[QString::fromLatin1(rule->id)] > MAX_PER_RULE) {
            continue;
        }
        out << makeFinding(rule->id, rule->severity, file, line, tr(rule->title), tr(rule->detail), raw);
    }
    if (hasEncodedBlob(sanitized) && ++hits[QStringLiteral("encoded-blob")] <= MAX_PER_RULE) {
        out << makeFinding("encoded-blob", Severity::Warning, file, line, tr("Contains a long block of encoded data"),
            tr("Encoded blocks can hide a program or script. Check what it is decoded into."), raw);
    }
}

// ---------------------------------------------------------------------------------------
// PKGBUILD structure

struct Entry {
    QString value;
    int line = 0;
};

struct Array {
    QList<Entry> entries;
    int firstLine = 0;
    int lastLine  = 0;
};

struct Parsed {
    QHash<QString, Array> arrays;
    QHash<QString, QString> scalars;
    /// Lines that only hold array data (dependency names, checksums, ...).
    QList<bool> dataLine;
};

Parsed parse(const QString& text, int lineCount) {
    Parsed parsed;
    parsed.dataLine.fill(false, lineCount + 2);

    static const QRegularExpression arrayStart(QStringLiteral("^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)\\+?=\\("), QRegularExpression::MultilineOption);
    auto arrays = arrayStart.globalMatch(text);
    while (arrays.hasNext()) {
        const auto match = arrays.next();
        auto& array      = parsed.arrays[match.captured(1)];
        int line         = static_cast<int>(text.left(match.capturedStart()).count(u'\n')) + 1;
        const int first  = line;
        if (array.firstLine == 0) {
            array.firstLine = line;
        }

        QString token;
        bool inToken   = false;
        bool hasCode   = false;
        int tokenLine  = line;
        QChar quote;
        const auto flush = [&] {
            if (inToken) {
                array.entries << Entry{token, tokenLine};
            }
            token.clear();
            inToken = false;
        };
        qsizetype i = match.capturedEnd();
        for (; i < text.size(); ++i) {
            const auto c = text.at(i);
            if (c == u'\n') {
                ++line;
            }
            if (!quote.isNull()) {
                if (c == quote) {
                    quote = QChar();
                } else {
                    token += c;
                }
                continue;
            }
            if (c == u'\'' || c == u'"') {
                quote = c;
                if (!inToken) {
                    inToken   = true;
                    tokenLine = line;
                }
            } else if (c == u')') {
                break;
            } else if (c.isSpace()) {
                flush();
            } else if (c == u'#' && !inToken) {
                while (i + 1 < text.size() && text.at(i + 1) != u'\n') {
                    ++i;
                }
            } else {
                if (c == u'`' || (c == u'$' && i + 1 < text.size() && text.at(i + 1) == u'(')) {
                    hasCode = true;
                }
                if (!inToken) {
                    inToken   = true;
                    tokenLine = line;
                }
                token += c;
            }
        }
        flush();
        array.lastLine = line;
        // arrays that run commands are still looked at by the line rules
        if (!hasCode) {
            for (int n = first; n <= line && n < parsed.dataLine.size(); ++n) {
                parsed.dataLine[n] = true;
            }
        }
    }

    static const QRegularExpression scalar(QStringLiteral("^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)=(?!\\()(.*)$"), QRegularExpression::MultilineOption);
    auto scalars = scalar.globalMatch(text);
    while (scalars.hasNext()) {
        const auto match = scalars.next();
        auto value       = stripComment(match.captured(2)).trimmed();
        if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\'')) && value.endsWith(value.at(0))) {
            value = value.mid(1, value.size() - 2);
        }
        parsed.scalars.insert(match.captured(1), value);
    }
    return parsed;
}

/// Replaces `$name` and `${name}` with known top-level values. Anything else is left alone.
QString expand(QString value, const Parsed& parsed) {
    static const QRegularExpression variable(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}|\\$([A-Za-z_][A-Za-z0-9_]*)"));
    // ${NAME:-fallback}: an environment override, the fallback is what a normal build uses
    static const QRegularExpression fallback(QStringLiteral("\\$\\{[A-Za-z_][A-Za-z0-9_]*:?[-=]([^{}]*)\\}"));
    value.replace(fallback, QStringLiteral("\\1"));
    for (int pass = 0; pass < 4; ++pass) {
        QString out;
        qsizetype last = 0;
        bool changed   = false;
        auto it        = variable.globalMatch(value);
        while (it.hasNext()) {
            const auto match = it.next();
            const auto name  = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
            out += QStringView(value).mid(last, match.capturedStart() - last);
            last = match.capturedEnd();
            if (parsed.scalars.contains(name)) {
                out += parsed.scalars.value(name);
                changed = true;
            } else if (parsed.arrays.contains(name) && !parsed.arrays.value(name).entries.isEmpty()) {
                out += parsed.arrays.value(name).entries.first().value;
                changed = true;
            } else {
                out += match.capturedView();
            }
        }
        out += QStringView(value).mid(last);
        value = out;
        value.replace(fallback, QStringLiteral("\\1"));
        if (!changed) {
            break;
        }
    }
    return value;
}

bool hostIs(const QString& host, QLatin1StringView domain) {
    return host == domain || host.endsWith(u'.' + QString(domain));
}

/// Places anyone can upload to anonymously, or that hide the real address.
bool isThrowawayHost(const QString& host) {
    static constexpr std::array HOSTS = {
        "bit.ly", "tinyurl.com", "t.co", "is.gd", "goo.gl", "cutt.ly", "rb.gy", "shorturl.at",
        "pastebin.com", "paste.rs", "paste.ee", "dpaste.com", "dpaste.org", "hastebin.com", "termbin.com", "ix.io", "rentry.co",
        "0x0.st", "transfer.sh", "temp.sh", "file.io", "catbox.moe", "litterbox.catbox.moe", "anonfiles.com", "gofile.io",
        "cdn.discordapp.com", "media.discordapp.net", "ngrok.io", "ngrok-free.app", "trycloudflare.com", "serveo.net",
        "gist.githubusercontent.com", "mega.nz", "mediafire.com", "dropbox.com", "dropboxusercontent.com", "drive.google.com",
    };
    return host.endsWith(QLatin1String(".onion"))
        || std::ranges::any_of(HOSTS, [&](const char* domain) { return hostIs(host, QLatin1StringView(domain)); });
}

bool isScriptFile(const QString& name) {
    static constexpr std::array SUFFIXES = {".install", ".sh", ".bash", ".hook", ".service", ".timer", ".path", ".socket", ".py", ".desktop", ".rules"};
    return std::ranges::any_of(SUFFIXES, [&](const char* suffix) { return name.endsWith(QLatin1String(suffix)); });
}

/// A file name that can only refer to a file next to the PKGBUILD.
bool isPlainFileName(const QString& name) {
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9@_+][A-Za-z0-9@._+-]*$"));
    return name.size() <= 128 && plain.match(name).hasMatch();
}

bool isVcsPackage(const QString& name) {
    static constexpr std::array SUFFIXES = {"-git", "-svn", "-hg", "-bzr", "-cvs", "-fossil", "-darcs", "-nightly"};
    return std::ranges::any_of(SUFFIXES, [&](const char* suffix) { return name.endsWith(QLatin1String(suffix)); });
}

void scanSources(const Parsed& parsed, Analysis& analysis) {
    static constexpr std::array STRONG = {"sha256sums", "sha512sums", "b2sums", "sha384sums", "sha224sums"};
    static constexpr std::array WEAK   = {"md5sums", "sha1sums", "cksums"};
    static const QRegularExpression vcsPrefix(QStringLiteral("^(git|svn|hg|bzr|fossil)\\+"));
    static const QRegularExpression ipHost(QStringLiteral("^(\\d{1,3}\\.){3}\\d{1,3}$"));
    const auto file    = QString::fromLatin1(PKGBUILD);
    const auto pkgname = expand(QStringLiteral("$pkgname"), parsed);
    int unverified     = 0;
    int dynamic        = 0;

    auto names = parsed.arrays.keys();
    names.sort();
    for (const auto& name : std::as_const(names)) {
        if (name != QLatin1String("source") && !name.startsWith(QLatin1String("source_"))) {
            continue;
        }
        const auto suffix   = name.mid(6);
        const auto& sources = parsed.arrays[name].entries;
        const auto checksum = [&](const char* algorithm, qsizetype index) {
            const auto sums = parsed.arrays.value(QLatin1String(algorithm) + suffix).entries;
            return index < sums.size() && sums.at(index).value != QLatin1String("SKIP");
        };

        for (qsizetype i = 0; i < sources.size(); ++i) {
            const auto& entry = sources.at(i);
            auto spec         = expand(entry.value, parsed);
            const auto rename = spec.indexOf(QLatin1String("::"));
            const auto scheme = spec.indexOf(QLatin1String("://"));
            if (rename > 0 && (scheme < 0 || rename < scheme)) {
                spec = spec.mid(rename + 2);
            }
            const auto prefix = vcsPrefix.match(spec);
            const bool vcs    = prefix.hasMatch() || spec.startsWith(QLatin1String("git://"));
            const auto address = prefix.hasMatch() ? spec.mid(prefix.capturedLength()) : spec;
            const bool remote = address.contains(QLatin1String("://"));
            const bool strong = std::ranges::any_of(STRONG, [&](const char* a) { return checksum(a, i); });
            const bool weak   = std::ranges::any_of(WEAK, [&](const char* a) { return checksum(a, i); });

            if (!remote) {
                if (isPlainFileName(address) && isScriptFile(address) && !analysis.localFiles.contains(address)) {
                    analysis.localFiles << address;
                }
                continue;
            }

            const QUrl url(address);
            const auto host = url.host().toLower();
            if (host.isEmpty() || host.contains(u'$')) {
                if (++dynamic <= MAX_PER_RULE) {
                    analysis.findings << makeFinding("dynamic-source", Severity::Info, file, entry.line, tr("Download address is put together from variables"),
                        tr("Work out where this source really comes from."), entry.value);
                }
            } else {
                if (!analysis.hosts.contains(host)) {
                    analysis.hosts << host;
                }
                if (ipHost.match(host).hasMatch() || host.contains(u':')) {
                    analysis.findings << makeFinding("ip-host", Severity::Warning, file, entry.line, tr("Downloads from a bare IP address"),
                        tr("Real projects publish their files under a name. %1 cannot be traced to anybody.").arg(host), entry.value);
                } else if (isThrowawayHost(host)) {
                    analysis.findings << makeFinding("throwaway-host", Severity::Warning, file, entry.line, tr("Downloads from a file-sharing, paste or link-shortening service"),
                        tr("Anybody can upload to %1 anonymously and replace the file later.").arg(host), entry.value);
                }
            }

            if (vcs) {
                static const QRegularExpression pinned(QStringLiteral("#(commit|tag|revision)="));
                if (!pinned.match(address).hasMatch() && !isVcsPackage(pkgname)) {
                    analysis.findings << makeFinding("vcs-unpinned", Severity::Info, file, entry.line, tr("Builds whatever is in the repository at that moment"),
                        tr("The source is not pinned to a commit or tag, so the code can differ from what the maintainer tested."), entry.value);
                }
                continue;
            }

            // detached signatures are checked against validpgpkeys instead of a checksum
            static const QRegularExpression signature(QStringLiteral("\\.(sig|asc|sign|minisig|gpg)([?#].*)?$"));
            if (signature.match(address).hasMatch() || signature.match(expand(entry.value, parsed).section(QLatin1String("::"), 0, 0)).hasMatch()) {
                continue;
            }

            const bool plaintext = url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("ftp");
            if (!strong && !weak) {
                if (++unverified <= MAX_PER_RULE) {
                    analysis.findings << makeFinding("no-checksum", Severity::Warning, file, entry.line, tr("Download is not verified"),
                        plaintext ? tr("This file has no checksum and is fetched without encryption, so anybody on the network can replace it.")
                                  : tr("This file has no checksum (SKIP), so whatever the server sends is accepted."),
                        entry.value);
                }
            } else if (!strong) {
                analysis.findings << makeFinding("weak-checksum", Severity::Info, file, entry.line, tr("Download is only verified with an outdated checksum"),
                    tr("MD5 and SHA-1 can be forged. Current packages use SHA-256 or BLAKE2."), entry.value);
            }
        }
    }

    auto install = expand(parsed.scalars.value(QStringLiteral("install")), parsed);
    if (!install.isEmpty() && isPlainFileName(install)) {
        analysis.localFiles.removeAll(install);
        analysis.localFiles.prepend(install);
    }
}

struct LogicalLine {
    int line = 0;
    /// First physical line, for display.
    QString raw;
    /// Comment-free code with backslash continuations joined.
    QString code;
};

QList<LogicalLine> logicalLines(const QString& text) {
    QList<LogicalLine> out;
    const auto lines = text.split(u'\n');
    for (int index = 0; index < lines.size(); ++index) {
        LogicalLine logical{index + 1, lines.at(index), stripComment(lines.at(index))};
        while (logical.code.endsWith(u'\\') && index + 1 < lines.size()) {
            logical.code.chop(1);
            logical.code += u' ' + stripComment(lines.at(++index)).trimmed();
        }
        if (!logical.code.trimmed().isEmpty()) {
            out << logical;
        }
    }
    return out;
}

/// Masks paths below the package and source directories, which a PKGBUILD is meant to write to.
QString maskBuildPaths(const QString& code) {
    static const QRegularExpression buildPath(QStringLiteral("[\"']?\\$\\{?(pkgdir|srcdir|startdir|_?builddir)\\}?[^\\s;|&)]*"));
    auto out = code;
    out.replace(buildPath, QStringLiteral(" PKGPATH "));
    return out;
}

bool writesOutsidePackage(const QString& masked) {
    static const QRegularExpression redirect(QStringLiteral(">{1,2}\\s*[\"']?/(usr|etc|opt|var|boot|root|srv|bin|sbin|lib|lib64)/"));
    if (redirect.match(masked).hasMatch()) {
        return true;
    }
    static const QRegularExpression write(QStringLiteral(CMD R"((install|cp|mv|mkdir|ln|rm|tee|touch|chown|chmod|rmdir)\s[^#;|&]*?\s["']?/(usr|etc|opt|var|boot|root|srv|bin|sbin|lib|lib64)\b)"));
    const auto match = write.match(masked);
    if (!match.hasMatch()) {
        return false;
    }
    // copying or linking *from* the system *into* the package is ordinary packaging
    static const QRegularExpression copy(QStringLiteral("^(install|cp|ln|mv)$"));
    if (copy.match(match.captured(1)).hasMatch()) {
        const auto command = masked.mid(match.capturedStart()).section(QRegularExpression(QStringLiteral("[;|&]")), 0, 0).trimmed();
        if (command.endsWith(QLatin1String("PKGPATH"))) {
            return false;
        }
    }
    return true;
}

}  // namespace

Analysis scanPkgbuild(const QString& text) {
    Analysis analysis;
    const auto file  = QString::fromLatin1(PKGBUILD);
    const auto lines  = logicalLines(text);
    const auto parsed = parse(text, static_cast<int>(text.count(u'\n')) + 1);

    scanSources(parsed, analysis);

    static const QRegularExpression functionStart(QStringLiteral("^\\s*(?:function\\s+)?([A-Za-z_][A-Za-z0-9_.+-]*)\\s*\\(\\s*\\)"));
    static const QRegularExpression assignment(QStringLiteral("^\\s*(?:(?:export|local|declare(?:\\s+-\\S+)*|readonly)\\s+)?[A-Za-z_][A-Za-z0-9_]*\\+?="));
    static const QRegularExpression plainArray(QStringLiteral("^\\s*(?:local\\s+)?[A-Za-z_][A-Za-z0-9_]*\\+?=\\([^$`]*\\)?\\s*$"));
    static const QRegularExpression homeAssigned(QStringLiteral("\\b(?:HOME|[A-Z_]+_HOME|GOPATH)="));

    QHash<QString, int> hits;
    bool inFunction = false;
    int depth       = 0;
    int topLevel    = 0;
    int outside     = 0;
    for (const auto& [line, raw, code] : lines) {
        const bool starts = !inFunction && functionStart.match(code).hasMatch();
        if (starts) {
            inFunction = true;
            depth      = 0;
        }
        const bool inBody = inFunction && !starts;
        if (inFunction) {
            depth += static_cast<int>(code.count(u'{') - code.count(u'}'));
            if (depth <= 0 && code.contains(u'}')) {
                inFunction = false;
            }
        }

        if (parsed.dataLine.value(line) || plainArray.match(code).hasMatch()) {
            continue;
        }

        const bool substitution = code.contains(QLatin1String("$(")) || code.contains(u'`');
        if (!inBody && !starts) {
            // Outside of functions only look at lines that execute something: descriptions
            // and option strings would otherwise match command names.
            const bool assigns = assignment.match(code).hasMatch();
            if (assigns && !substitution) {
                continue;
            }
            if (substitution && ++topLevel <= MAX_PER_RULE) {
                analysis.findings << makeFinding("top-level-command", Severity::Info, file, line, tr("Runs a command as soon as the PKGBUILD is read"),
                    tr("Code outside of the build functions already runs when package information is generated, before anything is built."), raw);
            }
        }

        auto masked = maskBuildPaths(code);
        if (homeAssigned.match(masked).hasMatch()) {
            // redirecting HOME or a tool's home into the build directory is the careful thing to do
            masked.replace(QRegularExpression(QStringLiteral("\\$HOME\\b|\\$\\{HOME\\}")), QStringLiteral("PKGPATH"));
        }
        applyRules(masked, raw, file, line, InPkgbuild, hits, analysis.findings);
        if (writesOutsidePackage(masked) && ++outside <= MAX_PER_RULE) {
            analysis.findings << makeFinding("system-write", Severity::Warning, file, line, tr("Writes outside the package directory"),
                tr("A build may only create files below $pkgdir and $srcdir. This line changes the running system directly."), raw);
        }
    }

    sort(analysis.findings);
    return analysis;
}

QList<Finding> scanScript(const QString& text, const QString& file) {
    QList<Finding> findings;
    QHash<QString, int> hits;
    for (const auto& [line, raw, code] : logicalLines(text)) {
        applyRules(code, raw, file, line, InScript, hits, findings);
    }
    sort(findings);
    return findings;
}

QList<Finding> scanMetadata(const Metadata& meta, const QDateTime& now) {
    QList<Finding> findings;
    constexpr qint64 DAY = 86400;
    const auto seconds   = now.toSecsSinceEpoch();
    const auto ageDays   = meta.firstSubmitted > 0 ? (seconds - meta.firstSubmitted) / DAY : -1;
    const auto editHours = meta.lastModified > 0 ? (seconds - meta.lastModified) / 3600 : -1;

    if (meta.maintainer.isEmpty()) {
        findings << makeFinding("orphaned", Severity::Warning, {}, 0, tr("Nobody maintains this package"),
            tr("Orphaned packages can be adopted by anyone, and nobody is watching for problems."));
    } else if (!meta.submitter.isEmpty() && meta.submitter != meta.maintainer) {
        findings << makeFinding("adopted", Severity::Info, {}, 0, tr("The maintainer has changed"),
            tr("Submitted by %1, now maintained by %2. Taking over an abandoned package is a known way to slip in malware.").arg(meta.submitter, meta.maintainer));
    }
    if (ageDays >= 0 && ageDays < 30) {
        findings << makeFinding("new-package", Severity::Warning, {}, 0, tr("This package is very new"),
            tr("It was first submitted %1 day(s) ago, so few people have looked at it.").arg(ageDays));
    }
    if (editHours >= 0 && editHours < 72 && ageDays >= 30) {
        findings << makeFinding("recent-change", Severity::Info, {}, 0, tr("This package changed very recently"),
            tr("The last change is %1 hour(s) old. Malicious updates are usually found and removed within days.").arg(editHours));
    }
    if (meta.votes < 3 && meta.popularity < 0.05) {
        findings << makeFinding("few-users", Severity::Info, {}, 0, tr("Hardly anybody uses this package"),
            tr("It has %1 vote(s). Problems in rarely used packages stay unnoticed for longer.").arg(meta.votes));
    }
    return findings;
}

namespace {

/// Optimal string alignment distance, stopped early once it exceeds 1.
bool withinOneEdit(QStringView a, QStringView b) {
    if (a.size() > b.size()) {
        std::swap(a, b);
    }
    if (b.size() - a.size() > 1) {
        return false;
    }
    qsizetype i = 0;
    while (i < a.size() && a.at(i) == b.at(i)) {
        ++i;
    }
    if (i == a.size()) {
        return true;  // equal, or one character appended
    }
    if (a.size() == b.size()) {
        // one substitution, or two neighbours swapped
        if (a.mid(i + 1) == b.mid(i + 1)) {
            return true;
        }
        return i + 1 < a.size() && a.at(i) == b.at(i + 1) && a.at(i + 1) == b.at(i) && a.mid(i + 2) == b.mid(i + 2);
    }
    return a.mid(i) == b.mid(i + 1);  // one character inserted
}

QString withoutDigits(const QString& text) {
    QString out;
    for (const auto c : text) {
        if (!c.isDigit()) {
            out += c;
        }
    }
    return out;
}

/// Drops the suffixes AUR variants of a package commonly carry.
QString baseName(QString name) {
    static constexpr std::array SUFFIXES = {"-bin", "-git", "-appimage", "-nightly", "-beta", "-dev", "-stable", "-wayland", "-qt5", "-qt6", "-gtk", "-electron"};
    bool stripped = true;
    while (stripped) {
        stripped = false;
        for (const char* suffix : SUFFIXES) {
            const QLatin1String view(suffix);
            if (name.size() > view.size() + 2 && name.endsWith(view)) {
                name.chop(view.size());
                stripped = true;
            }
        }
    }
    return name;
}

}  // namespace

QString similarName(const QString& name, const QStringList& known) {
    const auto base = baseName(name);
    if (base.size() < 5 || known.contains(name) || known.contains(base)) {
        return {};
    }
    const auto baseLetters = withoutDigits(base);
    for (const auto& candidate : known) {
        if (candidate.size() < 5 || qAbs(candidate.size() - base.size()) > 1 || !withinOneEdit(base, candidate)) {
            continue;
        }
        // foo2 next to foo3, or qt5-x next to qt6-x, are versions and not look-alikes
        if (withoutDigits(candidate) == baseLetters) {
            continue;
        }
        return candidate;
    }
    return {};
}

void sort(QList<Finding>& findings) {
    std::ranges::stable_sort(findings, [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) {
            return a.severity > b.severity;
        }
        // metadata first, then the PKGBUILD, then the other files
        const auto rank = [](const Finding& f) { return f.file.isEmpty() ? 0 : f.file == QLatin1String(PKGBUILD) ? 1 : 2; };
        if (rank(a) != rank(b)) {
            return rank(a) < rank(b);
        }
        if (a.file != b.file) {
            return a.file < b.file;
        }
        return a.line < b.line;
    });
}

QString severityName(Severity severity) {
    switch (severity) {
    case Severity::Danger:
        return QStringLiteral("danger");
    case Severity::Warning:
        return QStringLiteral("warning");
    case Severity::Info:
        break;
    }
    return QStringLiteral("info");
}

}  // namespace pkgscan
