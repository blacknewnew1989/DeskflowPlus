/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/PairingWizardModel.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {

DeviceSnapshot peerSnapshot()
{
  return {
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .alias = QStringLiteral("Design Mac"),
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
  PairingWizardModel wizard{machine};
  QByteArray fingerprint = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
};

bool advanceToComparison(Fixture &fixture)
{
  const auto snapshot = fixture.machine.snapshot();
  return snapshot.has_value() && fixture.machine.markTransportReady(snapshot->pairingSessionId).ok() &&
         fixture.machine.markTranscriptExchanged(snapshot->pairingSessionId).ok();
}

} // namespace

class PairingWizardModelTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void startsFromSharedDeviceSnapshot();
  void confirmsMatchingDisplayedCode();
  void validatesAndSubmitsEnteredCode();
  void cancelsActiveSession();
  void exposesExpiryAndSharedErrorKeys();
  void rejectsActionsWithoutSession();
  void formatsMissingFingerprintSafely();
};

void PairingWizardModelTests::startsFromSharedDeviceSnapshot()
{
  qRegisterMetaType<DeviceSnapshot>();
  Fixture fixture;
  QSignalSpy changed(&fixture.wizard, &PairingWizardModel::changed);
  QSignalSpy requested(&fixture.wizard, &PairingWizardModel::startRequested);

  QVERIFY(fixture.wizard.start(peerSnapshot(), fixture.fingerprint));
  QVERIFY(fixture.wizard.active());
  QCOMPARE(fixture.wizard.peerName(), QStringLiteral("Design Mac"));
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Requesting));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Requesting pairing"));
  QCOMPARE(fixture.wizard.title(), QStringLiteral("Pair device"));
  QCOMPARE(fixture.wizard.codePrompt(), QStringLiteral("Enter the six-digit code"));
  QCOMPARE(fixture.wizard.confirmActionText(), QStringLiteral("Codes match"));
  QCOMPARE(fixture.wizard.submitActionText(), QStringLiteral("Confirm code"));
  QCOMPARE(fixture.wizard.cancelActionText(), QStringLiteral("Cancel"));
  QCOMPARE(fixture.wizard.fingerprintLabel(), QStringLiteral("Certificate fingerprint"));
  QCOMPARE(fixture.wizard.shortFingerprint(), QStringLiteral("00:01:02:03 … 1C:1D:1E:1F"));
  QCOMPARE(
      fixture.wizard.fullFingerprint(),
      QStringLiteral("00:01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F")
  );
  QVERIFY(fixture.wizard.sixDigitSas().isEmpty());
  QVERIFY(fixture.wizard.canCancel());
  QVERIFY(!fixture.wizard.canConfirmMatchingSas());
  QVERIFY(!fixture.wizard.terminal());
  QCOMPARE(requested.count(), 1);
  QVERIFY(changed.count() >= 1);
}

void PairingWizardModelTests::confirmsMatchingDisplayedCode()
{
  Fixture fixture;
  QVERIFY(fixture.wizard.start(peerSnapshot(), fixture.fingerprint));
  QVERIFY(advanceToComparison(fixture));

  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::AwaitingUserComparison));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Compare the code on both devices"));
  QCOMPARE(fixture.wizard.sixDigitSas(), QStringLiteral("000042"));
  QCOMPARE(fixture.wizard.attemptsRemaining(), 3);
  QCOMPARE(fixture.wizard.attemptsRemainingText(), QStringLiteral("3 attempts remaining"));
  QVERIFY(fixture.wizard.canConfirmMatchingSas());
  QVERIFY(fixture.wizard.confirmMatchingSas());
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Confirming));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Confirming pairing"));
  QVERIFY(fixture.wizard.sixDigitSas().isEmpty());
  QVERIFY(!fixture.wizard.canConfirmMatchingSas());
  QVERIFY(fixture.machine.complete(fixture.machine.snapshot()->pairingSessionId).ok());
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Completed));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Device paired"));
  QVERIFY(fixture.wizard.terminal());
}

