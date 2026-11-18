// SPDX-License-Identifier: GPL-3.0-or-later
#include "localization.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QQmlEngine>
#include <QSettings>

#include <type_traits>

using namespace Qt::StringLiterals;

static_assert(!std::is_default_constructible_v<Localization>,
    "QML would create another instance instead of using create()");

namespace {
bool usable(const QLocale& locale) {
    return locale.language() != QLocale::C && locale.language() != QLocale::AnyLanguage;
}
}  // namespace

Localization* Localization::s_instance = nullptr;

Localization::Localization(QObject* parent) : QObject(parent) {
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;

    const auto saved = QSettings().value(u"language/language"_s, u"system"_s).toString();
    m_language       = supported(saved) ? saved : u"system"_s;

    // The English catalogue holds the plural forms ("1 package" / "2 packages") for every language.
    if (m_plural.load(u":/i18n/cachyos-pi_en.qm"_s)) {
        QCoreApplication::installTranslator(&m_plural);
    }

    // Install the translation before the QML engine reads its first string.
    integrate();
}

Localization* Localization::create(QQmlEngine* /*qmlEngine*/, QJSEngine* /*jsEngine*/) {
    Q_ASSERT(s_instance != nullptr);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

bool Localization::supported(const QString& language) {
    return language == u"system"_s || QFileInfo::exists(u":/i18n/cachyos-pi_%1.qm"_s.arg(language));
}

QString Localization::codeForLocale(const QLocale& locale) {
    if (locale.language() != QLocale::Chinese) {
        return u"en"_s;
    }
    // Hong Kong and Macao also read Traditional Chinese, for which zh_TW is the translation we have.
    return locale.script() == QLocale::TraditionalChineseScript ? u"zh_TW"_s : u"zh_CN"_s;
}

QString Localization::systemCode() {
    // Qt resolves LANGUAGE, LC_ALL, LC_MESSAGES and LANG into the preferred UI languages.
    for (const auto& name : QLocale::system().uiLanguages()) {
        const QLocale locale(name);
        // LC_ALL=C hides the session language behind a "C" entry; keep looking instead.
        if (!usable(locale)) {
            continue;
        }
        if (const auto code = codeForLocale(locale); supported(code)) {
            return code;
        }
    }

    // The same hiding happens when the application is started from a service or a bare shell.
    if (const auto language = qEnvironmentVariable("LANG"); !language.isEmpty()) {
        const QLocale locale(language);
        if (usable(locale)) {
            if (const auto code = codeForLocale(locale); supported(code)) {
                return code;
            }
        }
    }
    return u"en"_s;
}

void Localization::setLanguage(const QString& language) {
    if (!supported(language) || language == m_language) {
        return;
    }

    m_language = language;
    QSettings().setValue(u"language/language"_s, m_language);
    integrate();
    emit languageChanged();
}

void Localization::integrate() {
    const auto code = m_language == u"system"_s ? systemCode() : m_language;
    const QLocale locale(code);
    QLocale::setDefault(locale);

    QCoreApplication::removeTranslator(&m_app);
    QCoreApplication::removeTranslator(&m_qt);

    // Same catalogues initTranslations() installs, reloaded for the chosen language.
    if (!m_app.load(QLocale(), u"cachyos-pi"_s, u"_"_s, u":/i18n"_s)) {
        qWarning() << "No translation could be loaded for" << code;
    }
    // Qt's own strings, such as the file dialog and the line edit context menu. Not every
    // language ships a catalogue, in which case Qt's built-in English strings stay.
    static_cast<void>(m_qt.load(QLocale(), u"qt"_s, u"_"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath)));

    QCoreApplication::installTranslator(&m_app);
    QCoreApplication::installTranslator(&m_qt);
}
