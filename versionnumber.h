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


#ifndef VERSIONNUMBER_H
#define VERSIONNUMBER_H

#include <QStringList>
#include <QMetaType>

/** \brief A data type for version numbers.
  *
  * This class provides a data type for version numbers. Think of it
  * as a \e QString which provides special behavior for version
  * numbers in the six relational operators (\<, \<=, \>, \>=, ==, !=).
  *
  * The behavior of the relational operators is similar to the behavior
  * of RPM when comparing versions. "Similar" means that it is \e not
  * \e equal! See http://rpm.org/wiki/PackagerDocs/Dependencies for a
  * good description of the algorithm used by RPM to determinate
  * version ordering.
  *
  * You can assign values of the type \e QString and even \e qint64
  * (which will be converted to a QString) and of course of the
  * type \e %VersionNumber itself
  * to it. You can use the assignment operator or the constructor
  * for initiation. The data type is made available to QMetaType and
  * is this way available in for example QVariant. If you want to use
  * it in in non-template based functions like \e queued signal
  * and slot connections, do something like
  * <tt>int id = qRegisterMetaType<VersionNumber>("VersionNumber");</tt>
  * at the begin of your main function.
  * This will register the type also for this use case. <tt>id</tt>
  * will contain the type identifier used by QMetaObject. However,
  * for most cases <tt>id</tt> isn't intresting.
  *
  * You can convert to a string with toString(). This function returns
  * always exactly the string which was used to initialize this object.
  *
  * To compare version numbers, the QString is segmented into small
  * parts. See http://rpm.org/wiki/PackagerDocs/Dependencies for details.
  * The algorithm of \e %VersionNumber differs in some points from the
  * algorithm of RPM:
  * \li It accepts \e all strings, also with special characters.
  * \li You can use not only "." but also "-" as often as you want. (Each
  *     new dash separates a new part in the number.)
  * \li You can safely use special characters. If they aren't "." or "-",
  *     then they are treated as normal characters. The very last
  *     segmentation (e.g. "12#rc1" to "12", "#", "rc", "1") does not only
  *     differ between QChar::isDigit() and QChar::isLetter (like RPM does), but has
  *     a third category for characters who are neither digit nor letter.
  * \li The very first occurrence of ":" is treated as separator for the epoch.
  *     Each following occurrence of ":" is treated as normal character.
  * */

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
