// SPDX-License-Identifier: GPL-3.0-or-later
//
// cachyos-pi-scan: prints what a reviewer should look at in an AUR package.
// Reads files only, nothing from the package is executed.
#include "pkgscan.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {

constexpr qint64 MAX_FILE_SIZE = 1024 * 1024;

bool readFile(const QString& path, QString* text) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MAX_FILE_SIZE) {
        return false;
    }
    *text = QString::fromUtf8(file.readAll());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cachyos-pi-scan"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Lists the parts of an AUR package that deserve a closer look.\n"
        "This is a reading aid: no findings does not mean the package is safe.\n\n"
        "Exit status: 0 nothing or notes only, 1 warnings, 2 dangerous patterns, 3 could not read the package."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("json"), QStringLiteral("Print the result as JSON.")});
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("A PKGBUILD or the directory that contains it."), QStringLiteral("[path]"));
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const auto args = parser.positionalArguments();
    QFileInfo target(args.isEmpty() ? QStringLiteral(".") : args.first());
    if (target.isDir()) {
        target = QFileInfo(QDir(target.absoluteFilePath()), QStringLiteral("PKGBUILD"));
    }
    QString pkgbuild;
    if (!readFile(target.absoluteFilePath(), &pkgbuild)) {
        err << "cachyos-pi-scan: cannot read " << target.filePath() << '\n';
        return 3;
    }

    auto analysis = pkgscan::scanPkgbuild(pkgbuild);
    QStringList missing;
    for (const auto& name : std::as_const(analysis.localFiles)) {
        QString text;
        if (readFile(target.absoluteDir().filePath(name), &text)) {
            analysis.findings += pkgscan::scanScript(text, name);
        } else {
            missing << name;
        }
    }
    pkgscan::sort(analysis.findings);

    auto worst = pkgscan::Severity::Info;
    for (const auto& finding : std::as_const(analysis.findings)) {
        worst = std::max(worst, finding.severity);
    }

    if (parser.isSet(QStringLiteral("json"))) {
        QJsonArray findings;
        for (const auto& finding : std::as_const(analysis.findings)) {
            findings << QJsonObject{
                {QStringLiteral("id"), finding.id},
                {QStringLiteral("severity"), pkgscan::severityName(finding.severity)},
                {QStringLiteral("file"), finding.file},
                {QStringLiteral("line"), finding.line},
                {QStringLiteral("title"), finding.title},
                {QStringLiteral("detail"), finding.detail},
                {QStringLiteral("excerpt"), finding.excerpt},
            };
        }
        const QJsonObject root{
            {QStringLiteral("findings"), findings},
            {QStringLiteral("hosts"), QJsonArray::fromStringList(analysis.hosts)},
            {QStringLiteral("files"), QJsonArray::fromStringList(analysis.localFiles)},
            {QStringLiteral("unreadable"), QJsonArray::fromStringList(missing)},
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
            out << "Downloads from: " << analysis.hosts.join(QStringLiteral(", ")) << '\n';
        }
        for (const auto& name : std::as_const(missing)) {
            out << "Not checked (file missing): " << name << '\n';
        }
        if (analysis.findings.isEmpty()) {
            out << "Nothing flagged. That does not make the package safe: read the PKGBUILD.\n";
        }
    }

    return worst == pkgscan::Severity::Danger ? 2 : worst == pkgscan::Severity::Warning ? 1 : 0;
}
