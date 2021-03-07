#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include "aptcache.h"

AptCache::AptCache()
{
    dir_name = "/var/lib/apt/lists/";
    loadCacheFiles();
}

void AptCache::loadCacheFiles()
{
    QDir dir(dir_name);
    // include all _Packages list files
    QString packages_filter = "*_Packages";

    // some regexp's
    // to include those which match architecure in filename
    QRegularExpression re_binary_arch(".*binary-" + getArch() + "_Packages");
    // to include those flat-repos's which do not have 'binary' within the name
    QRegularExpression re_binary_other(".*binary-.*_Packages");
    // to exclude debian backports
    QRegularExpression re_backports(".*debian_.*-backports_.*_Packages");
    // to exclude mx testrepo
    QRegularExpression re_testrepo(".*mx_testrepo.*_test_.*_Packages");
    // to exclude devoloper's mx temp repo
    QRegularExpression re_temprepo(".*mx_repo.*_temp_.*_Packages");

    const QStringList packages_files = dir.entryList(QStringList() << packages_filter, QDir::Files, QDir::Unsorted);
    QStringList files;
    for (const QString &file_name : packages_files)  {
        if (re_backports.match(file_name).hasMatch() or
            re_testrepo.match(file_name).hasMatch()  or
            re_temprepo.match(file_name).hasMatch()) {
            continue;
        }
        if (re_binary_arch.match(file_name).hasMatch()) {
            files << file_name;
            continue;
        }
        if (not re_binary_other.match(file_name).hasMatch()) {
            files << file_name;
            continue;
        }
    }


    for (const QString &file_name : files)
        if(!readFile(file_name))
            qDebug() << "error reading a cache file";
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

    QString package;
    QString version;
    QString description;
    QString architecture;

    QRegularExpression re_arch(".*(" + getArch() + "|all).*");
    bool match_arch  = false;
    bool add_package = false;

    // FIXME: add deb822-format handling
    // assumption for now is made "Description:" line is always the last
    for (QString line : list) {
        if (line.startsWith(QLatin1String("Package: "))) {
            package = line.remove(QLatin1String("Package: "));
        } else if (line.startsWith(QLatin1String("Architecture:"))) {
            architecture = line.remove(QLatin1String("Architecture:")).trimmed();
            match_arch = re_arch.match(architecture).hasMatch();
        } else if (line.startsWith(QLatin1String("Version: "))) {
            version = line.remove(QLatin1String("Version: "));
        } else if (line.startsWith(QLatin1String("Description:"))) { // not "Description: " because some people don't add description to their packages
            description = line.remove(QLatin1String("Description:")).trimmed();
            if (match_arch)
                add_package = true;
        }
        // add only packages with correct architecure
        if (add_package and match_arch) {
            package_list     << package;
            version_list     << version;
            description_list << description;
            package = "";
            version = "";
            description = "";
            architecture = "";
            add_package = false;
            match_arch = false;
        }
    }
    for (int i = 0; i < package_list.size(); ++i) {
        if (candidates.contains(package_list.at(i)) && (VersionNumber(version_list.at(i)) <= VersionNumber(candidates.value(package_list.at(i)).at(0))))
            continue;
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
