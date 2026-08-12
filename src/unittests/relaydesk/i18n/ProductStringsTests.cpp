/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/i18n/ProductStrings.h"

#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace deskflow::relaydesk::i18n;

class ProductStringsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void keysAreUniqueAndSemantic();
  void untranslatedCatalogUsesEnglishFallback();
  void pluralFallbackUsesCount();
};

void ProductStringsTests::keysAreUniqueAndSemantic()
{
  const auto keys = allKeys();
  QCOMPARE(keys.size(), static_cast<qsizetype>(Text::Count));
  QCOMPARE(QSet<QString>(keys.cbegin(), keys.cend()).size(), keys.size());

  const QRegularExpression semanticKeyPattern(QStringLiteral("^[a-z]+(?:\\.[a-z][a-z0-9_]*)+$"));
  for (const auto &semanticKey : keys)
    QVERIFY2(semanticKeyPattern.match(semanticKey).hasMatch(), qPrintable(semanticKey));
}

void ProductStringsTests::untranslatedCatalogUsesEnglishFallback()
{
  QCOMPARE(key(Text::DevicesStatusOnline), QStringLiteral("devices.status.online"));
  QCOMPARE(translate(Text::DevicesStatusOnline), QStringLiteral("Online"));
  QCOMPARE(translate(Text::TransferActionOpenFolder), QStringLiteral("Open folder"));
}

void ProductStringsTests::pluralFallbackUsesCount()
{
  QVERIFY(isPlural(Text::DevicesDropItems));
  QVERIFY(isPlural(Text::PairingAttemptsRemaining));
  QVERIFY(isPlural(Text::TransferEtaSeconds));
  QVERIFY(!isPlural(Text::DevicesTitle));
  QCOMPARE(translatePlural(Text::DevicesDropItems, 1), QStringLiteral("1 item"));
  QCOMPARE(translatePlural(Text::DevicesDropItems, 3), QStringLiteral("3 items"));
  QCOMPARE(translatePlural(Text::PairingAttemptsRemaining, 1), QStringLiteral("1 attempt remaining"));
  QCOMPARE(translatePlural(Text::PairingAttemptsRemaining, 2), QStringLiteral("2 attempts remaining"));
  QCOMPARE(translatePlural(Text::TransferEtaSeconds, 1), QStringLiteral("1 second remaining"));
  QCOMPARE(translatePlural(Text::TransferEtaSeconds, 30), QStringLiteral("30 seconds remaining"));
}

QTEST_MAIN(ProductStringsTests)

#include "ProductStringsTests.moc"
