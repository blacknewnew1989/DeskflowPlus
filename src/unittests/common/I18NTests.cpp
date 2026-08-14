/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "I18NTests.h"

#include "common/I18N.h"
#include "common/Settings.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSet>

void I18NTests::initTestCase()
{
  QFile oldSettings(m_settingsFile);
  if (oldSettings.exists())
    oldSettings.remove();
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);

  m_myTDir = QStringLiteral("%1/translations").arg(QCoreApplication::applicationDirPath());
  const auto srcTDir = QStringLiteral("%1/../../../translations").arg(QCoreApplication::applicationDirPath());

  QDir dir;
  if (dir.exists(m_myTDir)) {
    dir.setPath(m_myTDir);
    dir.removeRecursively();
  }

  dir.mkdir(m_myTDir);
  dir.setPath(srcTDir);
  for (const auto &file : dir.entryList({"deskflow_*.qm", "relaydesk_*.qm"}, QDir::Files, QDir::Name)) {
    QFile::copy(QStringLiteral("%1/%2").arg(srcTDir, file), QStringLiteral("%1/%2").arg(m_myTDir, file));
    QVERIFY(QFile::exists(QStringLiteral("%1/%2").arg(m_myTDir, file)));
  }
}

void I18NTests::creationTest()
{
  QVERIFY(I18N::instance());
}

void I18NTests::supportedLanguagesUseCanonicalOrder()
{
  const auto supported = I18N::supportedLanguageCodes();
  QCOMPARE(supported.size(), 7);
  QCOMPARE(QSet<QString>(supported.cbegin(), supported.cend()).size(), supported.size());
  QCOMPARE(supported.first(), I18N::fallbackLanguage());
  QCOMPARE(I18N::fallbackLanguage(), QStringLiteral("en"));
  QVERIFY(supported.contains(QStringLiteral("zh_CN")));
}

void I18NTests::detectedLangTest()
{
  QCOMPARE(I18N::detectedLanguageCodes(), I18N::supportedLanguageCodes());
  QCOMPARE(I18N::detectedLanguages(), m_nativeLanguageNames);
}

void I18NTests::check639NameTest_validMapValues()
{
  const auto supported = I18N::supportedLanguageCodes();
  for (qsizetype index = 0; index < supported.size(); ++index)
    QCOMPARE(I18N::nativeTo639Name(m_nativeLanguageNames.at(index)), supported.at(index));
}

void I18NTests::check639NameTest_invalidName()
{
  QCOMPARE(I18N::nativeTo639Name("INVALID"), QString());
}

void I18NTests::toNativeNameTest_validMapValues()
{
  const auto supported = I18N::supportedLanguageCodes();
  for (qsizetype index = 0; index < supported.size(); ++index)
    QCOMPARE(I18N::toNativeName(supported.at(index)), m_nativeLanguageNames.at(index));
}

void I18NTests::toNativeNameTest_invalidName()
{
  QCOMPARE(I18N::toNativeName("INVALID"), QString());
}

void I18NTests::setLangTest_validLangs()
{
  // make sure we are not staring with our language set to the maps last value
  // ensures a languageChanged signal will be emited for each itteration of the testing loop
  const auto supported = I18N::supportedLanguageCodes();
  I18N::setLanguage(supported.constLast());
  QSignalSpy spy(I18N::instance(), &I18N::languageChanged);
  for (const auto &lang : supported) {
    I18N::setLanguage(lang);
    QCOMPARE(I18N::currentLanguage(), lang);
  }
  QCOMPARE(spy.count(), supported.count());
}

void I18NTests::setLangTest_invalidLang()
{
  I18N::setLanguage(QStringLiteral("zh_CN"));
  QSignalSpy spy(I18N::instance(), &I18N::languageChanged);
  I18N::setLanguage("INVALID-LANGUAGE");
  QCOMPARE(I18N::currentLanguage(), I18N::fallbackLanguage());
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), I18N::fallbackLanguage());
  QCOMPARE(spy.count(), 1);

  I18N::setLanguage(QStringLiteral("es-damaged"));
  QCOMPARE(I18N::currentLanguage(), I18N::fallbackLanguage());
}

void I18NTests::setLangTest_currentLang()
{
  QSignalSpy spy(I18N::instance(), &I18N::languageChanged);
  I18N::setLanguage(I18N::currentLanguage());
  QCOMPARE(spy.count(), 0);
}

void I18NTests::selectedLanguageIsPersisted()
{
  I18N::setLanguage(QStringLiteral("ja"));
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), QStringLiteral("ja"));

  QSettings persisted(m_settingsFile, QSettings::IniFormat);
  persisted.sync();
  QCOMPARE(persisted.value(Settings::Core::Language).toString(), QStringLiteral("ja"));

  Settings::setValue(Settings::Core::Language, QStringLiteral("damaged-value"));
  I18N::setLanguage(Settings::value(Settings::Core::Language).toString());
  QCOMPARE(I18N::currentLanguage(), I18N::fallbackLanguage());
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), I18N::fallbackLanguage());
}

void I18NTests::productCatalogTest()
{
  for (const auto &language : I18N::supportedLanguageCodes()) {
    I18N::setLanguage(language);
    QCOMPARE(I18N::currentLanguage(), language);
    const auto translated = QCoreApplication::translate("RelayDesk", "devices.status.online");
    QVERIFY2(!translated.isEmpty(), qPrintable(language));
    QVERIFY2(translated != QStringLiteral("devices.status.online"), qPrintable(language));
  }

  I18N::setLanguage(QStringLiteral("zh_CN"));
  QCOMPARE(QCoreApplication::translate("RelayDesk", "devices.status.online"), QStringLiteral("在线"));
  QCOMPARE(QCoreApplication::translate("RelayDesk", "transfer.action.open_folder"), QStringLiteral("打开目录"));
}

void I18NTests::reDetectTest()
{
  QSignalSpy spy(I18N::instance(), &I18N::languagesChanged);

  I18N::reDetectLanguages();
  QCOMPARE(spy.count(), 0);

  QFile::remove(QStringLiteral("%1/relaydesk_en.qm").arg(m_myTDir));

  I18N::reDetectLanguages();
  QCOMPARE(spy.count(), 1);
  QVERIFY(!I18N::detectedLanguageCodes().contains(QStringLiteral("en")));
}

QTEST_MAIN(I18NTests)