void PairingWizardModelTests::validatesAndSubmitsEnteredCode()
{
  Fixture fixture;
  QSignalSpy failed(&fixture.wizard, &PairingWizardModel::actionFailed);
  QVERIFY(fixture.wizard.start(peerSnapshot(), fixture.fingerprint, QStringLiteral("123456")));
  QVERIFY(advanceToComparison(fixture));

  QVERIFY(!fixture.wizard.submitDisplayedSas(QStringLiteral("12a456")));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("Enter exactly six digits"));
  QCOMPARE(fixture.wizard.attemptsRemaining(), 3);

  QVERIFY(!fixture.wizard.submitDisplayedSas(QStringLiteral("999999")));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing code does not match"));
  QCOMPARE(fixture.wizard.attemptsRemaining(), 2);
  QCOMPARE(fixture.wizard.attemptsRemainingText(), QStringLiteral("2 attempts remaining"));

  QVERIFY(fixture.wizard.submitDisplayedSas(QStringLiteral("123456")));
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Confirming));
  QVERIFY(fixture.wizard.errorText().isEmpty());
  QCOMPARE(failed.count(), 2);

  Fixture exhaustedFixture;
  QVERIFY(exhaustedFixture.wizard.start(peerSnapshot(), exhaustedFixture.fingerprint));
  QVERIFY(advanceToComparison(exhaustedFixture));
  for (int attempt = 0; attempt < 3; ++attempt)
    QVERIFY(!exhaustedFixture.wizard.submitDisplayedSas(QStringLiteral("999999")));
  QCOMPARE(exhaustedFixture.wizard.state(), static_cast<int>(PairingState::Rejected));
  QCOMPARE(exhaustedFixture.wizard.errorText(), QStringLiteral("Too many incorrect attempts. Try again later."));
}

void PairingWizardModelTests::cancelsActiveSession()
{
  Fixture fixture;
  QVERIFY(fixture.wizard.start(peerSnapshot(), fixture.fingerprint));
  QVERIFY(fixture.wizard.cancel());
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Rejected));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Pairing canceled"));
  QVERIFY(fixture.wizard.terminal());
  QVERIFY(!fixture.wizard.canCancel());
}

void PairingWizardModelTests::exposesExpiryAndSharedErrorKeys()
{
  Fixture fixture;
  QVERIFY(fixture.wizard.start(peerSnapshot(), fixture.fingerprint));
  const auto expiresAt = fixture.machine.snapshot()->expiresAtUtc;
  QVERIFY(!fixture.wizard.expiresAtText().isEmpty());
  fixture.now = expiresAt;
  QVERIFY(fixture.wizard.expireIfNeeded());
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Expired));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("The pairing code expired. Generate a new code."));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing code expired. Generate a new code."));

  Fixture failedFixture;
  QVERIFY(failedFixture.wizard.start(peerSnapshot(), failedFixture.fingerprint));
  const auto session = failedFixture.machine.snapshot()->pairingSessionId;
  QVERIFY(failedFixture.machine.fail(session, QStringLiteral("pairing.certificate_changed")).ok());
  QCOMPARE(
      failedFixture.wizard.errorText(),
      QStringLiteral("The other device certificate changed. Automatic connection was stopped.")
  );
}

void PairingWizardModelTests::rejectsActionsWithoutSession()
{
  Fixture fixture;
  QSignalSpy failed(&fixture.wizard, &PairingWizardModel::actionFailed);
  QVERIFY(!fixture.wizard.confirmMatchingSas());
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing session is no longer available"));
  QVERIFY(!fixture.wizard.cancel());
  QCOMPARE(failed.count(), 2);
}

void PairingWizardModelTests::formatsMissingFingerprintSafely()
{
  Fixture fixture;
  QCOMPARE(fixture.wizard.shortFingerprint(), QStringLiteral("Fingerprint unavailable"));
  QCOMPARE(fixture.wizard.fullFingerprint(), QStringLiteral("Fingerprint unavailable"));
  QVERIFY(!fixture.wizard.start(peerSnapshot(), QByteArray(31, '\x2a')));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The device identity is not ready. Try again."));
}

QTEST_MAIN(PairingWizardModelTests)

#include "PairingWizardModelTests.moc"
