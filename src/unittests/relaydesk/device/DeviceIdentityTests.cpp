/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/device/DeviceIdentity.h"
#include "relaydesk/device/DeviceInfo.h"

#include <QCborMap>
#include <QCborValue>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

class DeviceIdentityTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void firstStartCreatesPersistentId();
  void subsequentLoadsReturnStableId();
  void invalidStoredIdIsRecovered_data();
  void invalidStoredIdIsRecovered();
  void capabilitiesRoundTrip();
  void unsupportedSchemaVersionIsRejected();
};

void DeviceIdentityTests::firstStartCreatesPersistentId()
{
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

  DeviceIdentity identity(settings);
  QString errorMessage;
  const auto deviceId = identity.loadOrCreate(&errorMessage);

  QVERIFY2(deviceId.has_value(), qPrintable(errorMessage));
  QVERIFY(!deviceId->value().isNull());
  QCOMPARE(settings.value(DeviceIdentity::settingsKey()).toString(), deviceId->toString());
}

void DeviceIdentityTests::subsequentLoadsReturnStableId()
{
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const auto settingsFile = temporaryDirectory.filePath(QStringLiteral("settings.ini"));

  QString firstId;
  {
    QSettings settings(settingsFile, QSettings::IniFormat);
    DeviceIdentity identity(settings);
    const auto deviceId = identity.loadOrCreate();
    QVERIFY(deviceId.has_value());
    firstId = deviceId->toString();
  }

  QSettings settings(settingsFile, QSettings::IniFormat);
  DeviceIdentity identity(settings);
  const auto reloadedId = identity.loadOrCreate();
  QVERIFY(reloadedId.has_value());
  QCOMPARE(reloadedId->toString(), firstId);
}

void DeviceIdentityTests::invalidStoredIdIsRecovered_data()
{
  QTest::addColumn<QVariant>("storedValue");

  QTest::newRow("empty") << QVariant(QString());
  QTest::newRow("malformed") << QVariant(QStringLiteral("not-a-uuid"));
  QTest::newRow("null-uuid") << QVariant(QStringLiteral("00000000-0000-0000-0000-000000000000"));
  QTest::newRow("wrong-type") << QVariant(42);
}

void DeviceIdentityTests::invalidStoredIdIsRecovered()
{
  QFETCH(QVariant, storedValue);

  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(DeviceIdentity::settingsKey(), storedValue);
  settings.sync();

  DeviceIdentity identity(settings);
  const auto recoveredId = identity.loadOrCreate();

  QVERIFY(recoveredId.has_value());
  QVERIFY(!recoveredId->value().isNull());
  QVERIFY(DeviceId::fromString(settings.value(DeviceIdentity::settingsKey()).toString()).has_value());
}

void DeviceIdentityTests::capabilitiesRoundTrip()
{
  const DeviceCapabilities capabilities{
      .input = true,
      .clipboardText = true,
      .clipboardImage = false,
      .fileV1 = true,
      .folderV1 = true,
      .resumeV1 = true,
  };
  const DeviceInfo device{
      .deviceId = DeviceId::generate(),
      .displayName = QStringLiteral("Design MacBook"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .appVersion = QStringLiteral("1.0.0"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = capabilities,
      .certificateFingerprintSha256 = QByteArray(32, '\x2a'),
  };

  QString errorMessage;
  const auto encoded = DeviceInfoCodec::serialize(device, &errorMessage);
  QVERIFY2(!encoded.isEmpty(), qPrintable(errorMessage));

  const auto decoded = DeviceInfoCodec::deserialize(encoded, &errorMessage);
  QVERIFY2(decoded.has_value(), qPrintable(errorMessage));
  QCOMPARE(decoded->deviceId.toString(), device.deviceId.toString());
  QCOMPARE(decoded->displayName, device.displayName);
  QCOMPARE(decoded->platform, device.platform);
  QCOMPARE(decoded->architecture, device.architecture);
  QCOMPARE(decoded->appVersion, device.appVersion);
  QCOMPARE(decoded->inputPort, device.inputPort);
  QCOMPARE(decoded->filePort, device.filePort);
  QVERIFY(decoded->capabilities == device.capabilities);
  QCOMPARE(decoded->certificateFingerprintSha256, device.certificateFingerprintSha256);
}

void DeviceIdentityTests::unsupportedSchemaVersionIsRejected()
{
  const DeviceInfo device{
      .deviceId = DeviceId::generate(),
      .displayName = QStringLiteral("Windows Workstation"),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("1.0.0"),
  };

  auto map = QCborValue::fromCbor(DeviceInfoCodec::serialize(device)).toMap();
  map.insert(QCborValue(2), QCborValue(kDeviceInfoSchemaVersion + 1));

  QString errorMessage;
  const auto decoded = DeviceInfoCodec::deserialize(QCborValue(map).toCbor(), &errorMessage);

  QVERIFY(!decoded.has_value());
  QVERIFY(errorMessage.contains(QStringLiteral("schema version")));
}

QTEST_MAIN(DeviceIdentityTests)

#include "DeviceIdentityTests.moc"
