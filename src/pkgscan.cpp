// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkgscan.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace pkgscan {

namespace {

constexpr auto PKGBUILD         = "PKGBUILD"_L1;
constexpr auto SOURCE           = "source"_L1;
constexpr int MAX_PER_RULE      = 5;
constexpr qsizetype MAX_EXCERPT = 160;

enum Scope : unsigned { InPkgbuild = 1U,
    InScript                       = 2U,
    Everywhere                     = InPkgbuild | InScript };

/// Only the first matching rule of a group is reported for a line.
enum Group : unsigned { NoGroup = 0U,
    Net                         = 1U,
    Decode                      = 2U };

QString tr(const char* text) {
    return QCoreApplication::translate("pkgscan", text);
}

/// Something at the position of a command, as opposed to an argument or a string.
#define CMD R"((?:^|[;&|({`]|\b(?:then|do|else|elif|if|while|exec|sudo|xargs|env|time|nice)\s|\bExec\s*=)\s*)"

struct Rule {
    const char* id;
    Severity severity;
    unsigned scope;
    unsigned group;
    const char* pattern;
    const char* title;
    const char* detail;
};

// Ordered so that the most specific rule of a group comes first.
constexpr std::array RULES = {
    Rule{"pipe-to-shell", Severity::Danger, Everywhere, Net,
        R"(\b(?:curl|wget|fetch|aria2c)\b[^|#]*\|\s*(?:sudo\s+)?(?:env\s+)?(?:(?:ba|z|da|k|fi)?sh|python[0-9.]*|perl|ruby|node)\b)"
        R"(|\b(?:ba|z|da)?sh\s+<\(\s*(?:curl|wget)\b|\b(?:ba|z|da)?sh\s+-c\s+["']?\$\(\s*(?:curl|wget)\b)"
        R"(|\b(?:source|\.)\s+<\(\s*(?:curl|wget)\b|\beval\b[^#]*\$\(\s*(?:curl|wget)\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Downloads a script and runs it directly"),
        QT_TRANSLATE_NOOP("pkgscan", "The downloaded code is executed without being stored or verified, so it can change at any time.")},
    Rule{"download", Severity::Warning, Everywhere, Net,
        CMD R"((?:curl|wget|aria2c)\s|)" CMD R"(git\s+(?:-\S+\s+)*clone\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Downloads something outside the source list"),
        QT_TRANSLATE_NOOP("pkgscan", "Only files listed in source=() are checked against a checksum. This download is not.")},
    Rule{"decode-exec", Severity::Danger, Everywhere, Decode,
        R"(\bbase64\s+(?:-d|-D|--decode)\b[^#]*\|\s*(?:sudo\s+)?(?:ba|z|da)?sh\b|\beval\b[^#]*\bbase64\b|\bxxd\s+-r\b[^#]*\|\s*(?:ba|z|da)?sh\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Decodes hidden text and runs it"),
        QT_TRANSLATE_NOOP("pkgscan", "Encoding a command before running it has no purpose other than hiding what it does.")},
    Rule{"decode", Severity::Warning, Everywhere, Decode,
        R"(\bbase64\s+(?:-d|-D|--decode)\b|\bopenssl\s+enc\s+-d\b|\bxxd\s+-r\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Decodes encoded data"),
        QT_TRANSLATE_NOOP("pkgscan", "Check what the decoded data is and what happens with it.")},
    Rule{"reverse-shell", Severity::Danger, Everywhere, NoGroup,
        R"(/dev/(?:tcp|udp)/|\b(?:nc|ncat|netcat)\b[^#]*\s-[a-zA-Z]*e\b|\bsocat\b[^#]*\bexec:|\bbash\s+-i\s*>&)",
        QT_TRANSLATE_NOOP("pkgscan", "Opens a network connection from the shell"),
        QT_TRANSLATE_NOOP("pkgscan", "This is how remote-control backdoors are started. A build or install script has no reason to do this.")},
    Rule{"credentials", Severity::Danger, Everywhere, NoGroup,
        R"(\.ssh/|\bid_(?:rsa|ed25519|ecdsa)\b|\.gnupg\b|/etc/shadow\b|\.aws/credentials|\.mozilla/|cookies\.sqlite|logins\.json|Login Data)"
        R"(|\.config/(?:google-chrome|chromium|BraveSoftware|discord)|wallet\.dat|\.kube/config|\.netrc\b|\.git-credentials|\.local/share/keyrings)",
        QT_TRANSLATE_NOOP("pkgscan", "Accesses keys, passwords or browser data"),
        QT_TRANSLATE_NOOP("pkgscan", "These files hold your logins. Building or installing a package never needs them.")},
    Rule{"root-access", Severity::Danger, Everywhere, NoGroup,
        R"(/etc/sudoers|\bvisudo\b|/etc/polkit-1/rules\.d|/etc/ld\.so\.preload|/etc/pam\.d)",
        QT_TRANSLATE_NOOP("pkgscan", "Changes who may become administrator"),
        QT_TRANSLATE_NOOP("pkgscan", "Editing sudo, polkit, PAM or the library preload list can give lasting, silent root access.")},
    Rule{"destructive", Severity::Danger, Everywhere, NoGroup,
        CMD R"(rm\s+(?:-\S+\s+)*["']?(?:/|~|\$HOME|\$\{HOME\})["']?/?\*?(?:\s|$))",
        QT_TRANSLATE_NOOP("pkgscan", "Deletes a whole directory tree"),
        QT_TRANSLATE_NOOP("pkgscan", "This removes the root or home directory.")},
    Rule{"obfuscated-escapes", Severity::Warning, Everywhere, NoGroup,
        R"((?:\\x[0-9a-fA-F]{2}){8,}|(?:\\[0-7]{3}){8,})",
        QT_TRANSLATE_NOOP("pkgscan", "Contains text hidden as escape codes"),
        QT_TRANSLATE_NOOP("pkgscan", "Readable scripts do not need to spell out text byte by byte.")},
    Rule{"eval", Severity::Warning, Everywhere, NoGroup,
        CMD R"(eval\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Runs text that is put together at run time"),
        QT_TRANSLATE_NOOP("pkgscan", "With eval the command that really runs is not visible in the script. Check where its input comes from.")},
    Rule{"privilege", Severity::Warning, InPkgbuild, NoGroup,
        CMD R"((?:sudo|doas|pkexec)\s|)" CMD R"(su\s+(?:-\S*\s+)*\S)",
        QT_TRANSLATE_NOOP("pkgscan", "Asks for administrator rights while building"),
        QT_TRANSLATE_NOOP("pkgscan", "A build runs as your user and must not need root.")},
    Rule{"home", Severity::Warning, Everywhere, NoGroup,
        R"((?<![A-Za-z0-9_])(?:\$HOME\b|\$\{HOME\}|~/)|(?<![A-Za-z0-9_.-])/home/|/root/)",
        QT_TRANSLATE_NOOP("pkgscan", "Touches a home directory"),
        QT_TRANSLATE_NOOP("pkgscan", "Packages install into system directories. Check what is read from or written to the home directory.")},
    Rule{"autostart", Severity::Warning, Everywhere, NoGroup,
        R"(\bcrontab\b|/etc/cron|\.config/autostart|/etc/xdg/autostart|\.(?:bashrc|zshrc|bash_profile|zprofile|profile)\b|/etc/profile\.d|\bLD_PRELOAD=)",
        QT_TRANSLATE_NOOP("pkgscan", "Sets something up to run automatically"),
        QT_TRANSLATE_NOOP("pkgscan", "Cron jobs, autostart entries and shell start-up files keep running after the installation.")},
    Rule{"service", Severity::Warning, Everywhere, NoGroup,
        CMD R"(systemctl\s+(?:-\S+\s+)*(?:enable|start|restart|mask|unmask)\b)",
        QT_TRANSLATE_NOOP("pkgscan", "Enables or starts a service"),
        QT_TRANSLATE_NOOP("pkgscan", "Packages should leave it to you to enable their services.")},
    Rule{"setuid", Severity::Warning, Everywhere, NoGroup,
        CMD R"(chmod\s+(?:-\S+\s+)*(?:[4267][0-7]{3}\b|[ugoa]*\+[rwxX]*s))" "|" CMD R"(setcap\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Gives a program elevated rights"),
        QT_TRANSLATE_NOOP("pkgscan", "A setuid bit or capability lets the program act with more rights than the user who starts it.")},
    Rule{"tls-off", Severity::Warning, Everywhere, NoGroup,
        R"(--no-check-certificate|\bcurl\b[^#|;]*\s(?:-[a-zA-Z]*k[a-zA-Z]*|--insecure)(?:\s|$)|GIT_SSL_NO_VERIFY|sslVerify\s*=?\s*false|NODE_TLS_REJECT_UNAUTHORIZED)",
        QT_TRANSLATE_NOOP("pkgscan", "Turns off the check of the server's identity"),
        QT_TRANSLATE_NOOP("pkgscan", "Without certificate checks anybody on the network can replace the download.")},
    Rule{"system-state", Severity::Warning, InPkgbuild, NoGroup,
        CMD R"((?:modprobe|insmod|iptables|nft|chattr|mount|sysctl|useradd|usermod|passwd|groupadd|pacman|systemd-run)\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Changes the system while building"),
        QT_TRANSLATE_NOOP("pkgscan", "A build only creates files inside its own directory.")},
    Rule{"inline-script", Severity::Info, Everywhere, NoGroup,
        CMD R"((?:python[0-9.]*|perl|ruby|node)\s+(?:-\S+\s+)*-[ce]\s)",
        QT_TRANSLATE_NOOP("pkgscan", "Runs an inline script in another language"),
        QT_TRANSLATE_NOOP("pkgscan", "Read the embedded code as carefully as the rest.")},
};

