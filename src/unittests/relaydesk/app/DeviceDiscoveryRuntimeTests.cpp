/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/DeviceDiscoveryRuntime.h"

#include "relaydesk/discovery/DiscoveryCodec.h"
#include "relaydesk/model/DeviceHomeModel.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QUdpSocket>

#include <utility>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {
DeviceInfo device(QString name)
{
  return {
      .deviceId = DeviceId::generate(),
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("1.26.0-relaydesk.1"),
      .inputPort = 24800,
      .capabilities = {.input = true, .clipboardText = true},
  };
}

DeviceDiscoveryRuntimeOptions loopbackOptions()
{
  return {
      .serviceSettings = {.port = 0, .announcementIntervalMs = 60000},
      .interfaceProvider = []() { return QList<DiscoveryInterface>{}; },
  };
}
} // namespace

class DeviceDiscoveryRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void realLoopbackAdvertisementDrivesExistingDeviceModel();
  void updateAndExpiryFlowThroughSameModel();
  void destructionStopsListenerAndReleasesPort();
};

void DeviceDiscoveryRuntimeTests::realLoopbackAdvertisementDrivesExistingDeviceModel()
{
  DeviceHomeModel model;
  const auto local = device(QStringLiteral("Local Windows"));
  DeviceDiscoveryRuntime runtime(local, model, loopbackOptions());

  QCOMPARE(runtime.thread(), QThread::currentThread());
  QCOMPARE(runtime.service().thread(), runtime.thread());
  QCOMPARE(runtime.registry().thread(), runtime.thread());
  QCOMPARE(model.thread(), runtime.thread());
  QCOMPARE(model.rowCount(), 1);
  const auto localSnapshot = model.snapshot(local.deviceId);
  QVERIFY(localSnapshot.has_value());
  QCOMPARE(localSnapshot->presence, DevicePresence::Online);

  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());

  const auto peer = device(QStringLiteral("工作室 Mac \U0001f5a5"));
  const auto encoded = DiscoveryCodec::encodeAdvertisement(peer);
  QUdpSocket sender;
  QCOMPARE(
      sender.writeDatagram(encoded, QHostAddress::LocalHost, runtime.service().boundPort()),
      qint64(encoded.size())
  );

  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 2000);
  const auto peerSnapshot = model.snapshot(peer.deviceId);
  QVERIFY(peerSnapshot.has_value());
  QCOMPARE(peerSnapshot->displayName, peer.displayName);
  QCOMPARE(peerSnapshot->addresses, QList<QHostAddress>{QHostAddress(QHostAddress::LocalHost)});
  QCOMPARE(peerSnapshot->presence, DevicePresence::Discovered);
}

void DeviceDiscoveryRuntimeTests::updateAndExpiryFlowThroughSameModel()
{
  DeviceHomeModel model;
  const auto local = device(QStringLiteral("Local"));
  DeviceDiscoveryRuntime runtime(
      local, model,
      {
          .serviceSettings = {.port = 0, .announcementIntervalMs = 60000},
          .registryTtl = std::chrono::milliseconds(100),
          .interfaceProvider = []() { return QList<DiscoveryInterface>{}; },
      }
  );

  auto peer = device(QStringLiteral("First name"));
  QVERIFY(runtime.registry().observeAdvertisement(peer, QHostAddress::LocalHost));
  QCOMPARE(model.snapshot(peer.deviceId)->displayName, peer.displayName);

  peer.displayName = QStringLiteral("Renamed peer");
  QVERIFY(runtime.registry().observeAdvertisement(peer, QHostAddress(QStringLiteral("127.0.0.2"))));
  QCOMPARE(model.snapshot(peer.deviceId)->displayName, peer.displayName);
  QCOMPARE(model.snapshot(peer.deviceId)->addresses.size(), 2);

  QTest::qWait(120);
  runtime.registry().expireStaleDevices();
  QCOMPARE(model.snapshot(peer.deviceId)->presence, DevicePresence::Offline);
}

void DeviceDiscoveryRuntimeTests::destructionStopsListenerAndReleasesPort()
{
  DeviceHomeModel model;
  quint16 port = 0;
  {
    DeviceDiscoveryRuntime runtime(device(QStringLiteral("Local")), model, loopbackOptions());
    QVERIFY(runtime.start());
    port = runtime.service().boundPort();
    QVERIFY(port != 0);
  }

  QUdpSocket replacement;
  QVERIFY2(
      replacement.bind(QHostAddress::AnyIPv4, port, QUdpSocket::DontShareAddress),
      qPrintable(replacement.errorString())
  );
}

QTEST_MAIN(DeviceDiscoveryRuntimeTests)

#include "DeviceDiscoveryRuntimeTests.moc"
