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
      .serviceSettings = {.port = 0, .announcementIntervalMs = 60000, .broadcastsEnabled = false},
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
  void publishesNamedFileEndpointAnnouncement();
  void manualLoopbackProbeDiscoversUnpairedPeer();
  void manualProbeWorksWhenBroadcastDiscoveryIsDisabled();
  void ipv6ManualCandidatesAreDiagnosedAndIpv4FallbackWorks();
  void invalidAndStaleManualResolutionDoNotFabricatePeers();
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

void DeviceDiscoveryRuntimeTests::publishesNamedFileEndpointAnnouncement()
{
  DeviceHomeModel model;
  DeviceDiscoveryRuntime runtime(device(QStringLiteral("Local")), model, loopbackOptions());
  const FileEndpointAnnouncement announcement{
      .port = 24801,
      .fileV1 = true,
      .folderV1 = true,
      .resumeV1 = false,
  };

  QString diagnostic;
  QVERIFY2(runtime.setFileEndpoint(announcement, &diagnostic), qPrintable(diagnostic));
  QCOMPARE(runtime.service().localDevice().filePort, announcement.port);
  QCOMPARE(runtime.service().localDevice().capabilities.fileV1, announcement.fileV1);
  QCOMPARE(runtime.service().localDevice().capabilities.folderV1, announcement.folderV1);
  QCOMPARE(runtime.service().localDevice().capabilities.resumeV1, announcement.resumeV1);
}

void DeviceDiscoveryRuntimeTests::manualLoopbackProbeDiscoversUnpairedPeer()
{
  DeviceHomeModel peerModel;
  DeviceDiscoveryRuntime peer(device(QStringLiteral("Manual peer")), peerModel, loopbackOptions());
  QVERIFY(peer.start());

  DeviceHomeModel localModel;
  auto options = loopbackOptions();
  options.manualAddresses = {*parseManualAddress(QStringLiteral("127.0.0.1"))};
  options.manualProbePort = peer.service().boundPort();
  DeviceDiscoveryRuntime local(device(QStringLiteral("Manual source")), localModel, std::move(options));
  QVERIFY(local.start());

  QTRY_COMPARE_WITH_TIMEOUT(localModel.rowCount(), 2, 2000);
  QVERIFY(localModel.snapshot(peer.service().localDevice().deviceId).has_value());
}

void DeviceDiscoveryRuntimeTests::manualProbeWorksWhenBroadcastDiscoveryIsDisabled()
{
  DeviceHomeModel peerModel;
  DeviceDiscoveryRuntime peer(device(QStringLiteral("Manual peer")), peerModel, loopbackOptions());
  QVERIFY(peer.start());

  int broadcasts = 0;
  DeviceHomeModel localModel;
  auto options = loopbackOptions();
  options.manualAddresses = {*parseManualAddress(QStringLiteral("127.0.0.1"))};
  options.manualProbePort = peer.service().boundPort();
  options.interfaceProvider = []() {
    return QList{DiscoveryInterface{
        .name = QStringLiteral("ethernet"),
        .address = QHostAddress(QStringLiteral("192.168.1.10")),
        .broadcastAddress = QHostAddress(QStringLiteral("192.168.1.255")),
        .isUp = true,
        .isRunning = true,
    }};
  };
  options.datagramSender = [&broadcasts](
                              const QByteArray &payload, const QHostAddress &, quint16, const QHostAddress &, QString *
                          ) {
    ++broadcasts;
    return qint64(payload.size());
  };
  DeviceDiscoveryRuntime local(device(QStringLiteral("Manual source")), localModel, std::move(options));
  QVERIFY(local.start());

  QTRY_COMPARE_WITH_TIMEOUT(localModel.rowCount(), 2, 2000);
  QCOMPARE(broadcasts, 0);
}