/// Findings that do not come from a line rule.
constexpr std::array OTHER_IDS = {"encoded-blob", "top-level-command", "system-write", "dynamic-source", "ip-host",
    "throwaway-host", "vcs-unpinned", "no-checksum", "weak-checksum", "srcinfo-mismatch"};
constexpr std::size_t KINDS = RULES.size() + OTHER_IDS.size();

constexpr const char* idOf(std::size_t kind) {
    return kind < RULES.size() ? RULES.at(kind).id : OTHER_IDS.at(kind - RULES.size());
}

consteval std::size_t kind(std::string_view id) {
    for (std::size_t k = 0; k < KINDS; ++k) {
        if (id == idOf(k)) {
            return k;
        }
    }
    throw "unknown finding id";
}

const std::array<QRegularExpression, RULES.size()>& compiledRules() {
    static const auto rules = [] {
        std::array<QRegularExpression, RULES.size()> out;
        std::ranges::transform(RULES, out.begin(), [](const Rule& rule) { return QRegularExpression(QLatin1StringView(rule.pattern)); });
        return out;
    }();
    return rules;
}

QString excerptOf(QStringView raw) {
    const auto text = raw.trimmed();
    return text.size() > MAX_EXCERPT ? text.first(MAX_EXCERPT - 3).toString() + u'…' : text.toString();
}

/// Collects findings for one file, at most MAX_PER_RULE of each kind.
class Report {
 public:
    Report(QList<Finding>& out, QString file) : m_out(out), m_file(std::move(file)) { }

    void operator()(std::size_t kind, Severity severity, int line, const QString& title, const QString& detail, QStringView excerpt) {
        if (++m_hits.at(kind) <= MAX_PER_RULE) {
            m_out << Finding{QString::fromLatin1(idOf(kind)), severity, m_file, line, title, detail, excerptOf(excerpt)};
        }
    }

 private:
    QList<Finding>& m_out;
    QString m_file;
    std::array<int, KINDS> m_hits{};
};

/// Cuts an unquoted trailing comment. Quotes are tracked per line only.
QStringView stripComment(QStringView line) {
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
            return line.first(i);
        }
    }
    return line;
}

