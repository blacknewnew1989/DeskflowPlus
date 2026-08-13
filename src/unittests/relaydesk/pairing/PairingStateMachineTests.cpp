/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingStateMachine.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace deskflow::relaydesk;

namespace {

DeviceSnapshot peerSnapshot()
{
  return {
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = DevicePresence::Discovered,
  };
}

struct Fixture
{
  QDateTime now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  PairingStateMachine machine{
      PairingOptions{},
      [this]() { return now; },
      []() { return 42U; },
  };
  QByteArray fingerprint = QByteArray(32, '\x2a');
};

QUuid beginToComparison(Fixture &fixture, const std::optional<QString> &receivedSas = std::nullopt)
{
  if (!fixture.machine.begin(peerSnapshot(), fixture.fingerprint, receivedSas).ok()) {
    return {};
  }
  const QUuid sessionId = fixture.machine.snapshot()->pairingSessionId;
  if (!fixture.machine.markTransportReady(sessionId).ok() || !fixture.machine.markTranscriptExchanged(sessionId).ok()) {
    return {};
  }
  return sessionId;
}

} // namespace

class PairingStateMachineTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void completesDisplayedComparisonFlow();
  void acceptsEnteredSixDigitCode();
  void rejectsWrongCodesAfterBoundedAttempts();
  void expiresAtExactDeadline();
  void rejectsInvalidInputs();
  void acceptsOnlyBoundSessionsWithinLocalValidity();
  void rejectsStaleAndOutOfOrderActions();
  void terminalStateNeverRegresses();
  void publishesEveryStateChange();
};

void PairingStateMachineTests::completesDisplayedComparisonFlow()
{
  Fixture fixture;
  const QUuid sessionId = beginToComparison(fixture);
  QVERIFY(!sessionId.isNull());
  QCOMPARE(fixture.machine.snapshot()->sixDigitSas, QStringLiteral("000042"));
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::AwaitingUserComparison);
  QVERIFY(fixture.machine.confirmMatchingSas(sessionId).ok());
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Confirming);
  QVERIFY(fixture.machine.complete(sessionId).ok());
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Completed);
  const auto confirmedFingerprint = fixture.machine.confirmedFingerprint(sessionId);
  QVERIFY(confirmedFingerprint.has_value());
  QCOMPARE(*confirmedFingerprint, fixture.fingerprint);
}

void PairingStateMachineTests::acceptsEnteredSixDigitCode()
{
  Fixture fixture;
  const QUuid sessionId = beginToComparison(fixture, QStringLiteral("123456"));
  QVERIFY(!sessionId.isNull());
  QCOMPARE(fixture.machine.snapshot()->sixDigitSas, QStringLiteral("123456"));
  QVERIFY(fixture.machine.submitDisplayedSas(sessionId, QStringLiteral("123456")).ok());
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Confirming);
}

void PairingStateMachineTests::rejectsWrongCodesAfterBoundedAttempts()
{
  Fixture fixture;
  const QUuid sessionId = beginToComparison(fixture);
  for (int expected = 2; expected >= 1; --expected) {
    const auto result = fixture.machine.submitDisplayedSas(sessionId, QStringLiteral("999999"));
    QCOMPARE(result.error, PairingError::InvalidSas);
    QCOMPARE(fixture.machine.snapshot()->attemptsRemaining, expected);
    QCOMPARE(fixture.machine.snapshot()->state, PairingState::AwaitingUserComparison);
  }
  const auto exhausted = fixture.machine.submitDisplayedSas(sessionId, QStringLiteral("999999"));
  QCOMPARE(exhausted.error, PairingError::AttemptsExhausted);
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Rejected);
  QCOMPARE(fixture.machine.snapshot()->failureReason, PairingFailureReason::TooManyAttempts);
}

void PairingStateMachineTests::expiresAtExactDeadline()
{
  Fixture fixture;
  QVERIFY(fixture.machine.begin(peerSnapshot(), fixture.fingerprint).ok());
  const auto snapshot = *fixture.machine.snapshot();
  fixture.now = snapshot.expiresAtUtc.addMSecs(-1);
  QVERIFY(!fixture.machine.expireIfNeeded());
  fixture.now = snapshot.expiresAtUtc;
  QVERIFY(fixture.machine.expireIfNeeded());
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Expired);
  QCOMPARE(fixture.machine.snapshot()->failureReason, PairingFailureReason::Expired);
  QVERIFY(!fixture.machine.expireIfNeeded());
}

