// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

/// Parsing of the plain-text tables printed by the snap command. Text handling only.
namespace snapcli {

struct SnapApp {
    QString name;
    QString version;
    QString publisher;
    QString notes;
    QString summary;
};

struct SnapInstalled {
    QString name;
    QString version;
    QString revision;
    QString channel;
    QString publisher;
    QString notes;
};

struct SnapUpdate {
    QString name;
    QString version;
    QString revision;
    QString publisher;
    QString notes;
};

[[nodiscard]] QList<SnapInstalled> parseList(const QList<QString>& lines);
[[nodiscard]] QList<SnapUpdate> parseUpdates(const QList<QString>& lines);

}  // namespace snapcli
