/*
    Copyright (C) 2008  Tim Fechtner < urwald at users dot sourceforge dot net >
    Modfied by Adrian @MXLinux

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of
    the License or (at your option) version 3 or any later version
    accepted by the membership of KDE e.V. (or its successor approved
    by the membership of KDE e.V.), which shall act as a proxy
    defined in Section 14 of version 3 of the license.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "versionnumber.h"

VersionNumber::VersionNumber()
{
}


VersionNumber::VersionNumber(const QString& value)
{
    setStrings(value);
}

VersionNumber::VersionNumber(const VersionNumber& value)
{
    str = value.str;
    epoch = value.epoch;
    upstream_version = value.upstream_version;
    debian_revision = value.debian_revision;
}

VersionNumber::~VersionNumber()
{
}


QString VersionNumber::toString() const
{
    return str;
}


void VersionNumber::setStrings(const QString& value)
{
    str = value;
    QString upstream_str, debian_str;

    if (value.contains(QLatin1Char(':'))) {
        epoch = value.section(QLatin1Char(':'), 0, 0).toInt();
        upstream_str = value.section(QLatin1Char(':'), 1);
    } else {
        epoch = 0;
        upstream_str = value;
    }
    if (upstream_str.contains(QLatin1Char('-'))) {
        debian_str = upstream_str.section(QLatin1Char('-'), -1);
        upstream_str = upstream_str.remove(QLatin1Char('-') + debian_str);
    }

    upstream_version = groupDigits(upstream_str);
    debian_revision = groupDigits(debian_str);
}

VersionNumber VersionNumber::operator=(const VersionNumber& value)
{
    str = value.str;
    epoch = value.epoch;
    upstream_version = value.upstream_version;
    debian_revision = value.debian_revision;
    return *this;
}

VersionNumber VersionNumber::operator=(const QString& value)
{
    setStrings(value);
    return *this;
}

bool VersionNumber::operator<(const VersionNumber& value) const
{
    return (compare(*this, value) == 1);
}

bool VersionNumber::operator<=(const VersionNumber& value) const
{
    return !(*this > value);
}

bool VersionNumber::operator>(const VersionNumber& value) const
{
    return (compare(*this, value) == -1);
}

bool VersionNumber::operator>=(const VersionNumber& value) const
{
    return !(*this < value);
}

bool VersionNumber::operator==(const VersionNumber& value) const
{
    return (this->str == value.str);
}

bool VersionNumber::operator!=(const VersionNumber& value) const
{
    return !(*this == value);
}

// transform QString into QStringList with digits grouped together
QStringList VersionNumber::groupDigits(QString value)
{
    QStringList result = QStringList();
    QString cache = "";

    for (int i = 0; i < value.length(); ++i) {
        if (value.at(i).isDigit()) {
            cache.append(value.at(i));
            if (value.length() - 1 == i)
                result.append(cache);
        } else {
            if (!cache.isEmpty()) { // add accumulated digits
                result.append(cache);
                cache.clear();
            }
            result.append(value.at(i));
        }
    }

    return result;
}

// return 1 if second > first, -1 if second < first, 0 if equal
int VersionNumber::compare(const VersionNumber& first, const VersionNumber& second) const
{
    if (second.epoch > first.epoch)
        return 1;
    else if (second.epoch < first.epoch)
        return -1;
    int res = compare(first.upstream_version, second.upstream_version);
    if (res == 1)
        return 1;
    else if (res == -1)
        return -1;
    else if (!debian_revision.isEmpty())
        return compare(first.debian_revision, second.debian_revision);
    return 0;
}

// return 1 if second > first, -1 if second < first, 0 if equal
int VersionNumber::compare(const QStringList &first, const QStringList &second) const
{
    for (int i = 0; i < first.length() && i < second.length(); ++i) {
        // check if equal
        if (first.at(i) == second.at(i))
            continue; // continue till it finds difference

        // ~ sorts lowest
        if (first.at(i).at(0) == '~' && second.at(i).at(0) != '~')
            return 1;
        else if (second.at(i).at(0) == '~' && first.at(i).at(0) != '~')
            return -1;

        // if one char length check which one is larger
        if (first.at(i).length() == 1 && second.at(i).length() == 1) {
            int res = compare(first.at(i).at(0), second.at(i).at(0));
            if (res == 0)
                continue;
            else
                return res;
            // one char (not-number) vs. multiple (digits)
        } else if (first.at(i).length() > 1 && second.at(i).length() == 1 && !second.at(i).at(0).isDigit()) {
            return 1;
        } else if (first.at(i).length() == 1 && !first.at(i).at(0).isDigit() && second.at(i).length() > 1) {
            return -1;
        }

        // compare remaining digits
        if (second.at(i).toInt() > first.at(i).toInt())
            return 1;
        else
            return -1;
    }

    // if equal till the end of one of the lists, compare list size
    // if the larger list doesn't have "~" it's the bigger version
    if (second.length() > first.length()) {
        if (second.at(first.length()) != "~")
            return 1;
        else
            return -1;
    } else if (second.length() < first.length()) {
        if (first.at(second.length()) != "~")
            return -1;
        else
            return 1;
    }
    return 0;
}

// return 1 if second > first, -1 if second < first, 0 if equal
// letters and number sort before special chars
int VersionNumber::compare(const QChar& first, const QChar& second) const
{
    if (first == second)
        return 0;

    // sort letters and numbers before special char
    if (first.isLetterOrNumber() && !second.isLetterOrNumber())
        return 1;
    else if (!first.isLetterOrNumber() && second.isLetterOrNumber())
        return -1;

    if (first < second)
        return 1;
    else if (first > second)
        return -1;
    return 0;
}
