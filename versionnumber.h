/**********************************************************************
 *  versionnumber.h
 **********************************************************************
 * Copyright (C) 2018 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-packageinstaller.
 *
 * mx-packageinstaller is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-packageinstaller is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#ifndef VERSIONNUMBER_H
#define VERSIONNUMBER_H

#include <QStringList>
#include <QMetaType>

class VersionNumber
{

  public:
    VersionNumber();
    VersionNumber(const VersionNumber& value); // copy constructor
    VersionNumber(const QString& value);
    ~VersionNumber();

    QString toString() const;

    VersionNumber operator=(const VersionNumber& value);
    VersionNumber operator=(const QString& value);

    bool operator<(const VersionNumber& value) const;
    bool operator<=(const VersionNumber& value) const;
    bool operator>(const VersionNumber& value) const;
    bool operator>=(const VersionNumber& value) const;
    bool operator==(const VersionNumber& value) const;
    bool operator!=(const VersionNumber& value) const;

  private:
    QString str;                    // full version string
    int epoch;
    QStringList upstream_version;   // a string list of characters, numbers are grouped together
    QStringList debian_revision;

    QStringList groupDigits(QString value); // add characters to separate elements, groups digits together
    void setStrings(const QString& value);

    int compare(const VersionNumber& first, const VersionNumber& second) const; // 1 for >second, -1 for <second, 0 for equal
    int compare(const QStringList& first, const QStringList& second) const;
    int compare(const QChar& first, const QChar& second) const;

};

Q_DECLARE_METATYPE(VersionNumber)

#endif
