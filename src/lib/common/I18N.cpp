/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "I18N.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QList>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QTranslator>

#include <string_view>

I18N *I18N::instance()
{
  static I18N m;
  return &m;
}

I18N::I18N(QObject *parent) : QObject{parent}
{
  const auto appDir = QCoreApplication::applicationDirPath();
  const auto homeDir = QDir::homePath();

  const QList<QDir> appTrDirs{
      {QStringLiteral("%1/%2").arg(appDir, QStringLiteral("translations"))},
      {QStringLiteral("%1/../translations").arg(appDir)},
      {QStringLiteral("%1/../Resources/translations").arg(appDir)},
      {QStringLiteral("%1/../share/%2/translations").arg(appDir, kAppId)},
      {QStringLiteral("%1/.local/share/%2/translations").arg(homeDir, kAppId)},
      {QStringLiteral("/usr/local/share/%1/translations").arg(kAppId)},
      {QStringLiteral("/usr/share/%1/translations").arg(kAppId)}
  };
  const QStringList appTrFilter{QStringLiteral("%1*.qm").arg(kAppId)};

  for (const auto &dir : appTrDirs) {
    if (!dir.entryList(appTrFilter, QDir::Files, QDir::Name).isEmpty()) {
      m_appTrPath = dir.absolutePath();
      break;
    }
  }

  if (m_appTrPath.isEmpty()) {
    qInfo() << "no app translations found";
  }

  const auto qt = QStringLiteral("qt");
  const auto qt6 = QStringLiteral("qt6");

  const QList<QDir> qtTrDirs{
      {QStringLiteral("%1/%2").arg(appDir, QStringLiteral("translations"))},
      {QStringLiteral("%1/../Resources/translations").arg(appDir)},
      {QStringLiteral("%1/../qt-depends/translations").arg(appDir)},
      {QStringLiteral("%1/../share/%2/translations").arg(appDir, qt6)},
      {QStringLiteral("%1/../share/%2/translations").arg(appDir, qt)},
      {QStringLiteral("%1/.local/share/%2/translations").arg(homeDir, qt6)},
      {QStringLiteral("%1/.local/share/%2/translations").arg(homeDir, qt)},
      {QStringLiteral("/usr/local/share/%2/translations").arg(qt6)},
      {QStringLiteral("/usr/local/share/%2/translations").arg(qt)},
      {QStringLiteral("/usr/share/%2/translations").arg(qt6)},
      {QStringLiteral("/usr/share/%2/translations").arg(qt)}
  };
  const QStringList qtTrFilter{QStringLiteral("qt_*.qm")};

  for (const auto &dir : qtTrDirs) {
    if (!dir.entryList(qtTrFilter, QDir::Files, QDir::Name).isEmpty()) {
      m_qtTrPath = dir.absolutePath();
      break;
    }
  }

  if (m_qtTrPath.isEmpty()) {
    qInfo() << "no qt translations found";
  }

  detectLanguages();

  const auto configuredLanguage = Settings::value(Settings::Core::Language).toString();
  const auto requestedLanguage =
      configuredLanguage.isEmpty() ? QLocale::system().name() : configuredLanguage;
  const auto resolvedLanguage = resolveLanguage(requestedLanguage);
  activateLanguage(resolvedLanguage, !configuredLanguage.isEmpty() && configuredLanguage != resolvedLanguage);
}

QStringList I18N::detectedLanguages()
{
  QStringList languages;
  for (const auto &code : detectedLanguageCodes())
    languages.append(instance()->m_nameMap.value(code));
  return languages;
}

QStringList I18N::supportedLanguageCodes()
{
  QStringList languages;
  languages.reserve(static_cast<qsizetype>(kRelayDeskSupportedLanguages.size()));
  for (const std::string_view language : kRelayDeskSupportedLanguages)
    languages.append(QString::fromLatin1(language.data(), static_cast<qsizetype>(language.size())));
  return languages;
}

QStringList I18N::detectedLanguageCodes()
{
  QStringList languages;
  for (const auto &code : supportedLanguageCodes()) {
    if (instance()->m_translations.contains(code))
      languages.append(code);
  }
  return languages;
}

QString I18N::fallbackLanguage()
{
  return QString::fromLatin1(
      kRelayDeskFallbackLanguage.data(), static_cast<qsizetype>(kRelayDeskFallbackLanguage.size())
  );
}

QString I18N::nativeTo639Name(QString nativeName)
{
  return instance()->m_nameMap.key(nativeName);
}

QString I18N::toNativeName(QString shortName)
{
  return instance()->m_nameMap.value(shortName);
}

QString I18N::currentLanguage()
{
  return instance()->m_currentLang;
}

void I18N::setLanguage(const QString &langName)
{
  const auto resolvedLanguage = instance()->resolveLanguage(langName);
  instance()->activateLanguage(resolvedLanguage, true);
}

