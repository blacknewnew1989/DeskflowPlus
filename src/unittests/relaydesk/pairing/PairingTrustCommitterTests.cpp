/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingTrustCommitter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {

struct Fixture
{
  QTemporaryDir directory;
  DeviceId peerId = DeviceId::generate();
  QByteArray fingerprint = QByteArray(32, '\x51');
  PairingStateMachine machine{{}, {}, []() { return 123456U; }};
  TrustedDeviceStore store{directory.filePath(QStringLiteral("state/trusted-devices.json"))};

  DeviceSnapshot peer() const
  {
    return {
        .id = peerId,
        .displayName = QStringLiteral("Design Mac"),
        .alias = QStringLiteral(""),
        .platform = QStringLiteral("macos"),
        .architecture = QStringLiteral("arm64"),
        .presence = DevicePresence::Pairing,
        .addresses = {QHostAddress(QStringLiteral("192.168.1.20")), QHostAddress(QStringLiteral("2001:db8::1"))},
    };
  }

  QUuid readyToCommit()
  {
    if (!machine.begin(peer(), fingerprint).ok()) {
      return {};
    }
    const auto sessionId = machine.snapshot()->pairingSessionId;
    if (!machine.markTransportReady(sessionId).ok() || !machine.markTranscriptExchanged(sessionId).ok() ||
        !machine.confirmMatchingSas(sessionId).ok()) {
      return {};
    }
    return sessionId;
  }
};

} // namespace

class PairingTrustCommitterTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void atomicallyPersistsConfirmedTrust();
  void rePairReplacesFingerprintAndClearsRevocation();
  void rejectsSessionsBeforeUserConfirmation();
  void writeFailureDoesNotCompletePairing();
};

void PairingTrustCommitterTests::atomicallyPersistsConfirmedTrust()
{
  Fixture fixture;
  QVERIFY(fixture.directory.isValid());
  const auto sessionId = fixture.readyToCommit();
  QVERIFY(!sessionId.isNull());
  bool trustVisibleAtCompletion = false;
  connect(&fixture.machine, &PairingStateMachine::pairingChanged, this, [&](const PairingSnapshot &snapshot) {
    if (snapshot.state == PairingState::Completed) {
      trustVisibleAtCompletion =
          fixture.store.trustStatus(fixture.peerId, fixture.fingerprint) == TrustStatus::Trusted;
    }
  });

  const auto result = PairingTrustCommitter::commit(
      fixture.machine, fixture.store, sessionId, {.alias = QStringLiteral("  Studio  "), .autoAcceptFiles = true}
  );

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QVERIFY(trustVisibleAtCompletion);
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Completed);
  const auto confirmedFingerprint = fixture.machine.confirmedFingerprint(sessionId);
  QVERIFY(confirmedFingerprint.has_value());
  QCOMPARE(*confirmedFingerprint, fixture.fingerprint);
  const auto record = fixture.store.find(fixture.peerId);
  QVERIFY(record.has_value());
  QCOMPARE(record->alias, QStringLiteral("Studio"));
  QCOMPARE(record->platform, QStringLiteral("macos"));
  QCOMPARE(record->fingerprintSha256, fixture.fingerprint);
  QCOMPARE(record->lastAddresses, QStringList({QStringLiteral("192.168.1.20"), QStringLiteral("2001:db8::1")}));
  QVERIFY(record->autoAcceptFiles);
  QVERIFY(!record->revoked);

  TrustedDeviceStore reloaded(fixture.store.path());
  QVERIFY(reloaded.load().ok);
  QCOMPARE(reloaded.find(fixture.peerId), record);
}

void PairingTrustCommitterTests::rePairReplacesFingerprintAndClearsRevocation()
{
  Fixture fixture;
  const QByteArray oldFingerprint(32, '\x21');
  QVERIFY(fixture.store.upsert({
      .deviceId = fixture.peerId,
      .alias = QStringLiteral("Saved Alias"),
      .platform = QStringLiteral("macos"),
      .fingerprintSha256 = oldFingerprint,
      .autoAcceptFiles = true,
      .revoked = true,
  }));
  QVERIFY(fixture.store.save().ok);
  const auto sessionId = fixture.readyToCommit();

  const auto result = PairingTrustCommitter::commit(fixture.machine, fixture.store, sessionId);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  const auto record = fixture.store.find(fixture.peerId);
  QVERIFY(record.has_value());
  QCOMPARE(record->alias, QStringLiteral("Saved Alias"));
  QCOMPARE(record->fingerprintSha256, fixture.fingerprint);
  QVERIFY(!record->autoAcceptFiles);
  QVERIFY(!record->revoked);
}

void PairingTrustCommitterTests::rejectsSessionsBeforeUserConfirmation()
{
  Fixture fixture;
  QVERIFY(fixture.machine.begin(fixture.peer(), fixture.fingerprint).ok());
  const auto sessionId = fixture.machine.snapshot()->pairingSessionId;

  const auto result = PairingTrustCommitter::commit(fixture.machine, fixture.store, sessionId);

  QCOMPARE(result.error, PairingTrustCommitError::SessionNotConfirming);
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Requesting);
  QVERIFY(fixture.store.devices().isEmpty());
  QVERIFY(!QFileInfo::exists(fixture.store.path()));
}

void PairingTrustCommitterTests::writeFailureDoesNotCompletePairing()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString blocker = directory.filePath(QStringLiteral("not-a-directory"));
  QFile file(blocker);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  DeviceId peerId = DeviceId::generate();
  DeviceSnapshot peer{
      .id = peerId,
      .displayName = QStringLiteral("PC"),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .presence = DevicePresence::Pairing,
  };
  PairingStateMachine machine;
  QVERIFY(machine.begin(peer, QByteArray(32, '\x31')).ok());
  const auto sessionId = machine.snapshot()->pairingSessionId;
  QVERIFY(machine.markTransportReady(sessionId).ok());
  QVERIFY(machine.markTranscriptExchanged(sessionId).ok());
  QVERIFY(machine.confirmMatchingSas(sessionId).ok());
  TrustedDeviceStore store(blocker + QStringLiteral("/trusted.json"));

  const auto result = PairingTrustCommitter::commit(machine, store, sessionId);

  QCOMPARE(result.error, PairingTrustCommitError::PersistenceFailed);
  QCOMPARE(machine.snapshot()->state, PairingState::Failed);
  QCOMPARE(machine.snapshot()->errorMessageKey, QStringLiteral("pairing.trust_store_write_failed"));
  QVERIFY(store.devices().isEmpty());
  QVERIFY(!machine.confirmedFingerprint(sessionId).has_value());
}

QTEST_MAIN(PairingTrustCommitterTests)

#include "PairingTrustCommitterTests.moc"
