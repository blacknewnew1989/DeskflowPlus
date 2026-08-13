/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "../TestTlsIdentity.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {
DeviceInfo localDevice(DeviceId id, QByteArray fingerprint, QString name)
{
  return {
      .deviceId = std::move(id),
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .capabilities = {.input = true, .clipboardText = true},
      .certificateFingerprintSha256 = std::move(fingerprint),
  };
}

TrustedDevice trustedDevice(DeviceId id, QByteArray fingerprint)
{
  return {
      .deviceId = std::move(id),
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = std::move(fingerprint),
  };
}
} // namespace

class FileTransferRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void listenerLifecycleIsOwnedAndRestartable();
  void trustedPeersNegotiateIndependentFileChannel();
  void routesPostCapabilityFramesAfterPinning();
  void invalidIdentityFailsWithoutPublishingAListener();
};

void FileTransferRuntimeTests::listenerLifecycleIsOwnedAndRestartable()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  QVERIFY(!identityPath.isEmpty());

  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x11'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(localId, trust, discovery, identityPath, options);
  QSignalSpy started(&runtime, &FileTransferRuntime::started);
  QSignalSpy stopped(&runtime, &FileTransferRuntime::stopped);
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  const quint16 firstPort = runtime.listeningPort();
  QVERIFY(firstPort != 0);
  QCOMPARE(discovery.service().localDevice().filePort, firstPort);
  QVERIFY(discovery.service().localDevice().capabilities.fileV1);
  QVERIFY(discovery.service().localDevice().capabilities.folderV1);
  QVERIFY(discovery.service().localDevice().capabilities.resumeV1);
  QCOMPARE(started.count(), 1);
  QVERIFY(runtime.start(&diagnostic));
  QCOMPARE(runtime.listeningPort(), firstPort);
  QCOMPARE(started.count(), 1);

  runtime.stop();
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(discovery.service().localDevice().filePort, quint16{0});
  QVERIFY(!discovery.service().localDevice().capabilities.fileV1);
  QCOMPARE(stopped.count(), 1);
  QTcpServer releasedPortProbe;
  QVERIFY(releasedPortProbe.listen(QHostAddress::LocalHost, firstPort));
  releasedPortProbe.close();

  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  QCOMPARE(started.count(), 2);
  QCOMPARE(errors.count(), 0);
}

void FileTransferRuntimeTests::trustedPeersNegotiateIndependentFileChannel()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  model::DeviceHomeModel firstModel;
  model::DeviceHomeModel secondModel;
  DeviceDiscoveryRuntime firstDiscovery(
      localDevice(firstId, identity.fingerprintSha256, QStringLiteral("First")), firstModel
  );
  DeviceDiscoveryRuntime secondDiscovery(
      localDevice(secondId, identity.fingerprintSha256, QStringLiteral("Second")), secondModel
  );
  TrustedDeviceStore firstTrust(directory.filePath(QStringLiteral("first-trust.json")));
  TrustedDeviceStore secondTrust(directory.filePath(QStringLiteral("second-trust.json")));
  QVERIFY(firstTrust.upsert(trustedDevice(secondId, identity.fingerprintSha256)));
  QVERIFY(secondTrust.upsert(trustedDevice(firstId, identity.fingerprintSha256)));

  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime first(firstId, firstTrust, firstDiscovery, identityPath, options);
  FileTransferRuntime second(secondId, secondTrust, secondDiscovery, identityPath, options);
  QString diagnostic;
  QVERIFY2(first.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(second.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(firstDiscovery.registry().observeAdvertisement(
      secondDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  bool firstReady = false;
  bool secondReady = false;
  QStringList errors;
  connect(&first, &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
    if (peer == secondId) {
      firstReady = true;
    }
  });
  connect(&second, &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
    if (peer == firstId) {
      secondReady = true;
    }
  });
  connect(&first, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  connect(&second, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });

  QVERIFY2(first.connectPeer(secondId, &diagnostic), qPrintable(diagnostic));
  QTRY_VERIFY2_WITH_TIMEOUT(firstReady && secondReady, qPrintable(errors.join(QStringLiteral("; "))), 5'000);
  QVERIFY(first.isPeerReady(secondId));
  QVERIFY(second.isPeerReady(firstId));
  const auto negotiated = first.negotiatedCapabilities(secondId);
  QVERIFY(negotiated.has_value());
  QVERIFY(negotiated->features.contains(QStringLiteral("file.v1")));
  QVERIFY(negotiated->features.contains(QStringLiteral("sha256")));
}

void FileTransferRuntimeTests::routesPostCapabilityFramesAfterPinning()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto serverId = DeviceId::generate();
  const auto clientId = DeviceId::generate();
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(serverId, identity.fingerprintSha256, QStringLiteral("Server")), deviceModel
  );
  TrustedDeviceStore serverTrust(directory.filePath(QStringLiteral("server-trust.json")));
  TrustedDeviceStore clientTrust(directory.filePath(QStringLiteral("client-trust.json")));
  QVERIFY(serverTrust.upsert(trustedDevice(clientId, identity.fingerprintSha256)));
  QVERIFY(clientTrust.upsert(trustedDevice(serverId, identity.fingerprintSha256)));

  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(serverId, serverTrust, discovery, identityPath, options);
  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));

  bool ready = false;
  std::optional<Frame> routed;
  QStringList errors;
  connect(&runtime, &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
    ready = peer == clientId;
  });
  connect(&runtime, &FileTransferRuntime::protocolFrameReceived, this, [&](DeviceId peer, Frame frame) {
    if (peer == clientId) {
      routed = std::move(frame);
    }
  });
  connect(&runtime, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });

  FileTlsClient client(clientId, &clientTrust, identityPath);
  bool capabilitiesSent = false;
  connect(&client, &FileTlsClient::connectionCreated, this, [&](FileTlsConnection *connection) {
    connect(connection, &FileTlsConnection::authenticated, this, [&, connection]() {
      Frame capabilities{
          .type = MessageType::Capabilities,
          .metadata = CapabilityCodec::encode(options.localCapabilities),
      };
      capabilitiesSent = connection->sendFrame(capabilities, &diagnostic) == FileTlsError::None;
    });
  });
  QCOMPARE(client.connectToHost(QHostAddress::LocalHost, runtime.listeningPort()), FileTlsError::None);
  QTRY_VERIFY2_WITH_TIMEOUT(
      ready && capabilitiesSent, qPrintable(errors.join(QStringLiteral("; "))), 5'000
  );

  const Frame heartbeat{.type = MessageType::Heartbeat, .streamId = 73};
  QCOMPARE(client.connection()->sendFrame(heartbeat, &diagnostic), FileTlsError::None);
  QTRY_VERIFY2_WITH_TIMEOUT(routed.has_value(), qPrintable(errors.join(QStringLiteral("; "))), 2'000);
  QCOMPARE(*routed, heartbeat);
}

void FileTransferRuntimeTests::invalidIdentityFailsWithoutPublishingAListener()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x22'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(
      localId, trust, discovery, directory.filePath(QStringLiteral("missing.pem")), options
  );
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY(!runtime.start(&diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(errors.count(), 1);
  QCOMPARE(
      qvariant_cast<FileTransferRuntimeError>(errors.first().at(0)),
      FileTransferRuntimeError::ListenerFailed
  );
}

QTEST_MAIN(FileTransferRuntimeTests)

#include "FileTransferRuntimeTests.moc"
