/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/PairingTrustRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"
#include "relaydesk/trust/TlsPeerPinningPolicy.h"
#include "relaydesk/widgets/DevicesDock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::widgets;

namespace {
DeviceInfo device(QString name, char fingerprintByte)
{
  return {
      .deviceId = DeviceId::generate(),
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("1.26.0-relaydesk.1"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, fingerprintByte),
  };
}

DeviceDiscoveryRuntimeOptions loopbackDiscovery()
{
  return {
      .serviceSettings = {.port = 0, .announcementIntervalMs = 60000},
      .interfaceProvider = []() { return QList<DiscoveryInterface>{}; },
  };
}

struct RuntimePair
{
  RuntimePair(
      QTemporaryDir &directory, PairingTrustRuntimeOptions firstOptions = {},
      PairingTrustRuntimeOptions secondOptions = {}
  )
      : firstInfo(device(QStringLiteral("First"), '\x11')),
        secondInfo(device(QStringLiteral("Second"), '\x22')),
        firstPairingModel(),
        secondPairingModel(),
        firstDiscovery(firstInfo, firstModel, loopbackDiscovery()),
        secondDiscovery(secondInfo, secondModel, loopbackDiscovery())
  {
    QString firstDiagnostic;
    QString secondDiagnostic;
    const auto firstStarted = firstDiscovery.start(&firstDiagnostic);
    const auto secondStarted = secondDiscovery.start(&secondDiagnostic);
    readyDiagnostic = QStringLiteral("first: %1; second: %2").arg(firstDiagnostic, secondDiagnostic);
    if (!firstStarted || !secondStarted) {
      return;
    }
    if (!firstDiscovery.registry().observeAdvertisement(secondInfo, QHostAddress::LocalHost) ||
        !secondDiscovery.registry().observeAdvertisement(firstInfo, QHostAddress::LocalHost)) {
      readyDiagnostic = QStringLiteral("loopback discovery observations were rejected");
      return;
    }

    firstOptions.endpointResolver = [this](const DeviceId &) {
      return std::optional(std::pair(QHostAddress(QHostAddress::LocalHost), secondDiscovery.service().boundPort()));
    };
    secondOptions.endpointResolver = [this](const DeviceId &) {
      return std::optional(std::pair(QHostAddress(QHostAddress::LocalHost), firstDiscovery.service().boundPort()));
    };
    first = std::make_unique<PairingTrustRuntime>(
        firstInfo, directory.filePath(QStringLiteral("first/trusted.json")), firstDiscovery, firstModel,
        firstPairingModel, std::move(firstOptions)
    );
    second = std::make_unique<PairingTrustRuntime>(
        secondInfo, directory.filePath(QStringLiteral("second/trusted.json")), secondDiscovery, secondModel,
        secondPairingModel, std::move(secondOptions)
    );
    ready = true;
  }

  PairingOperationResult start()
  {
    return first->startPairing(secondInfo.deviceId);
  }

  DeviceInfo firstInfo;
  DeviceInfo secondInfo;
  DeviceHomeModel firstModel;
  DeviceHomeModel secondModel;
  PairingWizardModel firstPairingModel;
  PairingWizardModel secondPairingModel;
  DeviceDiscoveryRuntime firstDiscovery;
  DeviceDiscoveryRuntime secondDiscovery;
  std::unique_ptr<PairingTrustRuntime> first;
  std::unique_ptr<PairingTrustRuntime> second;
  bool ready = false;
  QString readyDiagnostic;
};
} // namespace

class PairingTrustRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void twoSharedDiscoverySocketsPersistExplicitlyConfirmedTrust();
  void wrongCodeNeverPinsAdvertisedFingerprint();
  void cancelAndExpiryReturnDevicesToDiscovered();
  void revokePersistsAndPinningPolicyRejectsPeer();
  void autoAcceptUpdateCommitsPrimaryWhenBackupWriteFails();
  void unusableTrustStoreBlocksPairing();
  void rejectsMissingFreshDiscoveryIdentityAndEndpoint();
  void productionPairingWidgetsConfirmAndPersistIndependentTrust();
  void productionPairingWidgetsCancelWithoutPersistingTrust();
};

