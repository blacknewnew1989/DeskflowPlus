/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/DeviceHomeModel.h"

#include <QAbstractItemModelTester>
#include <QDateTime>
#include <QHostAddress>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {

DeviceId deviceId(const char *uuid)
{
  const auto parsed = DeviceId::fromString(QString::fromLatin1(uuid));
  Q_ASSERT(parsed.has_value());
  return *parsed;
}

DeviceSnapshot makeSnapshot(
    const char *uuid, const QString &name, DevicePresence presence, bool trusted = false,
    const QString &alias = QString()
)
{
  return DeviceSnapshot{
      .id = deviceId(uuid),
      .displayName = name,
      .alias = alias,
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = presence,
      .trusted = trusted,
      .autoAcceptFiles = trusted,
      .latencyMs = presence == DevicePresence::Online ? 3 : -1,
      .addresses = {QHostAddress(QStringLiteral("192.168.1.20"))},
      .capabilities =
          {
              .input = true,
              .clipboardText = true,
              .clipboardImage = false,
              .fileV1 = true,
              .folderV1 = true,
              .resumeV1 = false,
          },
      .pinnedFingerprint = trusted ? QByteArray(32, '\x2a') : QByteArray(),
      .lastSeenUtc = QDateTime::fromString(QStringLiteral("2026-08-12T12:30:00Z"), Qt::ISODate),
  };
}

QString idAt(const DeviceHomeModel &model, int row)
{
  return model.data(model.index(row, 0), DeviceHomeModel::DeviceIdRole).toString();
}

} // namespace

class DeviceHomeModelTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void exposesStableRolesAndCardData();
  void insertsUpdatesAndRemovesByDeviceId();
  void keepsLocalFirstAndOrdersRemoteStatesDeterministically();
  void movesRowsWhenAnUpdateChangesSortPosition();
  void preservesTrustedDevicesWhenTheyGoOffline();
  void replacingLocalDeviceDoesNotLeaveASecondLocalRow();
};

void DeviceHomeModelTests::exposesStableRolesAndCardData()
{
  DeviceHomeModel model;
  QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
  const auto local = makeSnapshot(
      "10000000-0000-0000-0000-000000000001", QStringLiteral("Workstation"), DevicePresence::Online, true,
      QStringLiteral("My PC")
  );
  model.setLocalDevice(local);

  QCOMPARE(model.rowCount(), 1);
  const auto index = model.index(0, 0);
  QCOMPARE(model.data(index, Qt::DisplayRole).toString(), QStringLiteral("My PC"));
  QCOMPARE(model.data(index, DeviceHomeModel::DisplayNameRole).toString(), QStringLiteral("My PC"));
  QCOMPARE(model.data(index, DeviceHomeModel::ReportedNameRole).toString(), QStringLiteral("Workstation"));
  QCOMPARE(model.data(index, DeviceHomeModel::StatusTextRole).toString(), QStringLiteral("Online"));
  QVERIFY(model.data(index, DeviceHomeModel::IsLocalRole).toBool());
  QVERIFY(model.data(index, DeviceHomeModel::IsOnlineRole).toBool());
  QVERIFY(model.data(index, DeviceHomeModel::IsTrustedRole).toBool());
  QVERIFY(!model.data(index, DeviceHomeModel::IsPairingRole).toBool());
  QVERIFY(model.data(index, DeviceHomeModel::InputCapabilityRole).toBool());
  QVERIFY(model.data(index, DeviceHomeModel::FileCapabilityRole).toBool());
  QVERIFY(model.data(index, DeviceHomeModel::FolderCapabilityRole).toBool());
  QVERIFY(!model.data(index, DeviceHomeModel::ClipboardImageCapabilityRole).toBool());
  QCOMPARE(
      model.data(index, DeviceHomeModel::AddressesRole).toStringList(), QStringList{QStringLiteral("192.168.1.20")}
  );

  const auto roles = model.roleNames();
  QCOMPARE(roles.value(DeviceHomeModel::DeviceIdRole), QByteArray("deviceId"));
  QCOMPARE(roles.value(DeviceHomeModel::PresenceRole), QByteArray("presence"));
  QCOMPARE(roles.value(DeviceHomeModel::StatusTextRole), QByteArray("statusText"));
  QCOMPARE(roles.value(DeviceHomeModel::FileCapabilityRole), QByteArray("canSendFiles"));
  QCOMPARE(QSet<QByteArray>(roles.cbegin(), roles.cend()).size(), roles.size());
}

void DeviceHomeModelTests::insertsUpdatesAndRemovesByDeviceId()
{
  DeviceHomeModel model;
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

  auto peer =
      makeSnapshot("20000000-0000-0000-0000-000000000001", QStringLiteral("Studio Mac"), DevicePresence::Discovered);
  model.upsertRemoteDevice(peer);
  QCOMPARE(inserted.count(), 1);
  QCOMPARE(model.rowCount(), 1);

  peer.alias = QStringLiteral("Design Mac");
  peer.presence = DevicePresence::Pairing;
  model.upsertRemoteDevice(peer);
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(inserted.count(), 1);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.data(model.index(0, 0), DeviceHomeModel::DisplayNameRole).toString(), QStringLiteral("Design Mac"));
  QVERIFY(model.data(model.index(0, 0), DeviceHomeModel::IsPairingRole).toBool());

  const auto stored = model.snapshot(peer.id);
  QVERIFY(stored.has_value());
  QCOMPARE(stored->alias, QStringLiteral("Design Mac"));
  QVERIFY(model.removeDevice(peer.id));
  QCOMPARE(removed.count(), 1);
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(!model.snapshot(peer.id).has_value());
  QVERIFY(!model.removeDevice(peer.id));
}