void I18N::reDetectLanguages()
{
  instance()->detectLanguages();
  if (!instance()->m_translations.contains(instance()->m_currentLang))
    instance()->activateLanguage(instance()->fallbackLanguage(), true);
}

void I18N::activateLanguage(const QString &langName, bool persist)
{
  const auto resolvedLanguage = resolveLanguage(langName);
  if (persist)
    Settings::setValue(Settings::Core::Language, resolvedLanguage);

  if (resolvedLanguage == m_currentLang && !m_currentTranslations.isEmpty())
    return;

  for (const auto &translation : std::as_const(m_currentTranslations))
    QCoreApplication::removeTranslator(translation);

  qDeleteAll(m_currentTranslations);
  m_currentTranslations.clear();

  for (const auto &translation : m_translations.value(resolvedLanguage)) {
    auto translator = new QTranslator(this);
    if (translator->load(translation)) {
      m_currentTranslations.append(translator);
      QCoreApplication::installTranslator(translator);
    } else {
      delete translator;
    }
  }

  const auto previousLanguage = m_currentLang;
  m_currentLang = resolvedLanguage;
  QLocale::setDefault(QLocale(resolvedLanguage));
  if (previousLanguage != resolvedLanguage)
    Q_EMIT languageChanged(resolvedLanguage);
}

QString I18N::resolveLanguage(const QString &langName) const
{
  const auto canonical = canonicalLanguageCode(langName);
  if (m_translations.contains(canonical))
    return canonical;

  return fallbackLanguage();
}

QString I18N::canonicalLanguageCode(const QString &langName)
{
  auto candidate = langName.trimmed();
  candidate.replace(QLatin1Char('-'), QLatin1Char('_'));
  static const QRegularExpression localePattern(QStringLiteral("^[A-Za-z]{2}(?:_[A-Za-z]{2,4})?$"));
  if (!localePattern.match(candidate).hasMatch())
    return {};

  for (const auto &supported : supportedLanguageCodes()) {
    if (candidate.compare(supported, Qt::CaseInsensitive) == 0)
      return supported;
    if (!supported.contains(QLatin1Char('_')) &&
        candidate.left(2).compare(supported, Qt::CaseInsensitive) == 0) {
      return supported;
    }
  }
  return {};
}

void I18N::detectLanguages()
{
  const auto oldList = m_translations;
  m_translations.clear();
  m_nameMap.clear();

  QStringList nameFilter = {QStringLiteral("%1_*.qm").arg(kAppId)};
  QMap<QString, QString> appTranslations;
  QMap<QString, QString> productTranslations;
  QMap<QString, QString> nativeNames;
  QStringList detectedLangCodes;
  QDir dir(m_appTrPath);
  QStringList langList = dir.entryList(nameFilter, QDir::Files, QDir::Name);

  for (const QString &translation : std::as_const(langList)) {
    QTranslator translator;
    std::ignore = translator.load(translation, dir.absolutePath());
    const auto shortCode = canonicalLanguageCode(translator.language());
    if (shortCode.isEmpty())
      continue;
    //: Replace with your Language name
    //: This is a required string
    QString nativeLang = translator.translate("i18n", "LocalizedName");
    if (nativeLang.isEmpty())
      nativeLang = QStringLiteral("English");

    appTranslations.insert(shortCode, translator.filePath());
    nativeNames.insert(shortCode, nativeLang);
    detectedLangCodes.append(QStringLiteral("qt_%1.qm").arg(shortCode));
  }

  nameFilter = {QStringLiteral("%1_*.qm").arg(kProductTranslationCatalog)};
  langList = dir.entryList(nameFilter, QDir::Files, QDir::Name);
  for (const QString &translation : std::as_const(langList)) {
    QTranslator translator;
    std::ignore = translator.load(translation, dir.absolutePath());
    const auto shortCode = canonicalLanguageCode(translator.language());
    if (!shortCode.isEmpty())
      productTranslations.insert(shortCode, translator.filePath());
  }

  dir.setPath(m_qtTrPath);
  const static auto qtTrNameLen = 3; // length of qt_
  langList = dir.entryList(detectedLangCodes, QDir::Files, QDir::Name);

  QMap<QString, QString> qtTranslations;
  for (const QString &translation : std::as_const(langList)) {
    QString lang = translation.mid(qtTrNameLen, translation.lastIndexOf(QLatin1Char('.')) - qtTrNameLen);
    qtTranslations.insert(lang, QStringLiteral("%1/%2").arg(m_qtTrPath, translation));
  }

  for (const QString &lang : supportedLanguageCodes()) {
    if (!appTranslations.contains(lang) || !productTranslations.contains(lang))
      continue;
    QStringList translations{appTranslations.value(lang)};
    translations.append(productTranslations.value(lang));
    if (qtTranslations.contains(lang))
      translations.append(qtTranslations.value(lang));
    m_translations.insert(lang, translations);
    m_nameMap.insert(lang, nativeNames.value(lang));
  }

  if (oldList != m_translations)
    Q_EMIT languagesChanged(m_translations.keys());
}