void DeviceDiscoveryRuntimeTests::ipv6ManualCandidatesAreDiagnosedAndIpv4FallbackWorks()
{
  DeviceHomeModel peerModel;
  DeviceDiscoveryRuntime peer(device(QStringLiteral("IPv4 peer")), peerModel, loopbackOptions());
  QVERIFY(peer.start());

  DeviceHomeModel localModel;
  auto options = loopbackOptions();
  options.manualProbePort = peer.service().boundPort();
  options.manualHostResolver = [](const QString &host, AddressCandidateProvider::HostResolutionCallback callback) {
    if (host == QStringLiteral("aaaa-only.local")) {
      callback({QHostAddress(QStringLiteral("2001:db8::20"))}, {});
      return;
    }
    if (host == QStringLiteral("dual-stack.local")) {
      callback(
          {QHostAddress(QStringLiteral("2001:db8::21")), QHostAddress::LocalHost}, {}
      );
      return;
    }
    callback({}, QStringLiteral("unexpected host"));
  };
  DeviceDiscoveryRuntime local(device(QStringLiteral("IPv4 source")), localModel, std::move(options));
  QSignalSpy errors(&local, &DeviceDiscoveryRuntime::errorOccurred);
  QVERIFY(local.start());

  local.setManualAddresses({*parseManualAddress(QStringLiteral("2001:db8::10"))});
  QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 2000);
  QCOMPARE(errors.at(0).at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::SendFailed);
  QVERIFY(errors.at(0).at(1).toString().contains(QStringLiteral("IPv6")));
  QCOMPARE(localModel.rowCount(), 1);

  local.setManualAddresses({*parseManualAddress(QStringLiteral("aaaa-only.local"))});
  QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 2000);
  QVERIFY(errors.at(1).at(1).toString().contains(QStringLiteral("aaaa-only.local")));
  QCOMPARE(localModel.rowCount(), 1);

  local.setManualAddresses({*parseManualAddress(QStringLiteral("dual-stack.local"))});
  QTRY_COMPARE_WITH_TIMEOUT(localModel.rowCount(), 2, 2000);
  QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 3, 2000);
  QVERIFY(errors.at(2).at(1).toString().contains(QStringLiteral("dual-stack.local")));
}

void DeviceDiscoveryRuntimeTests::invalidAndStaleManualResolutionDoNotFabricatePeers()
{
  DeviceHomeModel peerModel;
  DeviceDiscoveryRuntime peer(device(QStringLiteral("Resolved peer")), peerModel, loopbackOptions());
  QVERIFY(peer.start());

  QList<AddressCandidateProvider::HostResolutionCallback> callbacks;
  DeviceHomeModel localModel;
  auto options = loopbackOptions();
  options.manualProbePort = peer.service().boundPort();
  options.manualHostResolver = [&callbacks](const QString &, AddressCandidateProvider::HostResolutionCallback callback) {
    callbacks.append(std::move(callback));
  };
  DeviceDiscoveryRuntime local(device(QStringLiteral("Resolved source")), localModel, std::move(options));
  QVERIFY(local.start());

  local.setManualAddresses({ManualAddress{.host = QStringLiteral("first.invalid")}});
  QTRY_COMPARE_WITH_TIMEOUT(callbacks.size(), 1, 1000);
  local.setManualAddresses({ManualAddress{.host = QStringLiteral("second.invalid")}});
  QTRY_COMPARE_WITH_TIMEOUT(callbacks.size(), 2, 1000);
  callbacks.at(0)({QHostAddress::LocalHost}, {});
  QTest::qWait(100);
  QCOMPARE(localModel.rowCount(), 1);

  callbacks.at(1)({QHostAddress::LocalHost}, {});
  QTRY_COMPARE_WITH_TIMEOUT(localModel.rowCount(), 2, 2000);

  local.setManualAddresses({ManualAddress{.host = QStringLiteral("not a valid host!")}});
  QTest::qWait(100);
  QCOMPARE(localModel.rowCount(), 2);
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