void DeviceHomeModelTests::keepsLocalFirstAndOrdersRemoteStatesDeterministically()
{
  DeviceHomeModel model;
  const auto local =
      makeSnapshot("30000000-0000-0000-0000-000000000001", QStringLiteral("Local"), DevicePresence::Offline, true);
  const auto offline =
      makeSnapshot("30000000-0000-0000-0000-000000000002", QStringLiteral("Offline"), DevicePresence::Offline, true);
  const auto discovered =
      makeSnapshot("30000000-0000-0000-0000-000000000003", QStringLiteral("Discovered"), DevicePresence::Discovered);
  const auto pairing =
      makeSnapshot("30000000-0000-0000-0000-000000000004", QStringLiteral("Pairing"), DevicePresence::Pairing);
  const auto onlineZulu =
      makeSnapshot("30000000-0000-0000-0000-000000000005", QStringLiteral("zulu"), DevicePresence::Online, true);
  const auto onlineAlpha =
      makeSnapshot("30000000-0000-0000-0000-000000000006", QStringLiteral("Alpha"), DevicePresence::Online, true);
  const auto violation = makeSnapshot(
      "30000000-0000-0000-0000-000000000007", QStringLiteral("Changed"), DevicePresence::TrustViolation, true
  );

  model.upsertRemoteDevice(offline);
  model.upsertRemoteDevice(onlineZulu);
  model.upsertRemoteDevice(discovered);
  model.upsertRemoteDevice(violation);
  model.upsertRemoteDevice(pairing);
  model.setLocalDevice(local);
  model.upsertRemoteDevice(onlineAlpha);

  const QStringList expected{
      local.id.toString(),      onlineAlpha.id.toString(), onlineZulu.id.toString(), pairing.id.toString(),
      discovered.id.toString(), violation.id.toString(),   offline.id.toString(),
  };
  QStringList actual;
  for (int row = 0; row < model.rowCount(); ++row)
    actual.append(idAt(model, row));
  QCOMPARE(actual, expected);
}

void DeviceHomeModelTests::movesRowsWhenAnUpdateChangesSortPosition()
{
  DeviceHomeModel model;
  QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
  auto zulu =
      makeSnapshot("40000000-0000-0000-0000-000000000001", QStringLiteral("Zulu"), DevicePresence::Online, true);
  const auto alpha =
      makeSnapshot("40000000-0000-0000-0000-000000000002", QStringLiteral("Alpha"), DevicePresence::Offline, true);
  model.upsertRemoteDevice(zulu);
  model.upsertRemoteDevice(alpha);
  QCOMPARE(idAt(model, 0), zulu.id.toString());

  QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  zulu.presence = DevicePresence::Offline;
  model.upsertRemoteDevice(zulu);

  QCOMPARE(moved.count(), 1);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(idAt(model, 0), alpha.id.toString());
  QCOMPARE(idAt(model, 1), zulu.id.toString());
}

void DeviceHomeModelTests::preservesTrustedDevicesWhenTheyGoOffline()
{
  DeviceHomeModel model;
  auto peer =
      makeSnapshot("50000000-0000-0000-0000-000000000001", QStringLiteral("Trusted Mac"), DevicePresence::Online, true);
  model.upsertRemoteDevice(peer);

  peer.presence = DevicePresence::Offline;
  peer.latencyMs = -1;
  peer.addresses.clear();
  peer.lastSeenUtc = QDateTime::fromString(QStringLiteral("2026-08-12T12:35:00Z"), Qt::ISODate);
  model.upsertRemoteDevice(peer);

  QCOMPARE(model.rowCount(), 1);
  const auto index = model.index(0, 0);
  QVERIFY(model.data(index, DeviceHomeModel::IsTrustedRole).toBool());
  QVERIFY(!model.data(index, DeviceHomeModel::IsOnlineRole).toBool());
  QCOMPARE(model.data(index, DeviceHomeModel::StatusTextRole).toString(), QStringLiteral("Offline"));
  QCOMPARE(model.data(index, DeviceHomeModel::LatencyMsRole).toInt(), -1);
  QVERIFY(model.data(index, DeviceHomeModel::AddressesRole).toStringList().isEmpty());
  QCOMPARE(model.data(index, DeviceHomeModel::LastSeenUtcRole).toDateTime(), peer.lastSeenUtc);
}

void DeviceHomeModelTests::replacingLocalDeviceDoesNotLeaveASecondLocalRow()
{
  DeviceHomeModel model;
  const auto oldLocal =
      makeSnapshot("60000000-0000-0000-0000-000000000001", QStringLiteral("Old local"), DevicePresence::Online, true);
  const auto newLocal =
      makeSnapshot("60000000-0000-0000-0000-000000000002", QStringLiteral("New local"), DevicePresence::Online, true);
  const auto remote =
      makeSnapshot("60000000-0000-0000-0000-000000000003", QStringLiteral("Peer"), DevicePresence::Online, true);
  model.setLocalDevice(oldLocal);
  model.upsertRemoteDevice(remote);

  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  model.setLocalDevice(newLocal);

  QCOMPARE(reset.count(), 1);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(idAt(model, 0), newLocal.id.toString());
  QVERIFY(model.data(model.index(0, 0), DeviceHomeModel::IsLocalRole).toBool());
  QVERIFY(!model.snapshot(oldLocal.id).has_value());
  QVERIFY(!model.data(model.index(1, 0), DeviceHomeModel::IsLocalRole).toBool());
}

QTEST_MAIN(DeviceHomeModelTests)

#include "DeviceHomeModelTests.moc"
