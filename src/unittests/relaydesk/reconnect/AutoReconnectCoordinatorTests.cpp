/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/reconnect/AutoReconnectCoordinator.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <utility>

using namespace deskflow::relaydesk;

namespace {

constexpr auto kFingerprintByte = '\x31';

QByteArray fingerprint(char byte = kFingerprintByte)
{
  return QByteArray(kSha256FingerprintBytes, byte);
}

TrustedDevice trustedDevice(
    const DeviceId &deviceId, QString alias, QStringList lastAddresses = {}, bool revoked = false
)
{
  return {
      .deviceId = deviceId,
      .alias = std::move(alias),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = fingerprint(),
      .lastAddresses = std::move(lastAddresses),
      .revoked = revoked,
  };
}

AutoReconnectRequest requestFor(
    const DeviceId &deviceId, QList<QHostAddress> discoveredAddresses = {}, QByteArray presented = fingerprint()
)
{
  return {
      .deviceId = deviceId,
      .presentedFingerprintSha256 = std::move(presented),
      .discoveredAddresses = std::move(discoveredAddresses),
  };
}

struct PendingAttempt
{
  DeviceId deviceId;
  AddressCandidate candidate;
  AutoReconnectCoordinator::ConnectCallback callback;
};

} // namespace

class AutoReconnectCoordinatorTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void changedIpFallsThroughAndPersistsRecentSuccess();
  void sameAliasNeverOverridesDeviceIdentity();
  void unknownAndRevokedDevicesNeverConnect();
  void advertisedFingerprintMismatchNeverConnects();
  void connectorFingerprintChangeStopsCandidateRound();
  void failedCandidateAdvancesSerially();
  void networkRecoveryStartsFreshRoundAndStaleCallbacksAreIgnored();
  void candidateRoundsAreBoundedAndBackoffIsCapped();
};

void AutoReconnectCoordinatorTests::changedIpFallsThroughAndPersistsRecentSuccess()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"), {QStringLiteral("10.0.0.4")})));

  AddressCandidateProvider provider;
  QList<AddressCandidate> attempted;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attempted](const DeviceId &, const AddressCandidate &candidate, auto callback) {
        attempted.append(candidate);
        callback({
            .error = candidate.address == QHostAddress(QStringLiteral("10.0.0.9"))
                         ? AutoReconnectConnectError::None
                         : AutoReconnectConnectError::NetworkError,
        });
      }
  );
  QSignalSpy connected(&coordinator, &AutoReconnectCoordinator::connected);

  coordinator.start(requestFor(deviceId, {QHostAddress(QStringLiteral("10.0.0.9"))}));

  QTRY_COMPARE(connected.count(), 1);
  QCOMPARE(attempted.size(), 2);
  QCOMPARE(attempted.at(0).address, QHostAddress(QStringLiteral("10.0.0.4")));
  QCOMPARE(attempted.at(0).source, AddressCandidateSource::RecentSuccessful);
  QCOMPARE(attempted.at(1).address, QHostAddress(QStringLiteral("10.0.0.9")));
  QCOMPARE(attempted.at(1).source, AddressCandidateSource::Discovered);
  QCOMPARE(store.find(deviceId)->lastAddresses.first(), QStringLiteral("10.0.0.9"));

  TrustedDeviceStore reloaded(store.path());
  QVERIFY(reloaded.load().ok);
  QCOMPARE(reloaded.find(deviceId)->lastAddresses.first(), QStringLiteral("10.0.0.9"));
}

void AutoReconnectCoordinatorTests::sameAliasNeverOverridesDeviceIdentity()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(firstId, QStringLiteral("Same name"))));
  QVERIFY(store.upsert(trustedDevice(secondId, QStringLiteral("Same name"))));

  AddressCandidateProvider provider;
  QList<DeviceId> attemptedIds;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attemptedIds](const DeviceId &deviceId, const AddressCandidate &, auto callback) {
        attemptedIds.append(deviceId);
        callback({});
      }
  );
  QSignalSpy connected(&coordinator, &AutoReconnectCoordinator::connected);

  coordinator.start(requestFor(secondId, {QHostAddress(QStringLiteral("192.0.2.20"))}));

  QTRY_COMPARE(connected.count(), 1);
  QCOMPARE(attemptedIds, QList<DeviceId>{secondId});
}

void AutoReconnectCoordinatorTests::unknownAndRevokedDevicesNeverConnect()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto unknownId = DeviceId::generate();
  const auto revokedId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(revokedId, QStringLiteral("Revoked"), {}, true)));

  AddressCandidateProvider provider;
  int attemptCount = 0;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attemptCount](const DeviceId &, const AddressCandidate &, auto) { ++attemptCount; }
  );
  QSignalSpy blocked(&coordinator, &AutoReconnectCoordinator::trustBlocked);

  coordinator.start(requestFor(unknownId, {QHostAddress(QStringLiteral("192.0.2.1"))}));
  coordinator.start(requestFor(revokedId, {QHostAddress(QStringLiteral("192.0.2.2"))}));

  QCOMPARE(attemptCount, 0);
  QCOMPARE(blocked.count(), 2);
  QCOMPARE(blocked.at(0).at(1).value<PeerPinningError>(), PeerPinningError::UnknownPeer);
  QCOMPARE(blocked.at(1).at(1).value<PeerPinningError>(), PeerPinningError::RevokedPeer);
}

