/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/device/DeviceSnapshot.h"

#include <QMetaType>
#include <QTest>

using namespace deskflow::relaydesk;

class DeviceSnapshotTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void defaultsMatchSharedContract();
  void snapshotIsAnIndependentCopy();
  void typesAreRegisteredForQueuedSignals();
};

void DeviceSnapshotTests::defaultsMatchSharedContract()
{
  const DeviceSnapshot snapshot{
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = DevicePresence::Discovered,
  };

  QVERIFY(!snapshot.trusted);
  QVERIFY(!snapshot.autoAcceptFiles);
  QCOMPARE(snapshot.latencyMs, -1);
  QVERIFY(snapshot.addresses.isEmpty());
  QVERIFY(snapshot.pinnedFingerprint.isEmpty());
  QVERIFY(!snapshot.lastSeenUtc.isValid());
}

void DeviceSnapshotTests::snapshotIsAnIndependentCopy()
{
  const auto timestamp = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate);
  DeviceSnapshot original{
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Office PC"),
      .alias = QStringLiteral("Primary workstation"),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .presence = DevicePresence::Online,
      .trusted = true,
      .autoAcceptFiles = true,
      .latencyMs = 4,
      .addresses = {QHostAddress(QStringLiteral("192.168.1.20"))},
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
      .pinnedFingerprint = QByteArray(32, '\x42'),
      .lastSeenUtc = timestamp,
  };
  auto copy = original;
  copy.displayName = QStringLiteral("Changed UI copy");
  copy.addresses.append(QHostAddress(QStringLiteral("10.0.0.20")));
  copy.pinnedFingerprint[0] = '\x11';

  QCOMPARE(original.displayName, QStringLiteral("Office PC"));
  QCOMPARE(original.addresses.size(), 1);
  QCOMPARE(original.pinnedFingerprint.at(0), '\x42');
  QVERIFY(copy != original);
}

void DeviceSnapshotTests::typesAreRegisteredForQueuedSignals()
{
  QVERIFY(QMetaType::fromType<DevicePresence>().isValid());
  QVERIFY(QMetaType::fromType<DeviceSnapshot>().isValid());
}

QTEST_MAIN(DeviceSnapshotTests)

#include "DeviceSnapshotTests.moc"
