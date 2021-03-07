#ifndef APTCACHE_H
#define APTCACHE_H

#include <QMap>
#include <QFile>

#include "versionnumber.h"

class AptCache
{
public:
    AptCache();

    void loadCacheFiles();
    const QMap<QString, QStringList> getCandidates();
    static const QString getArch();

private:
    QMap<QString, QStringList> candidates;
    QString dir_name;
    QString files_content;

    void parseContent();
    bool readFile(const QString &file_name);

};

#endif // APTCACHE_H
