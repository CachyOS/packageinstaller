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
    if (system("arch | grep -q x86_64") == 0) {
        return "amd64";
    } else {
        return "i386";
    }
}

void AptCache::parseContent()
{
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;
    const QStringList list = files_content.split("\n");

    for (QString line : list) {
        if (line.startsWith("Package: ")) {
            package_list << line.remove("Package: ");
        } else if (line.startsWith("Version: ")) {
            version_list << line.remove("Version: ");
        } else if (line.startsWith("Description: ")) {
            description_list << line.remove("Description: ");
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
