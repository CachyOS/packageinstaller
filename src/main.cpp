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
// Copyright (C) 2022-2025 Vladislav Nepogodin
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

#include "alpm_manager.hpp"
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
#include <QStandardPaths>
#include <QTranslator>

#include <spdlog/async.h>                  // for create_async
#include <spdlog/common.h>                 // for debug
#include <spdlog/sinks/basic_file_sink.h>  // for basic_file_sink_mt
#include <spdlog/spdlog.h>                 // for set_default_logger, set_level

namespace fs = std::filesystem;

namespace {

bool IsInstanceAlreadyRunning(QSharedMemory& memoryLock) noexcept {
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
void initTranslations(QTranslator& qtTranslatorBase, QTranslator& qtTranslator, QTranslator& translatorBase, QTranslator& translator) noexcept {
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    const auto lang_territory = QLocale::system().name();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    const auto translation_path{QLibraryInfo::location(QLibraryInfo::TranslationsPath)};
#else
    const auto translation_path{QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
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

}  // namespace

auto main(int argc, char** argv) -> std::int32_t {
    /// 1. Basic Qt initialization (not dependent on parameters or configuration)
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    // Generate high-dpi pixmaps
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    /// 2. Application identification
    QApplication::setOrganizationName("cachyos");
    QApplication::setOrganizationDomain("cachyos.org");
    QApplication::setApplicationName("cachyos-pi");

    // Set application attributes
    const QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/icons/cachyos-pi.png"));

    /// 3. Initialization of translations
    QTranslator qtTranslatorBase;
    QTranslator qtTranslator;
    QTranslator translatorBase;
    QTranslator translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);

    QSharedMemory sharedMemoryLock("CachyOS-PI-lock");
    if (IsInstanceAlreadyRunning(sharedMemoryLock)) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("Instance of the program is already running! Please close it first"));
        return EXIT_FAILURE;
    }

    const auto& cache_path   = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const auto& log_filepath = fmt::format("{}/cachyospi.log", cache_path.toStdString());
    if (fs::exists(log_filepath)) {
        std::ifstream currentfile{log_filepath};
        const std::string file_data{std::istreambuf_iterator<char>(currentfile), std::istreambuf_iterator<char>()};
        std::ofstream oldlogfile{fmt::format("{}.old", log_filepath)};
        oldlogfile << "-----------------------------------------------------------\nCACHYOSPI SESSION\n"
                      "-----------------------------------------------------------\n";
        oldlogfile << file_data;
        fs::remove(log_filepath);
    }
    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", log_filepath);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

    // Check if we have valid databases
    {
        if (!alpm::is_valid_alpm_dbs()) {
            QMessageBox::critical(nullptr, QObject::tr("Error"),
                QObject::tr("No db found!\nPlease run `pacman -Sy` to update DB!\nThis is needed for the app to work properly"));
            return EXIT_FAILURE;
        }
    }

    // Root guard
    if (system("logname |grep -q ^root$") == 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("You seem to be logged in as root, please log out and log in as normal user to use this program."));
        return EXIT_FAILURE;
    }

    if (getuid() == 0) {
        QApplication::beep();
        QMessageBox::critical(nullptr, QObject::tr("Unable to run the app"),
            QObject::tr("Please don't run that application as root user!"));
        return EXIT_FAILURE;
    }

    // Don't start app if pacman is running
    static constexpr auto lock_path = "/var/lib/pacman/db.lck";
    if (fs::exists(lock_path)) {
        QApplication::beep();
        QMessageBox::critical(nullptr, QObject::tr("Unable to get exclusive lock"),
            QObject::tr("Another package management application (like pamac or pacman), "
                        "is already running. Please close that application first"));
        return EXIT_FAILURE;
    }

    MainWindow w;
    w.show();
    const auto& status_code = QApplication::exec();

    spdlog::shutdown();
    return status_code;
}
