// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapcli.hpp"

#include <QStringView>

using namespace Qt::StringLiterals;

namespace snapcli {

namespace {

    /// The columns of one row, split on runs of whitespace.
    QList<QString> columns(QStringView line) {
        QList<QString> out;
        qsizetype at = 0;
        while (at < line.size()) {
            while (at < line.size() && line[at].isSpace()) {
                ++at;
            }
            if (at == line.size()) {
                break;
            }
            const auto start = at;
            while (at < line.size() && !line[at].isSpace()) {
                ++at;
            }
            out << line.sliced(start, at - start).toString();
        }
        return out;
    }

    /// Headers of every snap table begin with these two columns.
    bool isHeader(const QList<QString>& c) {
        return c.size() >= 2 && c[0] == u"Name"_s && c[1] == u"Version"_s;
    }

}  // namespace

QList<SnapApp> parseFind(const QList<QString>& lines) {
    QList<SnapApp> apps;
    for (const auto& line : lines) {
        const auto c = columns(line);
        if (isHeader(c) || c.size() < 5) {
            continue;
        }
        QString summary;
        for (qsizetype i = 4; i < c.size(); ++i) {
            if (!summary.isEmpty()) {
                summary += u' ';
            }
            summary += c[i];
        }
        apps << SnapApp{.name = c[0], .version = c[1], .publisher = c[2], .notes = c[3], .summary = summary};
    }
    return apps;
}

QList<SnapInstalled> parseList(const QList<QString>& lines) {
    QList<SnapInstalled> snaps;
    for (const auto& line : lines) {
        const auto c = columns(line);
        if (isHeader(c) || c.size() < 6) {
            continue;
        }
        snaps << SnapInstalled{.name = c[0], .version = c[1], .revision = c[2], .channel = c[3], .publisher = c[4], .notes = c[5]};
    }
    return snaps;
}

QList<SnapUpdate> parseUpdates(const QList<QString>& lines) {
    QList<SnapUpdate> snaps;
    for (const auto& line : lines) {
        if (line.trimmed().startsWith(u"All snaps up to date"_s)) {
            continue;
        }
        const auto c = columns(line);
        if (isHeader(c) || c.size() < 5) {
            continue;
        }
        // Size is optional, so publisher and notes are anchored from the right.
        snaps << SnapUpdate{.name = c[0], .version = c[1], .revision = c[2], .publisher = c[c.size() - 2], .notes = c.last()};
    }
    return snaps;
}

}  // namespace snapcli
