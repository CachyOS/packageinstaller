// SPDX-License-Identifier: GPL-3.0-or-later
//
// cachyos-pi-scan: prints what a reviewer should look at in an AUR package.
// Reads files only, nothing from the package is executed.
#include "pkgscan.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <backward.hpp>

using namespace Qt::StringLiterals;

namespace {

constexpr int EXIT_UNREADABLE = 3;

}  // namespace

int main(int argc, char** argv) {
    const backward::SignalHandling signalHandling;
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"cachyos-pi-scan"_s);
    QCoreApplication::setApplicationVersion(u"" PI_VERSION ""_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"Lists the parts of an AUR package that deserve a closer look.\n"
        "This is a reading aid: no findings does not mean the package is safe.\n\n"
        "Exit status: 0 nothing or notes only, 1 warnings, 2 dangerous patterns, 3 could not read the package."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption json(u"json"_s, u"Print the result as JSON."_s);
    const QCommandLineOption srcinfoOption(u"srcinfo"_s, u"The .SRCINFO to read sources from (default: .SRCINFO next to the PKGBUILD)."_s, u"file"_s);
    parser.addOptions({json, srcinfoOption});
    parser.addPositionalArgument(u"path"_s, u"A PKGBUILD or the directory that contains it."_s, u"[path]"_s);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const auto args = parser.positionalArguments();
    QFileInfo target(args.isEmpty() ? u"."_s : args.first());
    if (target.isDir()) {
        target = QFileInfo(QDir(target.absoluteFilePath()), u"PKGBUILD"_s);
    }
    const auto pkgbuild = pkgscan::readText(target.absoluteFilePath());
    const auto srcinfoPath = parser.isSet(srcinfoOption) ? parser.value(srcinfoOption) : target.absoluteDir().filePath(u".SRCINFO"_s);
    const auto srcinfo  = pkgscan::readText(srcinfoPath);
    if (!pkgbuild || (!srcinfo && parser.isSet(srcinfoOption))) {
        err << "cachyos-pi-scan: cannot read " << (pkgbuild ? srcinfoPath : target.filePath()) << '\n';
        return EXIT_UNREADABLE;
    }

    auto analysis    = pkgscan::scanPkgbuild(*pkgbuild, srcinfo.value_or(QString()));
    const auto files = pkgscan::collectFiles(target.absoluteDir(), analysis);
    for (const auto& [name, text] : files.files) {
        analysis.findings += pkgscan::scanScript(text, name);
    }
    pkgscan::sort(analysis.findings);

    if (parser.isSet(json)) {
        QJsonArray findings;
        for (const auto& finding : std::as_const(analysis.findings)) {
            findings << pkgscan::toJson(finding);
        }
        const QJsonObject root{
            {u"findings"_s, findings},
            {u"hosts"_s, QJsonArray::fromStringList(analysis.hosts)},
            {u"files"_s, QJsonArray::fromStringList(analysis.localFiles)},
            {u"unreadable"_s, QJsonArray::fromStringList(files.unreadable)},
        };
        out << QJsonDocument(root).toJson(QJsonDocument::Indented);
    } else {
        for (const auto& finding : std::as_const(analysis.findings)) {
            out << '[' << pkgscan::severityName(finding.severity).toUpper() << "] " << finding.title << '\n';
            if (finding.line > 0) {
                out << "    " << finding.file << ':' << finding.line << "  " << finding.excerpt << '\n';
            }
            out << "    " << finding.detail << '\n';
        }
        if (!analysis.hosts.isEmpty()) {
            out << "Downloads from: " << analysis.hosts.join(u", "_s) << '\n';
        }
        for (const auto& name : std::as_const(files.unreadable)) {
            out << "Not checked (file missing): " << name << '\n';
        }
        if (analysis.findings.isEmpty()) {
            out << "Nothing flagged. That does not make the package safe: read the PKGBUILD.\n";
        }
    }

    // findings are sorted most severe first, and the severities are the exit codes 0 to 2
    return analysis.findings.isEmpty() ? 0 : static_cast<int>(analysis.findings.first().severity);
}
