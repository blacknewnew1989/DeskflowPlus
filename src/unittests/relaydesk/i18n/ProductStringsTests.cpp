/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/i18n/ProductStrings.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTranslator>
#include <QXmlStreamReader>

using namespace deskflow::relaydesk::i18n;

namespace {

struct CatalogMessage
{
  QString source;
  QStringList translations;
  bool unfinished = false;
};

struct Catalog
{
  QString language;
  QList<CatalogMessage> messages;
  QString error;
};

Catalog readCatalog(const QString &path)
{
  Catalog catalog;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    catalog.error = file.errorString();
    return catalog;
  }

  QXmlStreamReader xml(&file);
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement())
      continue;

    if (xml.name() == QLatin1String("TS")) {
      catalog.language = xml.attributes().value(QLatin1String("language")).toString();
      continue;
    }
    if (xml.name() != QLatin1String("message"))
      continue;

    CatalogMessage message;
    while (!(xml.isEndElement() && xml.name() == QLatin1String("message")) && !xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement())
        continue;

      if (xml.name() == QLatin1String("source")) {
        message.source = xml.readElementText();
      } else if (xml.name() == QLatin1String("translation")) {
        message.unfinished = xml.attributes().value(QLatin1String("type")) == QLatin1String("unfinished");
        while (!(xml.isEndElement() && xml.name() == QLatin1String("translation")) && !xml.atEnd()) {
          xml.readNext();
          if (xml.isStartElement() && xml.name() == QLatin1String("numerusform"))
            message.translations.append(xml.readElementText());
          else if (xml.isCharacters() && !xml.isWhitespace())
            message.translations.append(xml.text().toString());
        }
      }
    }
    catalog.messages.append(message);
  }

  if (xml.hasError())
    catalog.error = xml.errorString();
  return catalog;
}

QStringList supportedLanguages()
{
  return QStringLiteral(RELAYDESK_SUPPORTED_LANGUAGES_FOR_TESTS).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

QSet<QString> placeholders(const QString &text)
{
  static const QRegularExpression placeholderPattern(QStringLiteral("%[1-9n]"));
  QSet<QString> result;
  auto matches = placeholderPattern.globalMatch(text);
  while (matches.hasNext())
    result.insert(matches.next().captured());
  return result;
}

} // namespace

class ProductStringsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void keysAreUniqueAndSemantic();
  void untranslatedCatalogUsesEnglishFallback();
  void pluralFallbackUsesCount();
  void catalogsHaveIdenticalCompleteKeys();
  void compiledCatalogsAreLoadable();
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
  QVERIFY(isPlural(Text::TransferHistoryItems));
  QVERIFY(!isPlural(Text::DevicesTitle));
  QCOMPARE(translatePlural(Text::DevicesDropItems, 1), QStringLiteral("1 item"));
  QCOMPARE(translatePlural(Text::DevicesDropItems, 3), QStringLiteral("3 items"));
  QCOMPARE(translatePlural(Text::PairingAttemptsRemaining, 1), QStringLiteral("1 attempt remaining"));
  QCOMPARE(translatePlural(Text::PairingAttemptsRemaining, 2), QStringLiteral("2 attempts remaining"));
  QCOMPARE(translatePlural(Text::TransferEtaSeconds, 1), QStringLiteral("1 second remaining"));
  QCOMPARE(translatePlural(Text::TransferEtaSeconds, 30), QStringLiteral("30 seconds remaining"));
  QCOMPARE(translatePlural(Text::TransferHistoryItems, 1).arg(1), QStringLiteral("1 item"));
  QCOMPARE(translatePlural(Text::TransferHistoryItems, 3).arg(3), QStringLiteral("3 items"));
}

void ProductStringsTests::catalogsHaveIdenticalCompleteKeys()
{
  const auto expectedKeyList = allKeys();
  const QSet<QString> expectedKeys(expectedKeyList.cbegin(), expectedKeyList.cend());
  const auto languages = supportedLanguages();
  QCOMPARE(languages.size(), 7);
  QMap<QString, QSet<QString>> expectedPlaceholders;

  for (const auto &language : languages) {
    const auto path = QStringLiteral("%1/relaydesk_%2.ts")
                          .arg(QStringLiteral(RELAYDESK_TRANSLATIONS_SOURCE_DIR), language);
    const auto catalog = readCatalog(path);
    QVERIFY2(catalog.error.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(path, catalog.error)));
    if (language == QStringLiteral("zh_CN"))
      QCOMPARE(catalog.language, language);
    else
      QCOMPARE(catalog.language.left(2), language);

    QSet<QString> actualKeys;
    for (const auto &message : catalog.messages) {
      QVERIFY2(!message.source.isEmpty(), qPrintable(path));
      QVERIFY2(
          !actualKeys.contains(message.source), qPrintable(QStringLiteral("duplicate: %1").arg(message.source))
      );
      actualKeys.insert(message.source);
      QVERIFY2(!message.unfinished, qPrintable(QStringLiteral("unfinished: %1").arg(message.source)));
      QVERIFY2(!message.translations.isEmpty(), qPrintable(QStringLiteral("empty: %1").arg(message.source)));
      for (const auto &translation : message.translations)
        QVERIFY2(
            !translation.trimmed().isEmpty(), qPrintable(QStringLiteral("empty form: %1").arg(message.source))
        );
      if (language == QStringLiteral("en")) {
        QSet<QString> expected;
        for (const auto &translation : message.translations)
          expected.unite(placeholders(translation));
        expectedPlaceholders.insert(message.source, expected);
      } else {
        for (const auto &translation : message.translations) {
          QCOMPARE(placeholders(translation), expectedPlaceholders.value(message.source));
        }
      }
    }

    const auto missing = expectedKeys - actualKeys;
    const auto extra = actualKeys - expectedKeys;
    QVERIFY2(
        missing.isEmpty(),
        qPrintable(QStringLiteral("%1 missing: %2").arg(language, QStringList(missing.values()).join(',')))
    );
    QVERIFY2(
        extra.isEmpty(),
        qPrintable(QStringLiteral("%1 extra: %2").arg(language, QStringList(extra.values()).join(',')))
    );
  }
}

void ProductStringsTests::compiledCatalogsAreLoadable()
{
  for (const auto &language : supportedLanguages()) {
    const auto path = QStringLiteral("%1/relaydesk_%2.qm")
                          .arg(QStringLiteral(RELAYDESK_TRANSLATIONS_BINARY_DIR), language);
    QTranslator translator;
    QVERIFY2(translator.load(path), qPrintable(path));

    for (int value = 0; value < static_cast<int>(Text::Count); ++value) {
      const auto text = static_cast<Text>(value);
      const auto semanticKey = key(text);
      const auto translated = translator.translate(
          "RelayDesk", semanticKey.toLatin1().constData(), nullptr, isPlural(text) ? 2 : -1
      );
      QVERIFY2(
          !translated.trimmed().isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(language, semanticKey))
      );
      QVERIFY2(translated != semanticKey, qPrintable(QStringLiteral("%1: %2").arg(language, semanticKey)));
    }
  }
}

QTEST_MAIN(ProductStringsTests)

#include "ProductStringsTests.moc"
