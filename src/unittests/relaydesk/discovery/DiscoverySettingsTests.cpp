/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoverySettings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

class DiscoverySettingsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void parsesIpv4Ipv6AndHostname_data();
  void parsesIpv4Ipv6AndHostname();
  void rejectsInvalidHostAndPort_data();
  void rejectsInvalidHostAndPort();
  void storeRoundTripsAndDeduplicates();
  void migratesLegacySingleAddress();
  void corruptAndFutureSettingsAreRejected();
};

void DiscoverySettingsTests::parsesIpv4Ipv6AndHostname_data()
{
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  QTest::newRow("ipv4") << QStringLiteral(" 192.168.1.20 ") << QStringLiteral("192.168.1.20");
  QTest::newRow("ipv6") << QStringLiteral("2001:0db8::1") << QStringLiteral("2001:db8::1");
  QTest::newRow("bracketed-ipv6") << QStringLiteral("[fe80::1%3]") << QStringLiteral("fe80::1%3");
  QTest::newRow("hostname") << QStringLiteral("RelayDesk-PC.LOCAL.") << QStringLiteral("relaydesk-pc.local");
  QTest::newRow("idn") << QString::fromUtf8("工作站.local") << QStringLiteral("xn--2qq276am0v.local");
}

void DiscoverySettingsTests::parsesIpv4Ipv6AndHostname()
{
  QFETCH(QString, input);
  QFETCH(QString, expected);

  QString diagnostic;
  const auto parsed = parseManualAddress(input, kDefaultManualInputPort, kDefaultManualFilePort, &diagnostic);
  QVERIFY2(parsed.has_value(), qPrintable(diagnostic));
  QCOMPARE(parsed->host, expected);
  QCOMPARE(parsed->inputPort, quint16(24800));
  QCOMPARE(parsed->filePort, quint16(24801));
}

void DiscoverySettingsTests::rejectsInvalidHostAndPort_data()
{
  QTest::addColumn<QString>("host");
  QTest::addColumn<int>("inputPort");
  QTest::addColumn<int>("filePort");

  QTest::newRow("empty") << QString() << 24800 << 24801;
  QTest::newRow("scheme") << QStringLiteral("https://host") << 24800 << 24801;
  QTest::newRow("malformed-ipv4") << QStringLiteral("999.1.2.3") << 24800 << 24801;
  QTest::newRow("malformed-ipv6") << QStringLiteral("2001:::1") << 24800 << 24801;
  QTest::newRow("underscore") << QStringLiteral("bad_host.local") << 24800 << 24801;
  QTest::newRow("mismatched-bracket") << QStringLiteral("[::1") << 24800 << 24801;
  QTest::newRow("zero-input-port") << QStringLiteral("host.local") << 0 << 24801;
  QTest::newRow("large-file-port") << QStringLiteral("host.local") << 24800 << 65536;
}

void DiscoverySettingsTests::rejectsInvalidHostAndPort()
{
  QFETCH(QString, host);
  QFETCH(int, inputPort);
  QFETCH(int, filePort);
  QVERIFY(!parseManualAddress(host, inputPort, filePort).has_value());
}

void DiscoverySettingsTests::storeRoundTripsAndDeduplicates()
{
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  DiscoverySettingsStore store(settings);
  const DiscoverySettings snapshot{
      .enabled = false,
      .manualAddresses = {
          *parseManualAddress(QStringLiteral("HOST.local")),
          *parseManualAddress(QStringLiteral("host.local.")),
          *parseManualAddress(QStringLiteral("10.0.0.5"), 25000, 25001),
      },
  };

  QString diagnostic;
  QVERIFY2(store.save(snapshot, &diagnostic), qPrintable(diagnostic));
  const auto loaded = store.load();
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostic));
  QVERIFY(!loaded.settings.enabled);
  QCOMPARE(loaded.settings.manualAddresses.size(), 2);
  QCOMPARE(loaded.settings.manualAddresses.at(0).host, QStringLiteral("host.local"));
  QCOMPARE(loaded.settings.manualAddresses.at(1).inputPort, quint16(25000));
  QCOMPARE(settings.value(DiscoverySettingsStore::schemaVersionKey()).toInt(), 1);
  QVERIFY(DiscoverySettingsStore::manualAddressesKey().startsWith(QStringLiteral("relaydesk/discovery/")));
}

void DiscoverySettingsTests::migratesLegacySingleAddress()
{
  QTemporaryDir temporaryDirectory;
  QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(DiscoverySettingsStore::legacyManualHostKey(), QStringLiteral("legacy-host.local"));
  settings.setValue(DiscoverySettingsStore::legacyManualInputPortKey(), 24900);
  settings.setValue(DiscoverySettingsStore::legacyManualFilePortKey(), 24901);
  settings.setValue(DiscoverySettingsStore::enabledKey(), false);

  DiscoverySettingsStore store(settings);
  const auto loaded = store.load();
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostic));
  QVERIFY(loaded.migrated);
  QVERIFY(!loaded.settings.enabled);
  QCOMPARE(loaded.settings.manualAddresses.size(), 1);
  QCOMPARE(loaded.settings.manualAddresses.first().host, QStringLiteral("legacy-host.local"));
  QCOMPARE(loaded.settings.manualAddresses.first().inputPort, quint16(24900));
  QVERIFY(!settings.contains(DiscoverySettingsStore::legacyManualHostKey()));
  QCOMPARE(settings.value(DiscoverySettingsStore::schemaVersionKey()).toInt(), 1);
}

void DiscoverySettingsTests::corruptAndFutureSettingsAreRejected()
{
  QTemporaryDir temporaryDirectory;
  QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(DiscoverySettingsStore::schemaVersionKey(), kDiscoverySettingsSchemaVersion + 1);
  DiscoverySettingsStore store(settings);
  QVERIFY(!store.load().ok);

  settings.setValue(DiscoverySettingsStore::schemaVersionKey(), kDiscoverySettingsSchemaVersion);
  settings.beginWriteArray(DiscoverySettingsStore::manualAddressesKey(), 1);
  settings.setArrayIndex(0);
  settings.setValue(QStringLiteral("host"), QStringLiteral("host.local"));
  settings.setValue(QStringLiteral("inputPort"), 0);
  settings.setValue(QStringLiteral("filePort"), 24801);
  settings.endArray();
  QVERIFY(!store.load().ok);
}

QTEST_MAIN(DiscoverySettingsTests)

#include "DiscoverySettingsTests.moc"