/// A long run of base64 characters that is not a hexadecimal checksum.
bool hasEncodedBlob(const QString& code) {
    static const QRegularExpression blob(u"[A-Za-z0-9+/]{160,}={0,2}"_s);
    const auto isHex = [](QChar c) { return c.isDigit() || (c.toLower() >= u'a' && c.toLower() <= u'f'); };
    for (const auto& match : blob.globalMatch(code)) {
        const auto text = match.capturedView();
        // paths and URLs are long runs of the same alphabet, real base64 rarely has many slashes
        if (!std::ranges::all_of(text, isHex) && text.count(u'/') < text.size() / 12) {
            return true;
        }
    }
    return false;
}

/// Applies the line rules. `code` is the line with harmless parts (package directory paths) masked.
void applyRules(const QString& code, QStringView raw, int line, unsigned scope, Report& report) {
    unsigned taken = NoGroup;
    for (std::size_t k = 0; k < RULES.size(); ++k) {
        const auto& rule = RULES.at(k);
        if ((rule.scope & scope) == 0U || (rule.group & taken) != 0U || !compiledRules().at(k).match(code).hasMatch()) {
            continue;
        }
        taken |= rule.group;
        report(k, rule.severity, line, tr(rule.title), tr(rule.detail), raw);
    }
    if (hasEncodedBlob(code)) {
        report(kind("encoded-blob"), Severity::Warning, line, tr("Contains a long block of encoded data"),
            tr("Encoded blocks can hide a program or script. Check what it is decoded into."), raw);
    }
}

/// Letters, digits and "@._+-", not starting with "." or "-": a name that stays inside its directory.
bool plainName(QStringView name, bool upper) {
    const auto allowed = [upper](QChar c) {
        const auto u = c.unicode();
        return (u >= u'a' && u <= u'z') || (upper && u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || QStringView(u"@._+-").contains(c);
    };
    return !name.isEmpty() && name.size() <= 128 && name.front() != u'.' && name.front() != u'-' && std::ranges::all_of(name, allowed);
}

// ---------------------------------------------------------------------------------------
// Sources

struct Entry {
    /// With the variables that could be resolved expanded.
    QString value;
    /// As written in the PKGBUILD, for display.
    QString raw;
    int line = 0;
};

using Arrays = QHash<QString, QList<Entry>>;

struct Sources {
    Arrays arrays;
    /// Every value assigned to `install`: a split package can set one per package.
    QStringList installs;
    QString pkgname;
};

struct Parsed {
    Sources sources;
    QHash<QString, QString> scalars;
    /// Lines that only hold array data (dependency names, checksums, ...).
    QList<bool> dataLine;
};

/// Replaces `$name` and `${name}` with known top-level values. Anything else is left alone.
QString expand(QString value, const Parsed& parsed) {
    static const QRegularExpression variable(u"\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}|\\$([A-Za-z_][A-Za-z0-9_]*)"_s);
    // ${NAME:-fallback}: an environment override, the fallback is what a normal build uses
    static const QRegularExpression fallback(u"\\$\\{[A-Za-z_][A-Za-z0-9_]*:?[-=]([^{}]*)\\}"_s);
    value.replace(fallback, u"\\1"_s);
    for (int pass = 0; pass < 8; ++pass) {
        QString out;
        qsizetype last = 0;
        bool changed   = false;
        for (const auto& match : variable.globalMatch(value)) {
            const auto name  = match.captured(match.lastCapturedIndex());
            const auto array = parsed.sources.arrays.constFind(name);
            out += QStringView(value).sliced(last, match.capturedStart() - last);
            last = match.capturedEnd();
            if (const auto scalar = parsed.scalars.constFind(name); scalar != parsed.scalars.cend()) {
                out += *scalar;
            } else if (array != parsed.sources.arrays.cend() && !array->isEmpty()) {
                out += array->first().raw;
            } else {
                out += match.capturedView();
                continue;
            }
            changed = true;
        }
        out += QStringView(value).sliced(last);
        value = out.replace(fallback, u"\\1"_s);
        if (!changed) {
            break;
        }
    }
    return value;
}

/// Reads arrays, scalars and install scriptlets from the PKGBUILD text, as far as that is possible without running it.
Parsed parse(const QString& text) {
    Parsed parsed;
    auto& arrays = parsed.sources.arrays;
    parsed.dataLine.fill(false, text.count(u'\n') + 3);

    static const QRegularExpression arrayStart(u"^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)\\+?=\\("_s, QRegularExpression::MultilineOption);
    qsizetype counted = 0;
    int startLine     = 1;
    for (const auto& match : arrayStart.globalMatch(text)) {
        startLine += static_cast<int>(QStringView(text).sliced(counted, match.capturedStart() - counted).count(u'\n'));
        counted        = match.capturedStart();
        auto& entries  = arrays[match.captured(1)];
        const auto isCode = [&](qsizetype i) { return text.at(i) == u'`' || (text.at(i) == u'$' && i + 1 < text.size() && text.at(i + 1) == u'('); };

        int line      = startLine;
        int tokenLine = line;
        bool inToken  = false;
        bool hasCode  = false;
        QString token;
        QChar quote;
        const auto begin = [&] {
            if (!inToken) {
                inToken   = true;
                tokenLine = line;
            }
        };
        const auto flush = [&] {
            if (inToken) {
                entries << Entry{{}, token, tokenLine};
            }
            token.clear();
            inToken = false;
        };
        qsizetype i = match.capturedEnd();
        for (; i < text.size(); ++i) {
            const auto c = text.at(i);
            line += c == u'\n' ? 1 : 0;
            if (!quote.isNull()) {
                if (c == quote) {
                    quote = QChar();
                } else {
                    // double quotes do not stop a command substitution
                    hasCode = hasCode || (quote == u'"' && isCode(i));
                    token += c;
                }
            } else if (c == u'\'' || c == u'"') {
                quote = c;
                begin();
            } else if (c == u')') {
                break;
            } else if (c.isSpace()) {
                flush();
            } else if (c == u'#' && !inToken) {
                while (i + 1 < text.size() && text.at(i + 1) != u'\n') {
                    ++i;
                }
            } else {
                hasCode = hasCode || isCode(i);
                begin();
                token += c;
            }
        }
        flush();
        // "name=(); command" continues with code behind the closing parenthesis
        if (i < text.size()) {
            auto end = text.indexOf(u'\n', i);
            end      = end < 0 ? text.size() : end;
            hasCode  = hasCode || !stripComment(QStringView(text).sliced(i + 1, end - i - 1)).trimmed().isEmpty();
        }
        // arrays that run commands are still looked at by the line rules
        for (int n = startLine; !hasCode && n <= line && n < parsed.dataLine.size(); ++n) {
            parsed.dataLine[n] = true;
        }
    }

    static const QRegularExpression scalar(u"^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)=(?!\\()(.*)$"_s, QRegularExpression::MultilineOption);
    QStringList installs;
    for (const auto& match : scalar.globalMatch(text)) {
        auto value = stripComment(match.capturedView(2)).trimmed();
        if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\'')) && value.endsWith(value.front())) {
            value = value.sliced(1, value.size() - 2);
        }
        parsed.scalars.insert(match.captured(1), value.toString());
        if (match.capturedView(1) == "install"_L1 && !installs.contains(value)) {
            installs << value.toString();
        }
    }

    for (auto& entries : arrays) {
        for (auto& entry : entries) {
            entry.value = expand(entry.raw, parsed);
        }
    }
    for (const auto& install : std::as_const(installs)) {
        parsed.sources.installs << expand(install, parsed);
    }
    parsed.sources.pkgname = expand(u"$pkgname"_s, parsed);
    return parsed;
}

