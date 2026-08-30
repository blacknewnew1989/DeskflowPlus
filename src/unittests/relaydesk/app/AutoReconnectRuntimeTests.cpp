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
#include <QPointer>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
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
  void settingsRefreshReplaysExistingTrustedSnapshot();
  void destructionInvalidatesScheduledRetryCallbacks();
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
  QObject errorContext;
  connect(&firstFiles, &FileTransferRuntime::errorOccurred, &errorContext, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  QPointer<AutoReconnectCoordinator> coordinator;
  QPointer<AddressCandidateProvider> provider;
  {
    AutoReconnectRuntime reconnect(firstPairing, firstDiscovery, firstFiles, {});
    QTRY_VERIFY2_WITH_TIMEOUT(firstFiles.isPeerReady(secondId), qPrintable(errors.join(QStringLiteral("; "))), 5000);
    QVERIFY(reconnect.m_coordinators.contains(secondId));
    coordinator = reconnect.m_coordinators.value(secondId);
    provider = reconnect.m_providers.value(secondId);
    QVERIFY(coordinator != nullptr);
    QVERIFY(provider != nullptr);
    reconnect.m_pending.insert(secondId, [](AutoReconnectConnectResult) {});
    QVERIFY(reconnect.m_pending.contains(secondId));

    QSignalSpy disconnected(&firstFiles, &FileTransferRuntime::peerDisconnected);
    QVERIFY(firstPairing.revoke(secondId).ok());
    QVERIFY(coordinator.isNull());
    QVERIFY(provider.isNull());
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

  QVERIFY(coordinator.isNull());
  QVERIFY(provider.isNull());
}

void AutoReconnectRuntimeTests::settingsRefreshReplaysExistingTrustedSnapshot()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto localId = DeviceId::generate();
  const auto peerId = DeviceId::generate();
  const auto localInfo = device(localId, identity.fingerprintSha256, QStringLiteral("Local"));
  auto peerInfo = device(peerId, identity.fingerprintSha256, QStringLiteral("Peer"));
  peerInfo.filePort = 24801;
  const auto trustPath = directory.filePath(QStringLiteral("trusted.json"));
  TrustedDeviceStore seed(trustPath);
  QVERIFY(seed.upsert(trustedDevice(peerId, identity.fingerprintSha256)));
  QVERIFY(seed.save().ok);

  DeviceHomeModel model;
  PairingWizardModel pairingModel;
  DeviceDiscoveryRuntime discovery(localInfo, model, loopbackDiscovery());
  QString diagnostic;
  QVERIFY2(discovery.start(&diagnostic), qPrintable(diagnostic));
  PairingTrustRuntime pairing(localInfo, trustPath, discovery, model, pairingModel);
  QVERIFY(pairing.isReady());
  QVERIFY(discovery.registry().observeAdvertisement(peerInfo, QHostAddress(QStringLiteral("192.0.2.60"))));

  FileTransferRuntime files(localId, pairing.trustedDevices(), discovery, identityPath);
  QPointer<AutoReconnectCoordinator> coordinator;
  QPointer<AddressCandidateProvider> provider;
  {
    AutoReconnectRuntime reconnect(pairing, discovery, files, {});
    QTRY_VERIFY(reconnect.m_coordinators.contains(peerId));
    coordinator = reconnect.m_coordinators.value(peerId);
    provider = reconnect.m_providers.value(peerId);
    QVERIFY(coordinator != nullptr);
    QVERIFY(provider != nullptr);
    QSignalSpy connecting(coordinator, &AutoReconnectCoordinator::connecting);
    QTRY_VERIFY(connecting.count() >= 1);
    const auto initialAttemptCount = connecting.count();

    reconnect.setSettings({.manualAddresses = {*parseManualAddress(QStringLiteral("192.0.2.61"), 24800, 24801)}});

    QTRY_VERIFY(connecting.count() >= initialAttemptCount + 2);
    QCOMPARE(connecting.last().at(1).value<AddressCandidate>().source, AddressCandidateSource::Manual);
  }

  QVERIFY(coordinator.isNull());
  QVERIFY(provider.isNull());
}

void AutoReconnectRuntimeTests::destructionInvalidatesScheduledRetryCallbacks()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto localId = DeviceId::generate();
  const auto peerId = DeviceId::generate();
  const auto localInfo = device(localId, identity.fingerprintSha256, QStringLiteral("Local"));
  const auto trustPath = directory.filePath(QStringLiteral("trusted.json"));
  TrustedDeviceStore seed(trustPath);
  QVERIFY(seed.upsert(trustedDevice(peerId, identity.fingerprintSha256)));
  QVERIFY(seed.save().ok);

  DeviceHomeModel model;
  PairingWizardModel pairingModel;
  DeviceDiscoveryRuntime discovery(localInfo, model, loopbackDiscovery());
  QString diagnostic;
  QVERIFY2(discovery.start(&diagnostic), qPrintable(diagnostic));
  PairingTrustRuntime pairing(localInfo, trustPath, discovery, model, pairingModel);
  QVERIFY(pairing.isReady());
  FileTransferRuntime files(localId, pairing.trustedDevices(), discovery, identityPath);
  TrustedDeviceStore coordinatorStore(trustPath);
  QVERIFY(coordinatorStore.load().ok);

  QList<std::function<void()>> scheduled;
  QPointer<AutoReconnectCoordinator> coordinator;
  QPointer<AddressCandidateProvider> provider;
  {
    AutoReconnectRuntime reconnect(pairing, discovery, files, {});
    provider = new AddressCandidateProvider({}, &reconnect);
    coordinator = new AutoReconnectCoordinator(
        coordinatorStore, *provider,
        [](const DeviceId &, const AddressCandidate &, AutoReconnectCoordinator::ConnectCallback callback) {
          callback({.error = AutoReconnectConnectError::NetworkError});
        },
        [&scheduled](ReconnectDelay, std::function<void()> callback) {
          scheduled.append(std::move(callback));
        }
    );
    reconnect.m_providers.insert(peerId, provider);
    reconnect.m_coordinators.insert(peerId, coordinator);
    coordinator->start({
        .deviceId = peerId,
        .discoveredAddresses = {QHostAddress::LocalHost},
        .settings = {},
        .inputPort = 24800,
        .filePort = 24801,
    });
    QTRY_COMPARE(scheduled.size(), 1);
  }

  QVERIFY(coordinator.isNull());
  QVERIFY(provider.isNull());
  auto retry = std::move(scheduled.front());
  retry();
  QCOMPARE(scheduled.size(), 1);
}

QTEST_MAIN(AutoReconnectRuntimeTests)

#include "AutoReconnectRuntimeTests.moc"