void PairingTrustRuntimeTests::twoSharedDiscoverySocketsPersistExplicitlyConfirmedTrust()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  PairingTrustRuntimeOptions firstOptions{.sasGenerator = []() { return 123456U; }};
  RuntimePair pair(directory, firstOptions);
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));

  const auto started = pair.start();
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);

  const auto sas = pair.first->snapshot()->sixDigitSas;
  QCOMPARE(sas, QStringLiteral("123456"));
  QVERIFY(pair.firstPairingModel.active());
  QVERIFY(pair.secondPairingModel.active());
  QCOMPARE(pair.firstPairingModel.fullFingerprint(), QString::fromLatin1(QByteArray(32, '\x22').toHex(':').toUpper()));
  QCOMPARE(pair.secondPairingModel.fullFingerprint(), QString::fromLatin1(QByteArray(32, '\x11').toHex(':').toUpper()));
  QVERIFY(pair.firstPairingModel.confirmMatchingSas());
  QVERIFY(pair.secondPairingModel.submitDisplayedSas(sas));

  QTRY_COMPARE_WITH_TIMEOUT(pair.first->snapshot()->state, PairingState::Completed, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(pair.second->snapshot()->state, PairingState::Completed, 3000);
  const auto firstPeer = pair.firstModel.snapshot(pair.secondInfo.deviceId);
  const auto secondPeer = pair.secondModel.snapshot(pair.firstInfo.deviceId);
  QVERIFY(firstPeer->trusted);
  QVERIFY(secondPeer->trusted);
  QCOMPARE(firstPeer->pinnedFingerprint, pair.secondInfo.certificateFingerprintSha256);
  QCOMPARE(secondPeer->pinnedFingerprint, pair.firstInfo.certificateFingerprintSha256);

  TrustedDeviceStore firstReload(directory.filePath(QStringLiteral("first/trusted.json")));
  TrustedDeviceStore secondReload(directory.filePath(QStringLiteral("second/trusted.json")));
  QVERIFY(firstReload.load().ok);
  QVERIFY(secondReload.load().ok);
  QCOMPARE(
      firstReload.trustStatus(pair.secondInfo.deviceId, pair.secondInfo.certificateFingerprintSha256),
      TrustStatus::Trusted
  );
  QCOMPARE(
      secondReload.trustStatus(pair.firstInfo.deviceId, pair.firstInfo.certificateFingerprintSha256),
      TrustStatus::Trusted
  );

  QVERIFY(pair.firstDiscovery.registry().observeAdvertisement(pair.secondInfo, QHostAddress::LocalHost));
  QVERIFY(pair.firstModel.snapshot(pair.secondInfo.deviceId)->trusted);
  auto changedIdentity = pair.secondInfo;
  changedIdentity.certificateFingerprintSha256 = QByteArray(32, '\x44');
  QVERIFY(pair.firstDiscovery.registry().observeAdvertisement(changedIdentity, QHostAddress::LocalHost));
  const auto violated = pair.firstModel.snapshot(pair.secondInfo.deviceId);
  QCOMPARE(violated->presence, DevicePresence::TrustViolation);
  QVERIFY(!violated->trusted);
  QVERIFY(violated->pinnedFingerprint.isEmpty());
}

void PairingTrustRuntimeTests::wrongCodeNeverPinsAdvertisedFingerprint()
{
  QTemporaryDir directory;
  RuntimePair pair(directory, {.sasGenerator = []() { return 123456U; }});
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));
  QVERIFY(pair.start().ok());
  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);

  QVERIFY(pair.firstPairingModel.confirmMatchingSas());
  QVERIFY(pair.secondPairingModel.submitDisplayedSas(QStringLiteral("000000")));
  QTRY_VERIFY_WITH_TIMEOUT(
      pair.first->snapshot()->state == PairingState::Rejected || pair.first->snapshot()->state == PairingState::Failed,
      3000
  );
  QVERIFY(!pair.first->trustedDevices().find(pair.secondInfo.deviceId).has_value());
  QVERIFY(!pair.second->trustedDevices().find(pair.firstInfo.deviceId).has_value());
  const auto peer = pair.firstModel.snapshot(pair.secondInfo.deviceId);
  QVERIFY(!peer->trusted);
  QVERIFY(peer->pinnedFingerprint.isEmpty());
}

