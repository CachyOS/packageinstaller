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
    QStringList files = dir.entryList(QStringList() << filter, QDir::Files, QDir::Unsorted);
    foreach (const QString &file_name, files) {
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
    QString package;
    QString version;
    QString description;
    QStringList list = files_content.split("\n");

    foreach(QString line, list) {
        if (line.startsWith("Package: ")) {
            package = line.remove("Package: ");
        } else if (line.startsWith("Version: ")) {
            version =line.remove("Version: ");
        } else if (line.startsWith("Description: ")) {
            description = line.remove("Description: ");
        }
        candidates.insert(package, QStringList() << version << description);
    }
}

bool AptCache::readFile(const QString &file_name)
{
    QFile file(dir_name + file_name);
    if(!file.open(QFile::ReadOnly)) {
        qDebug() << "Count not open file: " << file.fileName();
        return false;
    }
    files_content += file.readAll();
    file.close();
    return true;
}