void AutoReconnectCoordinatorTests::advertisedFingerprintMismatchNeverConnects()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"))));

  AddressCandidateProvider provider;
  int attemptCount = 0;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attemptCount](const DeviceId &, const AddressCandidate &, auto) { ++attemptCount; }
  );
  QSignalSpy blocked(&coordinator, &AutoReconnectCoordinator::trustBlocked);

  coordinator.start(requestFor(
      deviceId, {QHostAddress(QStringLiteral("192.0.2.1"))}, fingerprint('\x55')
  ));

  QCOMPARE(attemptCount, 0);
  QCOMPARE(blocked.count(), 1);
  QCOMPARE(blocked.first().at(1).value<PeerPinningError>(), PeerPinningError::FingerprintChanged);
}

void AutoReconnectCoordinatorTests::connectorFingerprintChangeStopsCandidateRound()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"))));

  AddressCandidateProvider provider;
  int attemptCount = 0;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attemptCount](const DeviceId &, const AddressCandidate &, auto callback) {
        ++attemptCount;
        callback({
            .error = AutoReconnectConnectError::FingerprintChanged,
            .diagnostic = QStringLiteral("handshake pin mismatch"),
        });
      }
  );
  QSignalSpy blocked(&coordinator, &AutoReconnectCoordinator::trustBlocked);
  QSignalSpy retry(&coordinator, &AutoReconnectCoordinator::retryScheduled);

  coordinator.start(requestFor(
      deviceId,
      {QHostAddress(QStringLiteral("192.0.2.10")), QHostAddress(QStringLiteral("192.0.2.11"))}
  ));

  QTRY_COMPARE(blocked.count(), 1);
  QCOMPARE(attemptCount, 1);
  QCOMPARE(retry.count(), 0);
  QCOMPARE(blocked.first().at(1).value<PeerPinningError>(), PeerPinningError::FingerprintChanged);
}

void AutoReconnectCoordinatorTests::failedCandidateAdvancesSerially()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"))));

  AddressCandidateProvider provider;
  QList<PendingAttempt> attempts;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attempts](const DeviceId &id, const AddressCandidate &candidate, auto callback) {
        attempts.append({id, candidate, std::move(callback)});
      }
  );
  QSignalSpy connected(&coordinator, &AutoReconnectCoordinator::connected);

  coordinator.start(requestFor(
      deviceId,
      {QHostAddress(QStringLiteral("192.0.2.40")), QHostAddress(QStringLiteral("192.0.2.41"))}
  ));
  QTRY_COMPARE(attempts.size(), 1);
  attempts[0].callback({.error = AutoReconnectConnectError::NetworkError});
  QCOMPARE(attempts.size(), 2);
  QCOMPARE(attempts.at(1).candidate.address, QHostAddress(QStringLiteral("192.0.2.41")));
  attempts[1].callback({});
  QCOMPARE(connected.count(), 1);
}

void AutoReconnectCoordinatorTests::networkRecoveryStartsFreshRoundAndStaleCallbacksAreIgnored()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"))));

  AddressCandidateProvider provider;
  QList<PendingAttempt> attempts;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attempts](const DeviceId &id, const AddressCandidate &candidate, auto callback) {
        attempts.append({id, candidate, std::move(callback)});
      }
  );
  QSignalSpy connected(&coordinator, &AutoReconnectCoordinator::connected);

  coordinator.start(requestFor(deviceId, {QHostAddress(QStringLiteral("192.0.2.50"))}));
  QTRY_COMPARE(attempts.size(), 1);
  coordinator.networkAvailable();
  QTRY_COMPARE(attempts.size(), 2);

  attempts[0].callback({});
  QCOMPARE(connected.count(), 0);
  attempts[1].callback({});
  QCOMPARE(connected.count(), 1);
}

void AutoReconnectCoordinatorTests::candidateRoundsAreBoundedAndBackoffIsCapped()
{
  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.load().ok);
  const auto deviceId = DeviceId::generate();
  QVERIFY(store.upsert(trustedDevice(deviceId, QStringLiteral("Peer"))));

  QList<QHostAddress> discovered;
  for (int index = 1; index <= 20; ++index) {
    discovered.append(QHostAddress(QStringLiteral("198.51.100.%1").arg(index)));
  }
  QList<std::pair<int, std::function<void()>>> scheduled;
  AddressCandidateProvider provider;
  int attemptCount = 0;
  AutoReconnectCoordinator coordinator(
      store, provider,
      [&attemptCount](const DeviceId &, const AddressCandidate &, auto callback) {
        ++attemptCount;
        callback({.error = AutoReconnectConnectError::NetworkError});
      },
      [&scheduled](int delayMs, std::function<void()> callback) {
        scheduled.emplaceBack(delayMs, std::move(callback));
      },
      {.initialRetryDelayMs = 100, .maxRetryDelayMs = 250}
  );

  coordinator.start(requestFor(deviceId, discovered));
  QTRY_COMPARE(scheduled.size(), 1);
  QCOMPARE(attemptCount, int(kDefaultMaxReconnectCandidates));
  QCOMPARE(scheduled.at(0).first, 100);

  scheduled.at(0).second();
  QTRY_COMPARE(scheduled.size(), 2);
  QCOMPARE(scheduled.at(1).first, 200);
  scheduled.at(1).second();
  QTRY_COMPARE(scheduled.size(), 3);
  QCOMPARE(scheduled.at(2).first, 250);
}

QTEST_MAIN(AutoReconnectCoordinatorTests)

#include "AutoReconnectCoordinatorTests.moc"