void PairingTrustRuntimeTests::cancelAndExpiryReturnDevicesToDiscovered()
{
  QTemporaryDir cancelDirectory;
  RuntimePair cancelled(cancelDirectory);
  QVERIFY2(cancelled.ready, qPrintable(cancelled.readyDiagnostic));
  QVERIFY(cancelled.start().ok());
  QTRY_VERIFY_WITH_TIMEOUT(cancelled.second->snapshot().has_value(), 3000);
  QVERIFY(cancelled.firstPairingModel.cancel());
  QTRY_COMPARE_WITH_TIMEOUT(cancelled.second->snapshot()->state, PairingState::Rejected, 3000);
  QCOMPARE(cancelled.firstModel.snapshot(cancelled.secondInfo.deviceId)->presence, DevicePresence::Discovered);
  QCOMPARE(cancelled.secondModel.snapshot(cancelled.firstInfo.deviceId)->presence, DevicePresence::Discovered);

  QTemporaryDir expiryDirectory;
  auto now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  PairingTrustRuntimeOptions expiryOptions{
      .pairing = {.validity = std::chrono::seconds(1), .attempts = 3},
      .clock = [&now]() { return now; },
  };
  RuntimePair expired(expiryDirectory, expiryOptions, expiryOptions);
  QVERIFY2(expired.ready, qPrintable(expired.readyDiagnostic));
  QVERIFY(expired.start().ok());
  QTRY_VERIFY_WITH_TIMEOUT(expired.second->snapshot().has_value(), 3000);
  now = now.addSecs(2);
  QVERIFY(expired.first->expireIfNeeded());
  QVERIFY(expired.second->expireIfNeeded());
  QCOMPARE(expired.first->snapshot()->state, PairingState::Expired);
  QCOMPARE(expired.second->snapshot()->state, PairingState::Expired);
  QCOMPARE(expired.firstModel.snapshot(expired.secondInfo.deviceId)->presence, DevicePresence::Discovered);
  QCOMPARE(expired.secondModel.snapshot(expired.firstInfo.deviceId)->presence, DevicePresence::Discovered);
}

void PairingTrustRuntimeTests::revokePersistsAndPinningPolicyRejectsPeer()
{
  QTemporaryDir directory;
  RuntimePair pair(directory, {.sasGenerator = []() { return 123456U; }});
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));
  QVERIFY(pair.start().ok());
  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);
  const auto sas = pair.first->snapshot()->sixDigitSas;
  QVERIFY(pair.firstPairingModel.confirmMatchingSas());
  QVERIFY(pair.secondPairingModel.submitDisplayedSas(sas));
  QTRY_COMPARE_WITH_TIMEOUT(pair.first->snapshot()->state, PairingState::Completed, 3000);

  QSignalSpy revoked(pair.first.get(), &PairingTrustRuntime::trustRevoked);
  QVERIFY(pair.first->revoke(pair.secondInfo.deviceId).ok());
  QCOMPARE(revoked.count(), 1);
  QCOMPARE(*static_cast<const DeviceId *>(revoked.first().at(0).constData()), pair.secondInfo.deviceId);
  QCOMPARE(pair.firstModel.snapshot(pair.secondInfo.deviceId)->presence, DevicePresence::TrustViolation);
  const auto pinning = TlsPeerPinningPolicy::verify(
      pair.first->trustedDevices(), pair.secondInfo.deviceId, pair.secondInfo.certificateFingerprintSha256
  );
  QCOMPARE(pinning.error, PeerPinningError::RevokedPeer);

  TrustedDeviceStore reloaded(directory.filePath(QStringLiteral("first/trusted.json")));
  QVERIFY(reloaded.load().ok);
  QCOMPARE(
      reloaded.trustStatus(pair.secondInfo.deviceId, pair.secondInfo.certificateFingerprintSha256), TrustStatus::Revoked
  );
}

