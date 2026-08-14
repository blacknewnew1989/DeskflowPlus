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
#include <QLocale>
#include <QSignalSpy>

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

void I18NTests::detectedLangTest()
{
  QCOMPARE(I18N::detectedLanguages().size(), m_langMap.size());
  for (const auto &lang : I18N::detectedLanguages())
    QVERIFY(m_langMap.contains(lang));
}

void I18NTests::check639NameTest_validMapValues()
{
  for (const auto &lang : m_langMap.keys())
    QCOMPARE(I18N::nativeTo639Name(lang), m_langMap.value(lang));
}

void I18NTests::check639NameTest_invalidName()
{
  QCOMPARE(I18N::nativeTo639Name("INVALID"), QString());
}

void I18NTests::toNativeNameTest_validMapValues()
{
  for (const auto &lang : m_langMap.values())
    QCOMPARE(I18N::toNativeName(lang), m_langMap.key(lang));
}

void I18NTests::toNativeNameTest_invalidName()
{
  QCOMPARE(I18N::toNativeName("INVALID"), QString());
}

void I18NTests::setLangTest_validLangs()
{
  // make sure we are not staring with our language set to the maps last value
  // ensures a languageChanged signal will be emited for each itteration of the testing loop
  I18N::setLanguage(m_langMap.value(m_langMap.lastKey()));
  QSignalSpy spy(I18N::instance(), &I18N::languageChanged);
  for (const auto &lang : m_langMap.values()) {
    I18N::setLanguage(lang);
    QCOMPARE(I18N::currentLanguage(), lang);
  }
  QCOMPARE(spy.count(), m_langMap.count());
}

void I18NTests::setLangTest_invalidLang()
{
  QSignalSpy spy(I18N::instance(), &I18N::languageChanged);
  I18N::setLanguage("INVALID-LANGUAGE");
  QCOMPARE(spy.count(), 0);
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
}

void I18NTests::selectedLocaleFollowsLanguage()
{
  I18N::setLanguage(QStringLiteral("ja"));
  QCOMPARE(QLocale().language(), QLocale::Japanese);

  I18N::setLanguage(QStringLiteral("ru"));
  QCOMPARE(QLocale().language(), QLocale::Russian);
}

void I18NTests::productCatalogTest()
{
  I18N::setLanguage(QStringLiteral("zh_CN"));
  QCOMPARE(QCoreApplication::translate("RelayDesk", "devices.status.online"), QStringLiteral("在线"));
  QCOMPARE(QCoreApplication::translate("RelayDesk", "transfer.action.open_folder"), QStringLiteral("打开文件夹"));
  QCOMPARE(
      QCoreApplication::translate("RelayDesk", "devices.drop.items", nullptr, 3), QStringLiteral("3 个项目")
  );

  I18N::setLanguage(QStringLiteral("en"));
  QCOMPARE(QCoreApplication::translate("RelayDesk", "devices.status.online"), QStringLiteral("Online"));
}

void I18NTests::productCatalogsCoverEverySelectableLanguage()
{
  const QMap<QString, QString> expectedOnlineTranslations{
      {QStringLiteral("en"), QStringLiteral("Online")},
      {QStringLiteral("es"), QStringLiteral("En línea")},
      {QStringLiteral("it"), QStringLiteral("Online")},
      {QStringLiteral("ja"), QStringLiteral("オンライン")},
      {QStringLiteral("ko"), QStringLiteral("온라인")},
      {QStringLiteral("ru"), QStringLiteral("В сети")},
      {QStringLiteral("zh_CN"), QStringLiteral("在线")},
  };

  for (auto it = expectedOnlineTranslations.cbegin(); it != expectedOnlineTranslations.cend(); ++it) {
    I18N::setLanguage(it.key());
    QCOMPARE(QCoreApplication::translate("RelayDesk", "devices.status.online"), it.value());
    QVERIFY(QCoreApplication::translate("RelayDesk", "transfer.action.open_folder") !=
            QStringLiteral("transfer.action.open_folder"));
    QVERIFY(QCoreApplication::translate("RelayDesk", "pairing.success") != QStringLiteral("pairing.success"));
  }
}

void I18NTests::reDetectTest()
{
  QSignalSpy spy(I18N::instance(), &I18N::languagesChanged);

  I18N::reDetectLanguages();
  QCOMPARE(spy.count(), 0);

  QFile::remove(QStringLiteral("%1/relaydesk_en.qm").arg(m_myTDir));

  I18N::reDetectLanguages();
  QCOMPARE(spy.count(), 1);
  QVERIFY(!I18N::detectedLanguages().contains(QStringLiteral("English")));
}

QTEST_MAIN(I18NTests)
