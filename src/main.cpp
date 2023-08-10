/**********************************************************************
 *  main.cpp
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          Dolphin_Oracle
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
// Copyright (C) 2022-2023 Vladislav Nepogodin
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "alpm_helper.hpp"
#include "lockfile.hpp"
#include "mainwindow.hpp"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSharedMemory>
#include <QTranslator>

#include <spdlog/async.h>                  // for create_async
#include <spdlog/common.h>                 // for debug
#include <spdlog/sinks/basic_file_sink.h>  // for basic_file_sink_mt
#include <spdlog/spdlog.h>                 // for set_default_logger, set_level

namespace fs = std::filesystem;

static bool IsInstanceAlreadyRunning(QSharedMemory& memoryLock) {
    if (!memoryLock.create(1)) {
        memoryLock.attach();
        memoryLock.detach();

        if (!memoryLock.create(1)) {
            return true;
        }
    }

    return false;
}

/* Adopted from bitcoin-qt source code.
 * Licensed under MIT
 */
/** Set up translations */
static void initTranslations(QTranslator& qtTranslatorBase, QTranslator& qtTranslator, QTranslator& translatorBase, QTranslator& translator) {
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = QLocale::system().name();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    const QString translation_path{QLibraryInfo::location(QLibraryInfo::TranslationsPath)};
#else
    const QString translation_path{QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
#endif

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, translation_path)) {
        QApplication::installTranslator(&qtTranslatorBase);
    }

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, translation_path)) {
        QApplication::installTranslator(&qtTranslator);
    }

    // Load e.g. cachyos-kernel-manager_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/")) {
        QApplication::installTranslator(&translatorBase);
    }

    // Load e.g. cachyos-kernel-manager_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/")) {
        QApplication::installTranslator(&translator);
    }
}

int main(int argc, char* argv[]) {
    /// 1. Basic Qt initialization (not dependent on parameters or configuration)
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    // Generate high-dpi pixmaps
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    /// 2. Application identification
    QApplication::setOrganizationName("CachyOS");
    QApplication::setOrganizationDomain("cachyos.org");
    QApplication::setApplicationName("CachyOS-PI");

    // Set application attributes
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/icons/cachyos-pi.png"));

    /// 3. Initialization of translations
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);

    QSharedMemory sharedMemoryLock("CachyOS-PI-lock");
    if (IsInstanceAlreadyRunning(sharedMemoryLock)) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("Instance of the program is already running! Please close it first"));
        return EXIT_FAILURE;
    }

    // Check if we have valid databases
    {
        alpm_errno_t alpm_err{};
        alpm_handle_t* handle = alpm_initialize("/", "/var/lib/pacman/", &alpm_err);
        setup_alpm(handle);
        size_t pkgcache_count{};
        auto* dbs = alpm_get_syncdbs(handle);
        for (alpm_list_t* i = dbs; i != nullptr; i = i->next) {
            auto* db = reinterpret_cast<alpm_db_t*>(i->data);
            pkgcache_count += alpm_list_count(alpm_db_get_pkgcache(db));
        }

        if (pkgcache_count == 0) {
            QMessageBox::critical(nullptr, QObject::tr("Error"),
                QObject::tr("No db found!\nPlease run `pacman -Sy` to update DB!\nThis is needed for the app to work properly"));
            alpm_release(handle);
            return EXIT_FAILURE;
        }
        alpm_release(handle);
    }

    // Root guard
    if (system("logname |grep -q ^root$") == 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("You seem to be logged in as root, please log out and log in as normal user to use this program."));
        return EXIT_FAILURE;
    }

    if (getuid() != 0) {
        QApplication::beep();
        QMessageBox::critical(nullptr, QObject::tr("Unable to run the app"),
            QObject::tr("Please run that application as root user!"));
        return EXIT_FAILURE;
    }

    // Don't start app if pacman is running, lock dpkg otherwise while the program runs
    static constexpr auto lock_path = "/var/lib/pacman/db.lck";
    LockFile lock_file(lock_path);
    if (fs::exists(lock_path)) {
        QApplication::beep();
        QMessageBox::critical(nullptr, QObject::tr("Unable to get exclusive lock"),
            QObject::tr("Another package management application (like pamac or pacman), "
                        "is already running. Please close that application first"));
        return EXIT_FAILURE;
    }
    lock_file.lock();

    static constexpr auto log_name = "/var/log/cachyospi.log";
    if (fs::exists(log_name)) {
        std::ifstream currentfile{log_name};
        std::string file_data{std::istreambuf_iterator<char>(currentfile), std::istreambuf_iterator<char>()};
        std::ofstream oldlogfile{fmt::format("{}.old", log_name)};
        oldlogfile << "-----------------------------------------------------------\nCACHYOSPI SESSION\n"
                      "-----------------------------------------------------------\n";
        oldlogfile << file_data;
        fs::remove(log_name);
    }
    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", log_name);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

    MainWindow w;
    w.show();
    const auto& status_code = QApplication::exec();

    spdlog::shutdown();
    return status_code;
}
