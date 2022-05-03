QT       += core widgets gui
CONFIG   += c++2a
DEFINES += SPDLOG_FMT_EXTERNAL

TARGET = cachyos-pi
TEMPLATE = app

LIBS += -lfmt -lalpm

SOURCES += \
    src/main.cpp \
    src/config.cpp \
    src/mainwindow.cpp \
    src/lockfile.cpp \
    src/pacmancache.cpp \
    src/remotes.cpp \
    src/about.cpp \
    src/cmd.cpp

HEADERS  += \
    src/mainwindow.hpp \
    src/config.hpp \
    src/lockfile.hpp \
    src/versionnumber.hpp \
    src/pacmancache.hpp \
    src/remotes.hpp \
    src/version.hpp \
    src/about.hpp \
    src/cmd.hpp

FORMS    += \
    src/mainwindow.ui

TRANSLATIONS += \
                lang/cachyos-packageinstaller_ru.ts

RESOURCES += \
    images.qrc

