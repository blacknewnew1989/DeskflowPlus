/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/AutoReconnectRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/PairingTrustRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "../TestTlsIdentity.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {

DeviceInfo device(DeviceId id, QByteArray fingerprint, QString displayName)
{
  return {
      .deviceId = std::move(id),
      .displayName = std::move(displayName),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
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

DeviceDiscoveryRuntimeOptions loopbackDiscovery()
{
  return {
      .serviceSettings = {.port = 0, .announcementIntervalMs = 60000},
      .interfaceProvider = []() { return QList<DiscoveryInterface>{}; },
  };
}

} // namespace

class AutoReconnectRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void trustRevocationStopsReconnectAndDisconnectsPeer();
};

void AutoReconnectRuntimeTests::trustRevocationStopsReconnectAndDisconnectsPeer()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  const auto firstInfo = device(firstId, identity.fingerprintSha256, QStringLiteral("First"));
  const auto secondInfo = device(secondId, identity.fingerprintSha256, QStringLiteral("Second"));
  const auto firstTrustPath = directory.filePath(QStringLiteral("first-trusted.json"));
  const auto secondTrustPath = directory.filePath(QStringLiteral("second-trusted.json"));
  TrustedDeviceStore firstSeed(firstTrustPath);
  TrustedDeviceStore secondSeed(secondTrustPath);
  QVERIFY(firstSeed.upsert(trustedDevice(secondId, identity.fingerprintSha256)));
  QVERIFY(secondSeed.upsert(trustedDevice(firstId, identity.fingerprintSha256)));
  QVERIFY(firstSeed.save().ok);
  QVERIFY(secondSeed.save().ok);

  DeviceHomeModel firstModel;
  DeviceHomeModel secondModel;
  PairingWizardModel firstPairingModel;
  PairingWizardModel secondPairingModel;
  DeviceDiscoveryRuntime firstDiscovery(firstInfo, firstModel, loopbackDiscovery());
  DeviceDiscoveryRuntime secondDiscovery(secondInfo, secondModel, loopbackDiscovery());
  QString diagnostic;
  QVERIFY2(firstDiscovery.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(secondDiscovery.start(&diagnostic), qPrintable(diagnostic));
  PairingTrustRuntime firstPairing(
      firstInfo, firstTrustPath, firstDiscovery, firstModel, firstPairingModel
  );
  PairingTrustRuntime secondPairing(
      secondInfo, secondTrustPath, secondDiscovery, secondModel, secondPairingModel
  );
  QVERIFY(firstPairing.isReady());
  QVERIFY(secondPairing.isReady());

  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime firstFiles(firstId, firstPairing.trustedDevices(), firstDiscovery, identityPath, options);
  FileTransferRuntime secondFiles(secondId, secondPairing.trustedDevices(), secondDiscovery, identityPath, options);
  QVERIFY2(firstFiles.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(secondFiles.start(&diagnostic), qPrintable(diagnostic));

  auto directFirst = firstDiscovery.service().localDevice();
  auto directSecond = secondDiscovery.service().localDevice();
  directFirst.filePort = firstFiles.listeningPort();
  directFirst.capabilities.fileV1 = true;
  directSecond.filePort = secondFiles.listeningPort();
  directSecond.capabilities.fileV1 = true;
  QVERIFY(firstDiscovery.registry().observeAdvertisement(directSecond, QHostAddress::LocalHost));
  QVERIFY(secondDiscovery.registry().observeAdvertisement(directFirst, QHostAddress::LocalHost));

  QStringList errors;
  connect(&firstFiles, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  AutoReconnectRuntime reconnect(firstPairing, firstDiscovery, firstFiles, {});
  QTRY_VERIFY2_WITH_TIMEOUT(firstFiles.isPeerReady(secondId), qPrintable(errors.join(QStringLiteral("; "))), 5000);
  QVERIFY(reconnect.m_coordinators.contains(secondId));
  reconnect.m_pending.insert(secondId, [](AutoReconnectConnectResult) {});
  QVERIFY(reconnect.m_pending.contains(secondId));

  QSignalSpy disconnected(&firstFiles, &FileTransferRuntime::peerDisconnected);
  QVERIFY(firstPairing.revoke(secondId).ok());
  QTRY_VERIFY_WITH_TIMEOUT(disconnected.count() == 1 && !firstFiles.isPeerReady(secondId), 5000);
  QVERIFY(!reconnect.m_coordinators.contains(secondId));
  QVERIFY(!reconnect.m_providers.contains(secondId));
  QVERIFY(!reconnect.m_pending.contains(secondId));

  QVERIFY(!firstFiles.connectPeer(secondId, &diagnostic));
  QCOMPARE(diagnostic, QStringLiteral("Peer file-transfer identity is not trusted"));

  QTest::qWait(100);
  QCOMPARE(disconnected.count(), 1);
  QVERIFY(!reconnect.m_coordinators.contains(secondId));
}

QTEST_MAIN(AutoReconnectRuntimeTests)

#include "AutoReconnectRuntimeTests.moc"
