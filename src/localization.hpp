// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

/// The language chosen on the settings page, kept apart from the look and the backend.
class Localization : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)

 public:
    // Must not be default-constructible: Qt then prefers it over create() for the QML singleton.
    explicit Localization(QObject* parent);

    static Localization* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// "system" or an embedded language code such as "en", "zh_CN" or "zh_TW"
    QString language() const { return m_language; }
    void setLanguage(const QString& language);

    /// Every embedded catalogue, English first.
    QStringList languages() const { return m_languages; }
    /// The name a language is listed under on the settings page.
    Q_INVOKABLE QString labelFor(const QString& language) const;

 signals:
    void languageChanged();

 private:
    static QStringList availableLanguages();
    static bool supported(const QString& language);
    static QString codeForLocale(const QLocale& locale);
    static QString systemCode();
    void integrate();

    static Localization* s_instance;
    // English carries the plural forms for every language, so it stays installed.
    QTranslator m_plural;
    QTranslator m_app;
    QTranslator m_qt;
    QStringList m_languages;
    QString m_language;
};
