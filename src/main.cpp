// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "iconprovider.hpp"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>

int main(int argc, char** argv) {
    // QApplication (not QGuiApplication): icon theme lookups go through the platform theme.
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CachyOS"));
    QCoreApplication::setApplicationName(QStringLiteral("cachyos-pi"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PI_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("cachyos-pi"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("cachyos-pi"),
        QIcon(QStringLiteral(":/qt/qml/CachyOS/PackageInstaller/icons/cachyos-pi.png"))));

    // Without a translation for the locale, still load the English plural forms.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("cachyos-pi"), QStringLiteral("_"), QStringLiteral(":/i18n"))
        || translator.load(QStringLiteral(":/i18n/cachyos-pi_en.qm"))) {
        QCoreApplication::installTranslator(&translator);
    }

    // The UI brings its own look; Basic is the style meant for customisation.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    auto* icons = new IconProvider;  // owned by the engine below
    Backend backend(icons);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("pkgicon"), icons);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("CachyOS.PackageInstaller", "Main");
    return QApplication::exec();
}
