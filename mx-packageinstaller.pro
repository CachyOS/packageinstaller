# **********************************************************************
# * Copyright (C) 2017 MX Authors
# *
# * Authors: Adrian
# *          Dolphin_Oracle
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-packageinstaller.
# *
# * mx-packageinstaller is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * mx-packageinstaller is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

QT       += core gui xml network
CONFIG   += c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = mx-packageinstaller
TEMPLATE = app


SOURCES += main.cpp\
    mainwindow.cpp \
    lockfile.cpp \
    versionnumber.cpp \
    aptcache.cpp \
    remotes.cpp \
    about.cpp \
    cmd.cpp

HEADERS  += \
    mainwindow.h \
    lockfile.h \
    versionnumber.h \
    aptcache.h \
    remotes.h \
    version.h \
    about.h \
    cmd.h

FORMS    += \
    mainwindow.ui

TRANSLATIONS += translations/mx-packageinstaller_am.ts \
                translations/mx-packageinstaller_ar.ts \
                translations/mx-packageinstaller_bg.ts \
                translations/mx-packageinstaller_ca.ts \
                translations/mx-packageinstaller_cs.ts \
                translations/mx-packageinstaller_da.ts \
                translations/mx-packageinstaller_de.ts \
                translations/mx-packageinstaller_el.ts \
                translations/mx-packageinstaller_es.ts \
                translations/mx-packageinstaller_et.ts \
                translations/mx-packageinstaller_eu.ts \
                translations/mx-packageinstaller_fa.ts \
                translations/mx-packageinstaller_fi.ts \
                translations/mx-packageinstaller_fr.ts \
                translations/mx-packageinstaller_he_IL.ts \
                translations/mx-packageinstaller_hi.ts \
                translations/mx-packageinstaller_hr.ts \
                translations/mx-packageinstaller_hu.ts \
                translations/mx-packageinstaller_id.ts \
                translations/mx-packageinstaller_is.ts \
                translations/mx-packageinstaller_it.ts \
                translations/mx-packageinstaller_ja.ts \
                translations/mx-packageinstaller_kk.ts \
                translations/mx-packageinstaller_ko.ts \
                translations/mx-packageinstaller_lt.ts \
                translations/mx-packageinstaller_mk.ts \
                translations/mx-packageinstaller_mr.ts \
                translations/mx-packageinstaller_nb.ts \
                translations/mx-packageinstaller_nl.ts \
                translations/mx-packageinstaller_pl.ts \
                translations/mx-packageinstaller_pt.ts \
                translations/mx-packageinstaller_pt_BR.ts \
                translations/mx-packageinstaller_ro.ts \
                translations/mx-packageinstaller_ru.ts \
                translations/mx-packageinstaller_sk.ts \
                translations/mx-packageinstaller_sl.ts \
                translations/mx-packageinstaller_sq.ts \
                translations/mx-packageinstaller_sr.ts \
                translations/mx-packageinstaller_sv.ts \
                translations/mx-packageinstaller_tr.ts \
                translations/mx-packageinstaller_uk.ts \
                translations/mx-packageinstaller_zh_CN.ts \
                translations/mx-packageinstaller_zh_TW.ts

RESOURCES += \
    images.qrc

