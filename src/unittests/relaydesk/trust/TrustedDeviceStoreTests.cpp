/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {

TrustedDevice makeDevice(const DeviceId &id, char fingerprintByte = '\x2a')
{
  return {
      .deviceId = id,
      .alias = QStringLiteral("  设计 Mac  "),
      .platform = QStringLiteral(" macos "),
      .fingerprintSha256 = QByteArray(kSha256FingerprintBytes, fingerprintByte),
      .lastAddresses = {QStringLiteral("192.168.1.20"), QStringLiteral("2001:db8::1"), QStringLiteral("192.168.1.20")},
      .autoAcceptFiles = true,
  };
}

bool overwrite(const QString &path, QByteArrayView bytes)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size();
}

} // namespace

class TrustedDeviceStoreTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void missingStoreLoadsEmpty();
  void roundTripsAndNormalizesRecords();
  void upsertReplacesByStableDeviceId();
  void trustStatusRejectsFingerprintChangesAndRevocation();
  void recoversCorruptPrimaryFromBackup();
  void rejectsCorruptPrimaryAndBackup();
  void rejectsInvalidRecords();
  void rejectsDuplicateDeviceIds();
};

void TrustedDeviceStoreTests::missingStoreLoadsEmpty()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted-devices.json")));

  const auto result = store.load();

  QVERIFY2(result.ok, qPrintable(result.diagnostic));
  QCOMPARE(result.source, TrustedDeviceLoadSource::Empty);
  QVERIFY(store.devices().isEmpty());
}

void TrustedDeviceStoreTests::roundTripsAndNormalizesRecords()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("state/trusted-devices.json"));
  const auto id = DeviceId::generate();
  TrustedDeviceStore store(path);
  QString diagnostic;
  QVERIFY2(store.upsert(makeDevice(id), &diagnostic), qPrintable(diagnostic));
  const auto saveResult = store.save();
  QVERIFY2(saveResult.ok, qPrintable(saveResult.diagnostic));
  QVERIFY(QFileInfo::exists(path));
  QVERIFY(QFileInfo::exists(store.backupPath()));

  TrustedDeviceStore reloaded(path);
  const auto loadResult = reloaded.load();
  QVERIFY2(loadResult.ok, qPrintable(loadResult.diagnostic));
  QCOMPARE(loadResult.source, TrustedDeviceLoadSource::Primary);
  const auto record = reloaded.find(id);
  QVERIFY(record.has_value());
  QCOMPARE(record->alias, QStringLiteral("设计 Mac"));
  QCOMPARE(record->platform, QStringLiteral("macos"));
  QCOMPARE(record->lastAddresses, QStringList({QStringLiteral("192.168.1.20"), QStringLiteral("2001:db8::1")}));
  QVERIFY(record->autoAcceptFiles);
  QVERIFY(!record->revoked);
}

void TrustedDeviceStoreTests::upsertReplacesByStableDeviceId()
{
  QTemporaryDir directory;
  const auto id = DeviceId::generate();
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.upsert(makeDevice(id)));
  auto updated = makeDevice(id, '\x3b');
  updated.alias = QStringLiteral("Renamed PC");
  updated.autoAcceptFiles = false;
  QVERIFY(store.upsert(updated));

  QCOMPARE(store.devices().size(), 1);
  const auto record = store.find(id);
  QVERIFY(record.has_value());
  QCOMPARE(record->alias, QStringLiteral("Renamed PC"));
  QCOMPARE(record->fingerprintSha256, QByteArray(kSha256FingerprintBytes, '\x3b'));
  QVERIFY(!record->autoAcceptFiles);
}

