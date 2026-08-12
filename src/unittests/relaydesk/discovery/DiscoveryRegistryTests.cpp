/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/discovery/DiscoveryService.h"

#include <QSignalSpy>
#include <QTest>

#include <chrono>

using namespace deskflow::relaydesk;
using namespace std::chrono_literals;

namespace {
DeviceInfo deviceInfo(DeviceId id, QString displayName = QStringLiteral("Remote workstation"))
{
  return {
      .deviceId = std::move(id),
      .displayName = std::move(displayName),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("1.26.0-relaydesk.1"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, '\x55'),
  };
}
} // namespace

class DiscoveryRegistryTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void ignoresLocalAndInvalidSenderAdvertisements();
  void deduplicatesByDeviceIdAndUpdatesAddressCandidates();
  void sameDisplayNameDoesNotMergeDifferentDevices();
  void expiresAtTtlAndDoesNotRepeatOfflineSignal();
  void offlineDeviceCanBeDiscoveredAgain();
  void advertisedFingerprintDoesNotBecomePinnedTrust();
  void serviceLoopbackFeedsRegistryAndOwnPacketIsIgnored();
};

void DiscoveryRegistryTests::ignoresLocalAndInvalidSenderAdvertisements()
{
  const auto localId = DeviceId::generate();
  DiscoveryRegistry registry(localId);
  QSignalSpy added(&registry, &DiscoveryRegistry::deviceAdded);

  QVERIFY(!registry.observeAdvertisement(deviceInfo(localId), QHostAddress(QStringLiteral("192.168.1.10"))));
  QVERIFY(!registry.observeAdvertisement(deviceInfo(DeviceId::generate()), QHostAddress()));
  QVERIFY(!registry.observeAdvertisement(deviceInfo(DeviceId::generate()), QHostAddress(QStringLiteral("::1"))));
  QVERIFY(registry.snapshots().isEmpty());
  QCOMPARE(added.size(), 0);
}

void DiscoveryRegistryTests::deduplicatesByDeviceIdAndUpdatesAddressCandidates()
{
  auto now = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate);
  DiscoveryRegistry registry(DeviceId::generate(), 15s, [&now]() { return now; });
  QSignalSpy added(&registry, &DiscoveryRegistry::deviceAdded);
  QSignalSpy changed(&registry, &DiscoveryRegistry::deviceChanged);
  const auto remoteId = DeviceId::generate();

  QVERIFY(registry.observeAdvertisement(deviceInfo(remoteId), QHostAddress(QStringLiteral("192.168.1.20"))));
  now = now.addSecs(1);
  auto updated = deviceInfo(remoteId, QStringLiteral("Renamed workstation"));
  updated.capabilities.folderV1 = true;
  QVERIFY(registry.observeAdvertisement(updated, QHostAddress(QStringLiteral("10.0.0.20"))));

  QCOMPARE(added.size(), 1);
  QCOMPARE(changed.size(), 1);
  QCOMPARE(registry.snapshots().size(), 1);
  const auto snapshot = registry.snapshot(remoteId);
  QVERIFY(snapshot.has_value());
  QCOMPARE(snapshot->displayName, QStringLiteral("Renamed workstation"));
  QCOMPARE(snapshot->presence, DevicePresence::Discovered);
  QCOMPARE(snapshot->addresses, QList<QHostAddress>({
                                   QHostAddress(QStringLiteral("10.0.0.20")),
                                   QHostAddress(QStringLiteral("192.168.1.20")),
                               }));
  QVERIFY(snapshot->capabilities.folderV1);

  for (int index = 1; index <= 12; ++index) {
    QVERIFY(registry.observeAdvertisement(
        updated, QHostAddress(QStringLiteral("172.16.0.%1").arg(index))
    ));
  }
  const auto bounded = registry.snapshot(remoteId);
  QCOMPARE(bounded->addresses.size(), kMaximumDiscoveryAddressesPerDevice);
  QCOMPARE(bounded->addresses.first(), QHostAddress(QStringLiteral("172.16.0.12")));
}

void DiscoveryRegistryTests::sameDisplayNameDoesNotMergeDifferentDevices()
{
  DiscoveryRegistry registry(DeviceId::generate());
  const auto first = DeviceId::generate();
  const auto second = DeviceId::generate();

  QVERIFY(registry.observeAdvertisement(deviceInfo(first, QStringLiteral("Shared name")),
                                        QHostAddress(QStringLiteral("192.168.1.21"))));
  QVERIFY(registry.observeAdvertisement(deviceInfo(second, QStringLiteral("Shared name")),
                                        QHostAddress(QStringLiteral("192.168.1.22"))));
  QCOMPARE(registry.snapshots().size(), 2);
  QVERIFY(registry.snapshot(first).has_value());
  QVERIFY(registry.snapshot(second).has_value());
}