/// Reads the `key = value` lines of a .SRCINFO. Values are matched to their entry in the
/// PKGBUILD to report the line and text a reviewer finds there.
Sources parseSrcinfo(const QString& srcinfo, const Arrays& pkgbuild) {
    Sources out;
    for (const auto line : QStringView(srcinfo).split(u'\n')) {
        const auto separator = line.indexOf(u" = ");
        if (separator < 0) {
            continue;
        }
        const auto key   = line.first(separator).trimmed().toString();
        const auto value = line.sliced(separator + 3).trimmed().toString();
        const auto& candidates = pkgbuild.value(key);
        const auto known       = std::ranges::find(candidates, value, &Entry::value);
        out.arrays[key] << (known != candidates.end() ? *known : Entry{value, value, candidates.isEmpty() ? 0 : candidates.first().line});
    }
    for (const auto& entry : out.arrays.value(u"install"_s)) {
        out.installs << entry.value;
    }
    out.pkgname = out.arrays.value(u"pkgname"_s).value(0).value;
    return out;
}

bool isSourceArray(const QString& name) {
    return name == SOURCE || (name.startsWith(SOURCE) && name.at(SOURCE.size()) == u'_');
}

/// Reports sources of the PKGBUILD that resolve to an address the .SRCINFO, and with it the AUR web page, does not show.
bool matchesSrcinfo(const Arrays& pkgbuild, const Sources& srcinfo, Report& report) {
    bool matches = true;
    for (const auto& [name, entries] : pkgbuild.asKeyValueRange()) {
        if (!isSourceArray(name)) {
            continue;
        }
        const auto& listed = srcinfo.arrays.value(name);
        for (const auto& entry : entries) {
            const bool resolved = !entry.value.contains(u'$') && !entry.value.contains(u'{') && !entry.value.contains(u'`');
            if (resolved && std::ranges::find(listed, entry.value, &Entry::value) == listed.end()) {
                matches = false;
                report(kind("srcinfo-mismatch"), Severity::Warning, entry.line, tr("The PKGBUILD downloads something .SRCINFO does not list"),
                    tr("The AUR shows the sources from .SRCINFO, but makepkg downloads what the PKGBUILD says."), entry.raw);
            }
        }
    }
    return matches;
}

/// Places anyone can upload to anonymously, or that hide the real address.
bool isThrowawayHost(const QString& host) {
    static constexpr std::array HOSTS = {
        "bit.ly", "tinyurl.com", "t.co", "is.gd", "goo.gl", "cutt.ly", "rb.gy", "shorturl.at",
        "pastebin.com", "paste.rs", "paste.ee", "dpaste.com", "dpaste.org", "hastebin.com", "termbin.com", "ix.io", "rentry.co",
        "0x0.st", "transfer.sh", "temp.sh", "file.io", "catbox.moe", "anonfiles.com", "gofile.io",
        "cdn.discordapp.com", "media.discordapp.net", "ngrok.io", "ngrok-free.app", "trycloudflare.com", "serveo.net",
        "gist.githubusercontent.com", "mega.nz", "mediafire.com", "dropbox.com", "dropboxusercontent.com", "drive.google.com",
    };
    return host.endsWith(".onion"_L1) || std::ranges::any_of(HOSTS, [&](const char* domain) {
        const QLatin1StringView view(domain);
        return host == view || (host.endsWith(view) && host.at(host.size() - view.size() - 1) == u'.');
    });
}

template <std::size_t N>
bool endsWithAny(const QString& name, const std::array<const char*, N>& suffixes) {
    return std::ranges::any_of(suffixes, [&](const char* suffix) { return name.endsWith(QLatin1StringView(suffix)); });
}

/// Splits "name::address". The name is empty for a source that is not renamed.
std::pair<QString, QString> splitRename(const QString& spec) {
    const auto rename = spec.indexOf("::"_L1);
    const auto scheme = spec.indexOf("://"_L1);
    if (rename > 0 && (scheme < 0 || rename < scheme)) {
        return {spec.first(rename), spec.sliced(rename + 2)};
    }
    return {{}, spec};
}

/// Name under which makepkg stores a source: the part before "::", else the last part of the
/// address. Only version control sources lose their "#fragment" and "?query"; for a plain
/// download makepkg keeps them, so "x.sig?download=1" is not stored as a signature.
QString localName(const QString& spec) {
    if (auto [name, _] = splitRename(spec); !name.isEmpty()) {
        return name;
    }
    // expansions that could not be resolved may hold "#", "?" and "/" of their own
    static const QRegularExpression expansion(u"\\$\\{[^}]*\\}"_s);
    static const QRegularExpression vcs(u"^((git|svn|hg|bzr|fossil)\\+|git://)"_s);
    static const QRegularExpression tail(u"[?#].*$"_s);
    auto address = spec;
    address.replace(expansion, u"$"_s);
    if (vcs.match(address).hasMatch()) {
        address.remove(tail);
        while (address.endsWith(u'/')) {
            address.chop(1);
        }
    }
    return address.section(u'/', -1);
}