void PairingStateMachineTests::rejectsInvalidInputs()
{
  Fixture fixture;
  QCOMPARE(fixture.machine.begin(peerSnapshot(), QByteArray(31, '\x2a')).error, PairingError::InvalidFingerprint);
  QCOMPARE(
      fixture.machine.begin(peerSnapshot(), fixture.fingerprint, QStringLiteral("12345x")).error,
      PairingError::InvalidSas
  );

  PairingStateMachine invalidGenerator({}, [now = fixture.now]() { return now; }, []() { return 1'000'000U; });
  QCOMPARE(invalidGenerator.begin(peerSnapshot(), fixture.fingerprint).error, PairingError::InvalidSas);
}

void PairingStateMachineTests::acceptsOnlyBoundSessionsWithinLocalValidity()
{
  Fixture fixture;
  const auto sessionId = QUuid::createUuid();
  const auto expiry = fixture.now.addSecs(240);

  const auto accepted = fixture.machine.beginBoundSession(
      peerSnapshot(), fixture.fingerprint, sessionId, expiry, QStringLiteral("654321")
  );

  QVERIFY2(accepted.ok(), qPrintable(accepted.diagnostic));
  QCOMPARE(fixture.machine.snapshot()->pairingSessionId, sessionId);
  QCOMPARE(fixture.machine.snapshot()->expiresAtUtc, expiry);
  QCOMPARE(fixture.machine.snapshot()->sixDigitSas, QStringLiteral("654321"));

  Fixture expired;
  QCOMPARE(
      expired.machine.beginBoundSession(
          peerSnapshot(), expired.fingerprint, QUuid::createUuid(), expired.now, QStringLiteral("123456")
      ).error,
      PairingError::InvalidState
  );
  Fixture tooLong;
  QCOMPARE(
      tooLong.machine.beginBoundSession(
          peerSnapshot(), tooLong.fingerprint, QUuid::createUuid(), tooLong.now.addSecs(301),
          QStringLiteral("123456")
      ).error,
      PairingError::InvalidState
  );
  Fixture nullSession;
  QCOMPARE(
      nullSession.machine.beginBoundSession(
          peerSnapshot(), nullSession.fingerprint, {}, nullSession.now.addSecs(60), QStringLiteral("123456")
      ).error,
      PairingError::InvalidState
  );
}

void PairingStateMachineTests::rejectsStaleAndOutOfOrderActions()
{
  Fixture fixture;
  QVERIFY(fixture.machine.begin(peerSnapshot(), fixture.fingerprint).ok());
  const QUuid sessionId = fixture.machine.snapshot()->pairingSessionId;
  QCOMPARE(fixture.machine.complete(sessionId).error, PairingError::InvalidState);
  QCOMPARE(fixture.machine.markTransportReady(QUuid::createUuid()).error, PairingError::SessionNotFound);
  QCOMPARE(fixture.machine.begin(peerSnapshot(), fixture.fingerprint).error, PairingError::ActiveSessionExists);
}

void PairingStateMachineTests::terminalStateNeverRegresses()
{
  Fixture fixture;
  const QUuid sessionId = beginToComparison(fixture);
  QVERIFY(fixture.machine.cancel(sessionId).ok());
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Rejected);
  QCOMPARE(fixture.machine.snapshot()->failureReason, PairingFailureReason::Cancelled);
  QCOMPARE(fixture.machine.confirmMatchingSas(sessionId).error, PairingError::InvalidState);
  QCOMPARE(fixture.machine.snapshot()->state, PairingState::Rejected);

  QVERIFY(fixture.machine.begin(peerSnapshot(), fixture.fingerprint).ok());
  QVERIFY(fixture.machine.snapshot()->pairingSessionId != sessionId);
}

void PairingStateMachineTests::publishesEveryStateChange()
{
  qRegisterMetaType<PairingSnapshot>();
  Fixture fixture;
  QSignalSpy spy(&fixture.machine, &PairingStateMachine::pairingChanged);
  const QUuid sessionId = beginToComparison(fixture);
  QVERIFY(!sessionId.isNull());
  QCOMPARE(spy.count(), 3);
  QVERIFY(fixture.machine.submitDisplayedSas(sessionId, QStringLiteral("999999")).error == PairingError::InvalidSas);
  QCOMPARE(spy.count(), 4);
  QVERIFY(fixture.machine.submitDisplayedSas(sessionId, QStringLiteral("000042")).ok());
  QCOMPARE(spy.count(), 5);
  QVERIFY(fixture.machine.complete(sessionId).ok());
  QCOMPARE(spy.count(), 6);
}

QTEST_MAIN(PairingStateMachineTests)

#include "PairingStateMachineTests.moc"
