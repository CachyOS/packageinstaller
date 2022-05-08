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
// Copyright (C) 2022 Vladislav Nepogodin
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

#include "config.hpp"
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
#include <QTranslator>

#include <spdlog/async.h>                  // for create_async
#include <spdlog/common.h>                 // for debug
#include <spdlog/sinks/basic_file_sink.h>  // for basic_file_sink_mt
#include <spdlog/spdlog.h>                 // for set_default_logger, set_level

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/icons/cachyos-pi.png"));
    QApplication::setOrganizationName("CachyOS");

    QTranslator qtTran;
    if (qtTran.load(QLocale::system(), "qt", "_", QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTran);

    QTranslator qtBaseTran;
    if (qtBaseTran.load("qtbase_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtBaseTran);

    QTranslator appTran;
    if (appTran.load(QApplication::applicationName() + "_" + QLocale::system().name(), "/usr/share/" + app.applicationName() + "/locale"))
        QApplication::installTranslator(&appTran);

    // Root guard
    if (system("logname |grep -q ^root$") == 0) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QObject::tr("You seem to be logged in as root, please log out and log in as normal user to use this program."));
        return EXIT_FAILURE;
    }

    bool setup_mode{};
    for (int i = 0; i < argc; ++i) {
        const char* tmp = argv[i];
        if (!tmp) {
            break;
        }

        std::string_view arg{tmp};
        if (arg == "--setupmode") {
            setup_mode = true;
            break;
        }
    }

    if (!Config::initialize()) {
        return EXIT_FAILURE;
    }

    Config::instance()->data()["setupmode"] = setup_mode;

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