/// Whether the PKGBUILD runs a local source file, whatever it is called.
bool runsFile(const QString& script, const QString& name) {
    // a command can be split over lines with a backslash, even in the middle of a word
    auto text = script;
    text.remove("\\\n"_L1);
    const QRegularExpression run(
        uR"((?:^|[\s;|&(!])(?:(?:ba|z|da|k|fi)?sh|python[0-9.]*|perl|ruby|node|lua|php|source|\.|exec|eval|chmod\s+\S*x\S*)\s[^\n;|&]*?(?<![A-Za-z0-9@._+-])%1(?![A-Za-z0-9@._+-]))"
        uR"(|(?:^|[\s;|&("'])\./%1(?![A-Za-z0-9@._+-]))"_s.arg(QRegularExpression::escape(name)),
        QRegularExpression::MultilineOption);
    return run.match(text).hasMatch();
}

void scanSources(const Sources& sources, const QString& text, Report& report, Analysis& analysis) {
    static constexpr std::array STRONG    = {"sha256sums", "sha512sums", "b2sums", "sha384sums", "sha224sums"};
    static constexpr std::array WEAK      = {"md5sums", "sha1sums", "cksums"};
    static constexpr std::array VCS_NAMES = {"-git", "-svn", "-hg", "-bzr", "-cvs", "-fossil", "-darcs", "-nightly"};
    static constexpr std::array SCRIPTS   = {".install", ".sh", ".bash", ".hook", ".service", ".timer", ".path", ".socket", ".py", ".desktop", ".rules"};
    static const QRegularExpression vcsPrefix(u"^(git|svn|hg|bzr|fossil)\\+"_s);
    static const QRegularExpression ipHost(u"^(\\d{1,3}\\.){3}\\d{1,3}$"_s);
    static const QRegularExpression pinned(u"#(commit|tag|revision)="_s);
    // makepkg treats a source as a detached signature when its stored name ends in .sig,
    // .sign or .asc, and checks it against the source of the same name without that ending
    // (also when that source is stored compressed: x.tar.sign signs x.tar.xz). What counts is
    // the set of sources one architecture really gets: the common ones plus its own.
    static const QRegularExpression signature(u"\\.(sig|sign|asc)$"_s);
    static const QRegularExpression compression(u"\\.(gz|bz2|xz|lrz|lzo|Z|zst|lz4|lz)$"_s);

    QStringList names;
    QHash<QString, QSet<QString>> storedIn;
    QSet<QString> everything;
    for (const auto& [name, entries] : sources.arrays.asKeyValueRange()) {
        if (isSourceArray(name)) {
            names << name;
            for (const auto& entry : entries) {
                storedIn[name] << localName(entry.value);
            }
            everything += storedIn.value(name);
        }
    }
    names.sort();

    QSet<QString> stored;
    const auto unpacked = [](const QString& local) {
        const auto packed = compression.match(local);
        return packed.hasMatch() ? local.first(packed.capturedStart()) : QString();
    };
    // a signature file is recognised wherever its source is listed; whether a source counts
    // as signed is decided within the sources of its own architecture
    const auto isSignature = [&](const QString& local) {
        const auto match = signature.match(local);
        const auto base  = local.first(match.hasMatch() ? match.capturedStart() : 0);
        return match.hasMatch() && (everything.contains(base) || std::ranges::any_of(everything, [&](const QString& other) { return !base.isEmpty() && unpacked(other) == base; }));
    };
    const auto isSigned = [&](const QString& local) {
        return std::ranges::any_of(QStringList{local, unpacked(local)}, [&](const QString& base) {
            return !base.isEmpty() && std::ranges::any_of(std::array{".sig"_L1, ".sign"_L1, ".asc"_L1}, [&](QLatin1StringView ending) { return stored.contains(base + ending); });
        });
    };
    const bool hasKeys = !sources.arrays.value(u"validpgpkeys"_s).isEmpty();
    const bool vcsName = endsWithAny(sources.pkgname, VCS_NAMES);

    QStringList plainLocal;
    for (const auto& name : std::as_const(names)) {
        const auto arch     = name.sliced(SOURCE.size());
        const auto& entries = sources.arrays[name];
        stored              = storedIn.value(SOURCE) + storedIn.value(name);
        const auto checksum = [&](const char* algorithm, qsizetype index) {
            const auto sums = sources.arrays.value(QLatin1StringView(algorithm) + arch);
            return index < sums.size() && sums.at(index).value != "SKIP"_L1;
        };

        for (qsizetype i = 0; i < entries.size(); ++i) {
            const auto& entry   = entries.at(i);
            const auto local    = localName(entry.value);
            const auto spec     = splitRename(entry.value).second;
            const auto prefix   = vcsPrefix.match(spec);
            const bool vcs      = prefix.hasMatch() || spec.startsWith("git://"_L1);
            const auto address  = spec.sliced(prefix.hasMatch() ? prefix.capturedLength() : 0);
            const auto add      = [&](std::size_t id, Severity severity, const QString& title, const QString& detail) {
                report(id, severity, entry.line, title, detail, entry.raw);
            };

            if (!address.contains("://"_L1)) {
                if (plainName(address, true) && !plainLocal.contains(address)) {
                    plainLocal << address;
                }
                continue;
            }

            const QUrl url(address);
            const auto host = url.host().toLower();
            if (host.isEmpty() || host.contains(u'$')) {
                add(kind("dynamic-source"), Severity::Info, tr("Download address is put together from variables"), tr("Work out where this source really comes from."));
            } else {
                if (!analysis.hosts.contains(host)) {
                    analysis.hosts << host;
                }
                if (ipHost.match(host).hasMatch() || host.contains(u':')) {
                    add(kind("ip-host"), Severity::Warning, tr("Downloads from a bare IP address"),
                        tr("Real projects publish their files under a name. %1 cannot be traced to anybody.").arg(host));
                } else if (isThrowawayHost(host)) {
                    add(kind("throwaway-host"), Severity::Warning, tr("Downloads from a file-sharing, paste or link-shortening service"),
                        tr("Anybody can upload to %1 anonymously and replace the file later.").arg(host));
                }
            }

            if (vcs) {
                if (!pinned.match(address).hasMatch() && !vcsName) {
                    add(kind("vcs-unpinned"), Severity::Info, tr("Builds whatever is in the repository at that moment"),
                        tr("The source is not pinned to a commit or tag, so the code can differ from what the maintainer tested."));
                }
                continue;
            }

            // a detached signature needs no checksum of its own, and a source it signs is
            // verified against validpgpkeys
            if (isSignature(local)) {
                continue;
            }
            const bool verified  = std::ranges::any_of(STRONG, [&](const char* a) { return checksum(a, i); }) || (isSigned(local) && hasKeys);
            const bool weak      = std::ranges::any_of(WEAK, [&](const char* a) { return checksum(a, i); });
            const bool plaintext = url.scheme() == "http"_L1 || url.scheme() == "ftp"_L1;
            if (!verified && !weak) {
                add(kind("no-checksum"), Severity::Warning, tr("Download is not verified"),
                    plaintext ? tr("This file has no checksum and is fetched without encryption, so anybody on the network can replace it.")
                              : tr("This file has no checksum (SKIP), so whatever the server sends is accepted."));
            } else if (!verified) {
                add(kind("weak-checksum"), Severity::Info, tr("Download is only verified with an outdated checksum"),
                    tr("MD5 and SHA-1 can be forged. Current packages use SHA-256 or BLAKE2."));
            }
        }
    }

    // install scriptlets first, in the order of the PKGBUILD
    QStringList installs;
    for (const auto& install : sources.installs) {
        if (plainName(install, true) && !installs.contains(install)) {
            installs << install;
        }
    }
    analysis.localFiles = installs;
    for (const auto& name : std::as_const(plainLocal)) {
        if (!installs.contains(name)) {
            (endsWithAny(name, SCRIPTS) || runsFile(text, name) ? analysis.localFiles : analysis.otherLocalFiles) << name;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Line rules

struct LogicalLine {
    int line = 0;
    /// First physical line, for display.
    QString raw;
    /// Comment-free code with backslash continuations joined.
    QString code;
};

QList<LogicalLine> logicalLines(const QString& text) {
    QList<LogicalLine> out;
    const auto lines = QStringView(text).split(u'\n');
    for (qsizetype index = 0; index < lines.size(); ++index) {
        LogicalLine logical{static_cast<int>(index) + 1, lines.at(index).toString(), stripComment(lines.at(index)).toString()};
        while (logical.code.endsWith(u'\\') && index + 1 < lines.size()) {
            // like the shell: the backslash and the line break vanish, nothing is inserted
            logical.code.chop(1);
            logical.code += stripComment(lines.at(++index));
        }
        if (!logical.code.trimmed().isEmpty()) {
            out << logical;
        }
    }
    return out;
}

/// Masks paths below the package and source directories, which a PKGBUILD is meant to write to.
QString maskBuildPaths(const QString& code) {
    // stops in front of a command substitution, which is code and not part of the path
    static const QRegularExpression buildPath(u"[\"']?\\$\\{?(pkgdir|srcdir|startdir|_?builddir)\\}?(?:[^\\s;|&)$`]|\\$(?!\\())*"_s);
    return QString(code).replace(buildPath, u" PKGPATH "_s);
}

/// Whether a line consists of variable assignments only, with no command behind them.
bool onlyAssigns(const QString& code) {
    static const QRegularExpression name(u"\\G\\s*(?:(?:export|local|declare(?:\\s+-\\S+)*|readonly)\\s+)?[A-Za-z_][A-Za-z0-9_]*(?:\\[[^\\]]*\\])?\\+?="_s);
    qsizetype i = 0;
    bool any    = false;
    while (true) {
        const auto match = name.match(code, i);
        if (!match.hasMatch()) {
            return any && QStringView(code).sliced(i).trimmed().isEmpty();
        }
        any       = true;
        i         = match.capturedEnd();
        int depth = 0;
        QChar quote;
        for (; i < code.size(); ++i) {
            const auto c = code.at(i);
            if (!quote.isNull()) {
                if (c == u'\\' && quote == u'"') {
                    ++i;
                } else if (c == quote) {
                    quote = QChar();
                }
            } else if (c == u'\'' || c == u'"') {
                quote = c;
            } else if (c == u'\\') {
                ++i;
            } else if (c == u'(') {
                ++depth;
            } else if (c == u')' && depth > 0) {
                --depth;
            } else if (depth == 0 && (c.isSpace() || QStringView(u";|&<>)").contains(c))) {
                break;
            }
        }
        // a value that is still open continues on the next line
        if (!quote.isNull() || depth > 0) {
            return true;
        }
    }
}

bool writesOutsidePackage(const QString& masked) {
    static const QRegularExpression redirect(u">{1,2}\\s*[\"']?/(usr|etc|opt|var|boot|root|srv|bin|sbin|lib|lib64)/"_s);
    static const QRegularExpression write(QStringLiteral(CMD R"((install|cp|mv|mkdir|ln|rm|tee|touch|chown|chmod|rmdir)\s[^#;|&]*?\s["']?/(usr|etc|opt|var|boot|root|srv|bin|sbin|lib|lib64)\b)"));
    static const QRegularExpression separator(u"[;|&]"_s);
    static constexpr std::array<QStringView, 4> COPIES = {u"install", u"cp", u"ln", u"mv"};
    if (redirect.match(masked).hasMatch()) {
        return true;
    }
    const auto match = write.match(masked);
    if (!match.hasMatch()) {
        return false;
    }
    // copying or linking *from* the system *into* the package is ordinary packaging
    if (std::ranges::contains(COPIES, match.capturedView(1))) {
        auto command = QStringView(masked).sliced(match.capturedStart());
        if (const auto end = command.indexOf(separator); end >= 0) {
            command.truncate(end);
        }
        return !command.trimmed().endsWith(u"PKGPATH");
    }
    return true;
}

#undef CMD

}  // namespace

Analysis scanPkgbuild(const QString& text, const QString& srcinfo) {
    Analysis analysis;
    Report report(analysis.findings, PKGBUILD);
    const auto parsed = parse(text);
    if (srcinfo.isEmpty()) {
        scanSources(parsed.sources, text, report, analysis);
    } else {
        auto listed = parseSrcinfo(srcinfo, parsed.sources.arrays);
        // when they disagree, what makepkg really downloads is what counts
        if (!matchesSrcinfo(parsed.sources.arrays, listed, report)) {
            listed.arrays = parsed.sources.arrays;
        }
        listed.installs += parsed.sources.installs;
        scanSources(listed, text, report, analysis);
    }

    static const QRegularExpression functionStart(u"^\\s*(?:function\\s+)?([A-Za-z_][A-Za-z0-9_.+-]*)\\s*\\(\\s*\\)"_s);
    static const QRegularExpression plainArray(u"^\\s*(?:local\\s+)?[A-Za-z_][A-Za-z0-9_]*\\+?=\\([^$`()]*\\)?\\s*$"_s);
    static const QRegularExpression homeAssigned(u"\\b(?:HOME|[A-Z_]+_HOME|GOPATH)="_s);
    static const QRegularExpression homeVariable(u"\\$HOME\\b|\\$\\{HOME\\}"_s);

    bool inFunction = false;
    qsizetype depth = 0;
    for (const auto& [line, raw, code] : logicalLines(text)) {
        const bool starts = !inFunction && functionStart.match(code).hasMatch();
        if (starts) {
            inFunction = true;
            depth      = 0;
        }
        const bool inBody = inFunction && !starts;
        if (inFunction) {
            depth += code.count(u'{') - code.count(u'}');
            inFunction = depth > 0 || !code.contains(u'}');
        }

        if (parsed.dataLine.value(line) || plainArray.match(code).hasMatch()) {
            continue;
        }

        const bool substitution = code.contains("$("_L1) || code.contains(u'`');
        if (!inBody && !starts) {
            // Outside of functions only look at lines that execute something: descriptions
            // and option strings would otherwise match command names.
            if (!substitution && onlyAssigns(code)) {
                continue;
            }
            if (substitution) {
                report(kind("top-level-command"), Severity::Info, line, tr("Runs a command as soon as the PKGBUILD is read"),
                    tr("Code outside of the build functions already runs when package information is generated, before anything is built."), raw);
            }
        }

        auto masked = maskBuildPaths(code);
        if (homeAssigned.match(masked).hasMatch()) {
            // redirecting HOME or a tool's home into the build directory is the careful thing to do
            masked.replace(homeVariable, u"PKGPATH"_s);
        }
        applyRules(masked, raw, line, InPkgbuild, report);
        if (writesOutsidePackage(masked)) {
            report(kind("system-write"), Severity::Warning, line, tr("Writes outside the package directory"),
                tr("A build may only create files below $pkgdir and $srcdir. This line changes the running system directly."), raw);
        }
    }

    sort(analysis.findings);
    return analysis;
}

QList<Finding> scanScript(const QString& text, const QString& file) {
    QList<Finding> findings;
    Report report(findings, file);
    for (const auto& [line, raw, code] : logicalLines(text)) {
        applyRules(code, raw, line, InScript, report);
    }
    sort(findings);
    return findings;
}

QList<Finding> scanMetadata(const Metadata& meta, const QDateTime& now) {
    QList<Finding> findings;
    const auto add = [&](const char* id, Severity severity, const QString& title, const QString& detail) {
        findings << Finding{QString::fromLatin1(id), severity, {}, 0, title, detail, {}};
    };
    constexpr qint64 DAY = 86400;
    const auto seconds   = now.toSecsSinceEpoch();
    const auto ageDays   = meta.firstSubmitted > 0 ? (seconds - meta.firstSubmitted) / DAY : -1;
    const auto editHours = meta.lastModified > 0 ? (seconds - meta.lastModified) / 3600 : -1;

    if (meta.maintainer.isEmpty()) {
        add("orphaned", Severity::Warning, tr("Nobody maintains this package"),
            tr("Orphaned packages can be adopted by anyone, and nobody is watching for problems."));
    } else if (!meta.submitter.isEmpty() && meta.submitter != meta.maintainer) {
        add("adopted", Severity::Info, tr("The maintainer has changed"),
            tr("Submitted by %1, now maintained by %2. Taking over an abandoned package is a known way to slip in malware.").arg(meta.submitter, meta.maintainer));
    }
    if (ageDays >= 0 && ageDays < 30) {
        add("new-package", Severity::Warning, tr("This package is very new"),
            tr("It was first submitted %1 day(s) ago, so few people have looked at it.").arg(ageDays));
    }
    if (editHours >= 0 && editHours < 72 && ageDays >= 30) {
        add("recent-change", Severity::Info, tr("This package changed very recently"),
            tr("The last change is %1 hour(s) old. Malicious updates are usually found and removed within days.").arg(editHours));
    }
    if (meta.votes < 3 && meta.popularity < 0.05) {
        add("few-users", Severity::Info, tr("Hardly anybody uses this package"),
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
        return a.sliced(i + 1) == b.sliced(i + 1)
            || (i + 1 < a.size() && a.at(i) == b.at(i + 1) && a.at(i + 1) == b.at(i) && a.sliced(i + 2) == b.sliced(i + 2));
    }
    return a.sliced(i) == b.sliced(i + 1);  // one character inserted
}

QString withoutDigits(QString text) {
    return text.removeIf([](QChar c) { return c.isDigit(); });
}

/// Drops the suffixes AUR variants of a package commonly carry.
QString baseName(QString name) {
    static constexpr std::array SUFFIXES = {"-bin", "-git", "-appimage", "-nightly", "-beta", "-dev", "-stable", "-wayland", "-qt5", "-qt6", "-gtk", "-electron"};
    for (bool stripped = true; stripped;) {
        stripped = false;
        for (const char* suffix : SUFFIXES) {
            const QLatin1StringView view(suffix);
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
    const auto similar     = std::ranges::find_if(known, [&](const QString& candidate) {
        // foo2 next to foo3, or qt5-x next to qt6-x, are versions and not look-alikes
        return candidate.size() >= 5 && qAbs(candidate.size() - base.size()) <= 1 && withinOneEdit(base, candidate)
            && withoutDigits(candidate) != baseLetters;
    });
    return similar != known.end() ? *similar : QString();
}

QString srcinfoVersion(QStringView srcinfo) {
    QString pkgver;
    QString pkgrel;
    QString epoch;
    for (const auto line : srcinfo.tokenize(u'\n')) {
        // the package base comes first; the packages after it may only repeat its version
        if (line.startsWith("pkgname"_L1)) {
            break;
        }
        const auto separator = line.indexOf(u" = ");
        if (separator < 0) {
            continue;
        }
        const auto key   = line.first(separator).trimmed();
        const auto value = line.sliced(separator + 3).trimmed().toString();
        if (key == "pkgver"_L1) {
            pkgver = value;
        } else if (key == "pkgrel"_L1) {
            pkgrel = value;
        } else if (key == "epoch"_L1) {
            epoch = value;
        }
    }
    if (pkgver.isEmpty() || pkgrel.isEmpty()) {
        return {};
    }
    return (epoch.isEmpty() || epoch == "0"_L1 ? QString() : epoch + u':') + pkgver + u'-' + pkgrel;
}

QList<QByteArrayView> catFileContents(QByteArrayView output) {
    QList<QByteArrayView> contents;
    qsizetype at = 0;
    while (at < output.size()) {
        const auto end = output.indexOf('\n', at);
        if (end < 0) {
            break;
        }
        // "<object> blob <size>" or "<object> missing"
        const auto header = output.sliced(at, end - at);
        at                = end + 1;
        const auto space  = header.lastIndexOf(' ');
        bool ok           = false;
        const auto size   = header.contains(" blob "_ba) && space > 0 ? header.sliced(space + 1).toLongLong(&ok) : -1;
        if (!ok || size < 0 || at + size > output.size()) {
            contents << QByteArrayView();
            continue;
        }
        contents << output.sliced(at, size);
        at += size + 1;  // and the newline after it
    }
    return contents;
}

QString readableDiff(QStringView diff) {
    QStringList lines;
    QString file;           // the file whose name has not been written yet
    QString state;          // what happened to it, if more than a change of its lines
    bool inHeader = false;  // between "diff --git" and its first hunk
    const auto flush = [&](const QString& fallback = {}) {
        if (!file.isEmpty()) {
            const auto note = !state.isEmpty() ? state : fallback;
            lines << (note.isEmpty() ? file : u"%1 (%2)"_s.arg(file, note));
            file.clear();
        }
    };
    for (const auto line : diff.tokenize(u'\n')) {
        if (line.startsWith("diff --git "_L1)) {
            flush(u"mode changed"_s);
            // "diff --git a/<name> b/<name>", without renames both names are the same
            const auto at = line.lastIndexOf(" b/"_L1);
            file          = (at < 0 ? line.mid(11) : line.mid(at + 3)).toString();
            state.clear();
            inHeader = true;
        } else if (inHeader && line.startsWith("new file mode"_L1)) {
            state = u"new file"_s;
        } else if (inHeader && line.startsWith("deleted file mode"_L1)) {
            state = u"removed"_s;
        } else if (inHeader && line.startsWith("Binary files"_L1)) {
            flush();
            lines << u"\\ binary file, not shown"_s;
            inHeader = false;
        } else if (inHeader && !line.startsWith("@@"_L1)) {
            continue;  // index, ---, +++ and mode lines
        } else {
            flush();
            inHeader = false;
            lines << line.toString();
        }
    }
    flush(u"mode changed"_s);
    return lines.join(u'\n');
}

std::optional<QString> readText(const QString& path, qint64 maxSize) {
    const QFileInfo info(path);
    QFile file(path);
    if (info.isSymLink() || !info.isFile() || info.size() > maxSize || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return QString::fromUtf8(file.readAll());
}

LocalFiles collectFiles(const QDir& dir, Analysis& analysis, qsizetype maxFiles, qint64 maxSize) {
    // a source without a telling name is still a script when it starts with "#!"
    for (const auto& name : std::as_const(analysis.otherLocalFiles)) {
        const QFileInfo info(dir, name);
        if (QFile file(info.filePath()); !info.isSymLink() && info.isFile() && file.open(QIODevice::ReadOnly) && file.read(2) == "#!") {
            analysis.localFiles << name;
        }
    }
    LocalFiles out;
    const auto count = std::min(analysis.localFiles.size(), maxFiles);
    out.unreadable   = analysis.localFiles.sliced(count);
    for (const auto& name : analysis.localFiles.first(count)) {
        if (auto text = readText(dir.filePath(name), maxSize)) {
            out.files << ScriptFile{name, *std::move(text)};
        } else {
            out.unreadable << name;
        }
    }
    return out;
}

bool validPkgbase(QStringView name) {
    return plainName(name, false);
}

void sort(QList<Finding>& findings) {
    // metadata first, then the PKGBUILD, then the other files
    std::ranges::stable_sort(findings, std::less{}, [](const Finding& f) {
        const int rank = f.file.isEmpty() ? 0 : f.file == PKGBUILD ? 1 : 2;
        return std::tuple<int, int, const QString&, int>(-std::to_underlying(f.severity), rank, f.file, f.line);
    });
}

QString severityName(Severity severity) {
    static constexpr std::array NAMES = {QStringView(u"info"), QStringView(u"warning"), QStringView(u"danger")};
    return NAMES.at(static_cast<std::size_t>(severity)).toString();
}

QJsonObject toJson(const Finding& finding) {
    return {
        {u"id"_s, finding.id},
        {u"severity"_s, severityName(finding.severity)},
        {u"file"_s, finding.file},
        {u"line"_s, finding.line},
        {u"title"_s, finding.title},
        {u"detail"_s, finding.detail},
        {u"excerpt"_s, finding.excerpt},
    };
}

}  // namespace pkgscan
