// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "iconprovider.hpp"
#include "localization.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include <backward.hpp>

using namespace Qt::StringLiterals;

int main(int argc, char** argv) {
    // prints a stack trace on a crash, then re-raises the signal
    const backward::SignalHandling crashHandler;

    // QApplication (not QGuiApplication): icon theme lookups go through the platform theme.
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"CachyOS"_s);
    QCoreApplication::setApplicationName(u"cachyos-pi"_s);
    QCoreApplication::setApplicationVersion(QString::fromLatin1(PI_VERSION));
    QGuiApplication::setDesktopFileName(u"cachyos-pi"_s);
    QGuiApplication::setWindowIcon(QIcon::fromTheme(u"cachyos-pi"_s, QIcon(u":/qt/qml/CachyOS/PackageInstaller/icons/cachyos-pi.png"_s)));

    // The chosen language is installed before QML reads its first string.
    Localization localization(nullptr);

    // The UI brings its own look; Basic is the style meant for customisation.
    QQuickStyle::setStyle(u"Basic"_s);

    auto* icons = new IconProvider;  // owned by the engine below
    Backend backend(icons);
    // "cachyos-pi some.AppImage" offers to add that file
    if (const auto arguments = QCoreApplication::arguments(); arguments.size() > 1 && QFileInfo(arguments.at(1)).isFile()) {
        backend.setStartupFile(QUrl::fromLocalFile(QFileInfo(arguments.at(1)).absoluteFilePath()));
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(u"pkgicon"_s, icons);
    QObject::connect(&localization, &Localization::languageChanged, &engine, [&engine] { engine.retranslate(); });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("CachyOS.PackageInstaller", "Main");
    return QApplication::exec();
}