void PairingTrustRuntimeTests::autoAcceptUpdateCommitsPrimaryWhenBackupWriteFails()
{
  QTemporaryDir directory;
  RuntimePair pair(directory, {.sasGenerator = []() { return 123456U; }});
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));
  QVERIFY(pair.start().ok());
  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);
  const auto sas = pair.first->snapshot()->sixDigitSas;
  QVERIFY(pair.firstPairingModel.confirmMatchingSas());
  QVERIFY(pair.secondPairingModel.submitDisplayedSas(sas));
  QTRY_COMPARE_WITH_TIMEOUT(pair.first->snapshot()->state, PairingState::Completed, 3000);

  const auto storePath = directory.filePath(QStringLiteral("first/trusted.json"));
  const auto backupPath = storePath + QStringLiteral(".bak");
  QVERIFY(QFile::remove(backupPath));
  QVERIFY(QDir().mkpath(backupPath));

  const auto result = pair.first->setAutoAcceptFiles(pair.secondInfo.deviceId, true);
  QVERIFY(result.ok());
  QVERIFY(pair.first->trustedDevices().find(pair.secondInfo.deviceId)->autoAcceptFiles);

  TrustedDeviceStore reloaded(storePath);
  QVERIFY(reloaded.load().ok);
  QVERIFY(reloaded.find(pair.secondInfo.deviceId)->autoAcceptFiles);
}

void PairingTrustRuntimeTests::unusableTrustStoreBlocksPairing()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localInfo = device(QStringLiteral("Local"), '\x51');
  const auto peerInfo = device(QStringLiteral("Peer"), '\x52');
  DeviceHomeModel devices;
  PairingWizardModel pairing;
  DeviceDiscoveryRuntime discovery(localInfo, devices, loopbackDiscovery());
  QVERIFY(discovery.start());
  QVERIFY(discovery.registry().observeAdvertisement(peerInfo, QHostAddress::LocalHost));

  const auto storePath = directory.filePath(QStringLiteral("state/trusted.json"));
  QVERIFY(QDir().mkpath(QFileInfo(storePath).absolutePath()));
  for (const auto &path : {storePath, storePath + QStringLiteral(".bak")}) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not-json"), qint64(8));
  }

  PairingTrustRuntime runtime(localInfo, storePath, discovery, devices, pairing);
  QVERIFY(!runtime.isReady());
  const auto result = runtime.startPairing(peerInfo.deviceId);
  QCOMPARE(result.error, PairingOperationError::PersistenceFailed);
  QVERIFY(!pairing.active());
  QVERIFY(!devices.snapshot(peerInfo.deviceId)->trusted);
}

void PairingTrustRuntimeTests::rejectsMissingFreshDiscoveryIdentityAndEndpoint()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localInfo = device(QStringLiteral("Local"), '\x61');
  const auto peerInfo = device(QStringLiteral("Peer"), '\x62');
  DeviceHomeModel devices;
  PairingWizardModel pairing;
  DeviceDiscoveryRuntime discovery(localInfo, devices, loopbackDiscovery());
  QVERIFY(discovery.start());

  PairingTrustRuntime runtime(
      localInfo, directory.filePath(QStringLiteral("trusted.json")), discovery, devices, pairing,
      {.endpointResolver = [](const DeviceId &) { return std::optional<std::pair<QHostAddress, quint16>>{}; }}
  );
  QSignalSpy failed(&runtime, &IPairingService::operationFailed);

  const auto missing = runtime.startPairing(peerInfo.deviceId);
  QCOMPARE(missing.error, PairingOperationError::InvalidPeer);
  QCOMPARE(failed.count(), 1);
  QVERIFY(!runtime.snapshot().has_value());

  QVERIFY(discovery.registry().observeAdvertisement(peerInfo, QHostAddress::LocalHost));
  const auto endpoint = runtime.startPairing(peerInfo.deviceId);
  QCOMPARE(endpoint.error, PairingOperationError::InvalidEndpoint);
  QCOMPARE(failed.count(), 2);
  QVERIFY(!runtime.snapshot().has_value());
  QVERIFY(!runtime.trustedDevices().find(peerInfo.deviceId).has_value());
}

