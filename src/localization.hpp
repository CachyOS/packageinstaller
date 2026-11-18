// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLocale>
#include <QObject>
#include <QString>
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

 public:
    // Must not be default-constructible: Qt then prefers it over create() for the QML singleton.
    explicit Localization(QObject* parent);

    static Localization* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// "system" or an embedded language code such as "en", "zh_CN" or "zh_TW"
    QString language() const { return m_language; }
    void setLanguage(const QString& language);

 signals:
    void languageChanged();

 private:
    static bool supported(const QString& language);
    static QString codeForLocale(const QLocale& locale);
    static QString systemCode();
    void integrate();

    static Localization* s_instance;
    QTranslator m_translator;
    QString m_language;
};