void TrustedDeviceStoreTests::trustStatusRejectsFingerprintChangesAndRevocation()
{
  QTemporaryDir directory;
  const auto id = DeviceId::generate();
  const auto unknownId = DeviceId::generate();
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.upsert(makeDevice(id)));

  QCOMPARE(store.trustStatus(unknownId, QByteArray(kSha256FingerprintBytes, '\x2a')), TrustStatus::Unknown);
  QCOMPARE(store.trustStatus(id, QByteArray(kSha256FingerprintBytes, '\x2a')), TrustStatus::Trusted);
  QCOMPARE(store.trustStatus(id, QByteArray(kSha256FingerprintBytes, '\x2b')), TrustStatus::FingerprintMismatch);
  QCOMPARE(store.trustStatus(id, QByteArray(20, '\x2a')), TrustStatus::FingerprintMismatch);
  QVERIFY(store.revoke(id));
  QCOMPARE(store.trustStatus(id, QByteArray(kSha256FingerprintBytes, '\x2a')), TrustStatus::Revoked);
  QVERIFY(!store.find(id)->autoAcceptFiles);
}

void TrustedDeviceStoreTests::recoversCorruptPrimaryFromBackup()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("trusted.json"));
  const auto id = DeviceId::generate();
  TrustedDeviceStore store(path);
  QVERIFY(store.upsert(makeDevice(id)));
  QVERIFY(store.save().ok);
  QVERIFY(overwrite(path, QByteArrayLiteral("{broken")));

  TrustedDeviceStore recovered(path);
  const auto result = recovered.load();

  QVERIFY2(result.ok, qPrintable(result.diagnostic));
  QCOMPARE(result.source, TrustedDeviceLoadSource::Backup);
  QVERIFY(result.diagnostic.contains(QStringLiteral("recovered")));
  QVERIFY(recovered.find(id).has_value());
}

void TrustedDeviceStoreTests::rejectsCorruptPrimaryAndBackup()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("trusted.json"));
  TrustedDeviceStore store(path);
  QVERIFY(overwrite(path, QByteArrayLiteral("not-json")));
  QVERIFY(overwrite(store.backupPath(), QByteArrayLiteral("also-not-json")));

  const auto result = store.load();

  QVERIFY(!result.ok);
  QCOMPARE(result.source, TrustedDeviceLoadSource::Empty);
  QVERIFY(store.devices().isEmpty());
}

void TrustedDeviceStoreTests::rejectsInvalidRecords()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  auto invalidFingerprint = makeDevice(DeviceId::generate());
  invalidFingerprint.fingerprintSha256 = QByteArray(31, '\x2a');
  QString diagnostic;
  QVERIFY(!store.upsert(invalidFingerprint, &diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("SHA-256")));

  auto invalidAddress = makeDevice(DeviceId::generate());
  invalidAddress.lastAddresses = {QStringLiteral("not an address")};
  QVERIFY(!store.upsert(invalidAddress, &diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("address")));
}

void TrustedDeviceStoreTests::rejectsDuplicateDeviceIds()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("trusted.json"));
  const auto id = DeviceId::generate();
  const QJsonObject entry{
      {QStringLiteral("deviceId"), id.toString()},
      {QStringLiteral("alias"), QStringLiteral("Device")},
      {QStringLiteral("platform"), QStringLiteral("windows")},
      {QStringLiteral("fingerprintSha256"), QString::fromLatin1(QByteArray(32, '\x4c').toBase64())},
      {QStringLiteral("lastAddresses"), QJsonArray{QStringLiteral("192.168.1.2")}},
      {QStringLiteral("autoAcceptFiles"), false},
      {QStringLiteral("revoked"), false},
  };
  const QByteArray duplicateStore = QJsonDocument(QJsonObject{
                                                      {QStringLiteral("schemaVersion"), 1},
                                                      {QStringLiteral("devices"), QJsonArray{entry, entry}},
                                                  })
                                        .toJson();
  QVERIFY(overwrite(path, duplicateStore));
  TrustedDeviceStore store(path);

  const auto result = store.load();

  QVERIFY(!result.ok);
  QVERIFY(result.diagnostic.contains(QStringLiteral("duplicate")));
}

QTEST_MAIN(TrustedDeviceStoreTests)

#include "TrustedDeviceStoreTests.moc"