void PairingTrustRuntimeTests::productionPairingWidgetsConfirmAndPersistIndependentTrust()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  RuntimePair pair(directory, {.sasGenerator = [] { return 123456U; }});
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));

  PermissionStatusModel permissions(PermissionPlatform::Other);
  DevicesDock dock(pair.firstModel, pair.firstPairingModel, permissions);
  // This is the production MainWindow intent boundary: the local UI emits a
  // typed DeviceId and the runtime resolves the current discovery identity.
  connect(&dock, &DevicesDock::pairingRequested, pair.first.get(), [runtime = pair.first.get()](const DeviceId &id) {
    (void)runtime->startPairing(id);
  });
  dock.show();

  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pairButton = dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  auto *panel = dock.findChild<QFrame *>(QStringLiteral("relaydeskPairingPanel"));
  auto *sas = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingSasLabel"));
  auto *confirm = dock.findChild<QPushButton *>(QStringLiteral("relaydeskConfirmMatchingSasButton"));
  QVERIFY(list != nullptr);
  QVERIFY(pairButton != nullptr);
  QVERIFY(panel != nullptr);
  QVERIFY(sas != nullptr);
  QVERIFY(confirm != nullptr);

  const auto index = pair.firstModel.index(pair.firstModel.indexOf(pair.secondInfo.deviceId), 0);
  QVERIFY(index.isValid());
  list->setCurrentIndex(index);
  QTRY_VERIFY(pairButton->isVisible() && pairButton->isEnabled());
  QTest::mouseClick(pairButton, Qt::LeftButton);

  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);
  QTRY_VERIFY_WITH_TIMEOUT(pair.firstPairingModel.active() && pair.secondPairingModel.active(), 3000);
  QTRY_VERIFY_WITH_TIMEOUT(!pair.firstPairingModel.sixDigitSas().isEmpty(), 3000);
  QVERIFY(pair.secondPairingModel.canSubmitCode());
  QVERIFY(pair.secondPairingModel.sixDigitSas().isEmpty());
  QTRY_VERIFY(panel->isVisible());
  QCOMPARE(
      sas->text(), pair.firstPairingModel.sixDigitSas().first(3) + QStringLiteral(" ") +
                       pair.firstPairingModel.sixDigitSas().last(3)
  );
  QTRY_VERIFY(confirm->isVisible() && confirm->isEnabled());

  QTest::mouseClick(confirm, Qt::LeftButton);
  QVERIFY(pair.secondPairingModel.submitDisplayedSas(pair.firstPairingModel.sixDigitSas()));
  QTRY_COMPARE_WITH_TIMEOUT(pair.first->snapshot()->state, PairingState::Completed, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(pair.second->snapshot()->state, PairingState::Completed, 3000);

  const auto firstDevice = pair.firstModel.snapshot(pair.secondInfo.deviceId);
  const auto secondDevice = pair.secondModel.snapshot(pair.firstInfo.deviceId);
  QVERIFY(firstDevice.has_value());
  QVERIFY(secondDevice.has_value());
  QCOMPARE(firstDevice->presence, DevicePresence::Online);
  QCOMPARE(secondDevice->presence, DevicePresence::Online);
  QVERIFY(firstDevice->trusted);
  QVERIFY(secondDevice->trusted);
  QCOMPARE(firstDevice->pinnedFingerprint, pair.secondInfo.certificateFingerprintSha256);
  QCOMPARE(secondDevice->pinnedFingerprint, pair.firstInfo.certificateFingerprintSha256);

  TrustedDeviceStore firstStore(directory.filePath(QStringLiteral("first/trusted.json")));
  TrustedDeviceStore secondStore(directory.filePath(QStringLiteral("second/trusted.json")));
  QVERIFY(firstStore.load().ok);
  QVERIFY(secondStore.load().ok);
  QCOMPARE(
      firstStore.trustStatus(pair.secondInfo.deviceId, pair.secondInfo.certificateFingerprintSha256), TrustStatus::Trusted
  );
  QCOMPARE(
      secondStore.trustStatus(pair.firstInfo.deviceId, pair.firstInfo.certificateFingerprintSha256), TrustStatus::Trusted
  );

  QFile firstTrustFile(directory.filePath(QStringLiteral("first/trusted.json")));
  QFile secondTrustFile(directory.filePath(QStringLiteral("second/trusted.json")));
  QVERIFY(firstTrustFile.open(QIODevice::ReadOnly));
  QVERIFY(secondTrustFile.open(QIODevice::ReadOnly));
  const auto firstTrustBeforeRepeat = firstTrustFile.readAll();
  const auto secondTrustBeforeRepeat = secondTrustFile.readAll();
  firstTrustFile.close();
  secondTrustFile.close();
  const auto completedSession = pair.first->snapshot()->pairingSessionId;
  QTRY_VERIFY(!pairButton->isVisible());
  QTest::mouseClick(pairButton, Qt::LeftButton);
  QCOMPARE(pair.first->snapshot()->pairingSessionId, completedSession);
  QVERIFY(firstTrustFile.open(QIODevice::ReadOnly));
  QVERIFY(secondTrustFile.open(QIODevice::ReadOnly));
  QCOMPARE(firstTrustFile.readAll(), firstTrustBeforeRepeat);
  QCOMPARE(secondTrustFile.readAll(), secondTrustBeforeRepeat);
}