void DiscoveryRegistryTests::expiresAtTtlAndDoesNotRepeatOfflineSignal()
{
  auto now = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate);
  DiscoveryRegistry registry(DeviceId::generate(), 15s, [&now]() { return now; });
  const auto remoteId = DeviceId::generate();
  QVERIFY(registry.observeAdvertisement(deviceInfo(remoteId), QHostAddress(QStringLiteral("192.168.1.20"))));
  QSignalSpy changed(&registry, &DiscoveryRegistry::deviceChanged);

  now = now.addMSecs(14999);
  registry.expireStaleDevices();
  QCOMPARE(registry.snapshot(remoteId)->presence, DevicePresence::Discovered);
  QCOMPARE(changed.size(), 0);

  now = now.addMSecs(1);
  registry.expireStaleDevices();
  QCOMPARE(registry.snapshot(remoteId)->presence, DevicePresence::Offline);
  QCOMPARE(changed.size(), 1);

  now = now.addSecs(30);
  registry.expireStaleDevices();
  QCOMPARE(changed.size(), 1);
}

void DiscoveryRegistryTests::offlineDeviceCanBeDiscoveredAgain()
{
  auto now = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate);
  DiscoveryRegistry registry(DeviceId::generate(), 15s, [&now]() { return now; });
  const auto remoteId = DeviceId::generate();
  QVERIFY(registry.observeAdvertisement(deviceInfo(remoteId), QHostAddress(QStringLiteral("192.168.1.20"))));
  const auto firstSeen = registry.snapshot(remoteId)->lastSeenUtc;

  now = now.addSecs(15);
  registry.expireStaleDevices();
  QCOMPARE(registry.snapshot(remoteId)->presence, DevicePresence::Offline);

  now = now.addSecs(2);
  QVERIFY(registry.observeAdvertisement(deviceInfo(remoteId), QHostAddress(QStringLiteral("192.168.2.20"))));
  const auto rediscovered = registry.snapshot(remoteId);
  QCOMPARE(rediscovered->presence, DevicePresence::Discovered);
  QCOMPARE(rediscovered->addresses.first(), QHostAddress(QStringLiteral("192.168.2.20")));
  QVERIFY(rediscovered->lastSeenUtc > firstSeen);
}

void DiscoveryRegistryTests::advertisedFingerprintDoesNotBecomePinnedTrust()
{
  DiscoveryRegistry registry(DeviceId::generate());
  const auto remoteId = DeviceId::generate();
  const auto advertised = deviceInfo(remoteId);
  QVERIFY(registry.observeAdvertisement(advertised, QHostAddress(QStringLiteral("192.168.1.20"))));

  const auto snapshot = registry.snapshot(remoteId);
  QVERIFY(snapshot.has_value());
  QVERIFY(!snapshot->trusted);
  QVERIFY(snapshot->pinnedFingerprint.isEmpty());
  QCOMPARE(registry.deviceInfo(remoteId)->certificateFingerprintSha256,
           advertised.certificateFingerprintSha256);
}

void DiscoveryRegistryTests::serviceLoopbackFeedsRegistryAndOwnPacketIsIgnored()
{
  const auto local = deviceInfo(DeviceId::generate(), QStringLiteral("Local workstation"));
  const auto peer = deviceInfo(DeviceId::generate(), QStringLiteral("Peer workstation"));
  DiscoveryRegistry registry(local.deviceId);
  DiscoveryService service(
      local, {.port = 0, .announcementIntervalMs = 60000}, []() { return QList<DiscoveryInterface>{}; }
  );
  connect(
      &service, &DiscoveryService::advertisementReceived, &registry,
      [&registry](const DeviceInfo &device, const QHostAddress &senderAddress) {
        static_cast<void>(registry.observeAdvertisement(device, senderAddress));
      }
  );
  QVERIFY(service.start());

  QUdpSocket sender;
  const auto localDatagram = DiscoveryCodec::encodeAdvertisement(local);
  const auto peerDatagram = DiscoveryCodec::encodeAdvertisement(peer);
  QCOMPARE(
      sender.writeDatagram(localDatagram, QHostAddress::LocalHost, service.boundPort()), qint64(localDatagram.size())
  );
  QCOMPARE(
      sender.writeDatagram(peerDatagram, QHostAddress::LocalHost, service.boundPort()), qint64(peerDatagram.size())
  );

  QTRY_COMPARE_WITH_TIMEOUT(registry.snapshots().size(), 1, 2000);
  QVERIFY(!registry.snapshot(local.deviceId).has_value());
  QVERIFY(registry.snapshot(peer.deviceId).has_value());
}

QTEST_MAIN(DiscoveryRegistryTests)

#include "DiscoveryRegistryTests.moc"
