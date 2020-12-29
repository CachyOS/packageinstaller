#include <QDir>
#include <QDebug>

#include "aptcache.h"

AptCache::AptCache()
{
    dir_name = "/var/lib/apt/lists/";
    loadCacheFiles();
}

void AptCache::loadCacheFiles()
{
    QDir dir(dir_name);
    QString filter = "*binary-" + getArch() + "_Packages";
    const QStringList files = dir.entryList(QStringList() << filter, QDir::Files, QDir::Unsorted);
    for (const QString &file_name : files) {
        if(!readFile(file_name)) {
            qDebug() << "error reading a cache file";
        }
    }
    parseContent();
}

const QMap<QString, QStringList> AptCache::getCandidates()
{
    return candidates;
}

QString AptCache::getArch()
{
    return (system("arch | grep -q x86_64") == 0) ? "amd64" : "i386";
}

void AptCache::parseContent()
{
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;
    const QStringList list = files_content.split("\n");

    for (QString line : list) {
        if (line.startsWith(QStringLiteral("Package: "))) {
            package_list << line.remove(QStringLiteral("Package: "));
        } else if (line.startsWith(QStringLiteral("Version: "))) {
            version_list << line.remove(QStringLiteral("Version: "));
        } else if (line.startsWith(QStringLiteral("Description:"))) { // not "Description: " because some people don't add description to their packages
            description_list << line.remove(QStringLiteral("Description:")).trimmed();
        }
    }
    for (int i = 0; i < package_list.size(); ++i) {
        if (candidates.contains(package_list.at(i)) && (VersionNumber(version_list.at(i)) <= VersionNumber(candidates.value(package_list.at(i)).at(0)))) {
            continue;
        }
        candidates.insert(package_list.at(i), QStringList() << version_list.at(i) << description_list.at(i));
    }
}

bool AptCache::readFile(const QString &file_name)
{
    QFile file(dir_name + file_name);
    if(!file.open(QFile::ReadOnly)) {
        qDebug() << "Could not open file: " << file.fileName();
        return false;
    }
    files_content += file.readAll();
    file.close();
    return true;
}