void PairingTrustRuntimeTests::productionPairingWidgetsCancelWithoutPersistingTrust()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  RuntimePair pair(directory, {.sasGenerator = [] { return 654321U; }});
  QVERIFY2(pair.ready, qPrintable(pair.readyDiagnostic));

  PermissionStatusModel permissions(PermissionPlatform::Other);
  DevicesDock dock(pair.firstModel, pair.firstPairingModel, permissions);
  connect(&dock, &DevicesDock::pairingRequested, pair.first.get(), [runtime = pair.first.get()](const DeviceId &id) {
    (void)runtime->startPairing(id);
  });
  dock.show();

  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pairButton = dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  auto *cancel = dock.findChild<QPushButton *>(QStringLiteral("relaydeskCancelPairingButton"));
  QVERIFY(list != nullptr);
  QVERIFY(pairButton != nullptr);
  QVERIFY(cancel != nullptr);

  const auto index = pair.firstModel.index(pair.firstModel.indexOf(pair.secondInfo.deviceId), 0);
  QVERIFY(index.isValid());
  list->setCurrentIndex(index);
  QTRY_VERIFY(pairButton->isVisible() && pairButton->isEnabled());
  QTest::mouseClick(pairButton, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(pair.second->snapshot().has_value(), 3000);
  QTRY_VERIFY_WITH_TIMEOUT(cancel->isVisible() && cancel->isEnabled(), 3000);
  QTest::mouseClick(cancel, Qt::LeftButton);

  QTRY_COMPARE_WITH_TIMEOUT(pair.first->snapshot()->state, PairingState::Rejected, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(pair.second->snapshot()->state, PairingState::Rejected, 3000);
  QVERIFY(!pair.first->trustedDevices().find(pair.secondInfo.deviceId).has_value());
  QVERIFY(!pair.second->trustedDevices().find(pair.firstInfo.deviceId).has_value());
  const auto firstDevice = pair.firstModel.snapshot(pair.secondInfo.deviceId);
  const auto secondDevice = pair.secondModel.snapshot(pair.firstInfo.deviceId);
  QVERIFY(firstDevice.has_value());
  QVERIFY(secondDevice.has_value());
  QCOMPARE(firstDevice->presence, DevicePresence::Discovered);
  QCOMPARE(secondDevice->presence, DevicePresence::Discovered);
  QVERIFY(!firstDevice->trusted);
  QVERIFY(!secondDevice->trusted);
  QTRY_VERIFY(!cancel->isVisible());
  QTRY_VERIFY(pairButton->isVisible() && pairButton->isEnabled());

  TrustedDeviceStore firstStore(directory.filePath(QStringLiteral("first/trusted.json")));
  TrustedDeviceStore secondStore(directory.filePath(QStringLiteral("second/trusted.json")));
  QVERIFY(firstStore.load().ok);
  QVERIFY(secondStore.load().ok);
  QVERIFY(!firstStore.find(pair.secondInfo.deviceId).has_value());
  QVERIFY(!secondStore.find(pair.firstInfo.deviceId).has_value());
}

QTEST_MAIN(PairingTrustRuntimeTests)

#include "PairingTrustRuntimeTests.moc"
