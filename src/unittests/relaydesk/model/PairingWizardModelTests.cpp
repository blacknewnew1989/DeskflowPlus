/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/PairingWizardModel.h"

#include "../FakePairingService.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::test;

namespace {

DeviceSnapshot peerSnapshot()
{
  return {
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .alias = QStringLiteral("Design Mac"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = DevicePresence::Pairing,
  };
}

struct Fixture
{
  PairingSnapshot makeSnapshot(
      PairingState state, QString sas = {}, int attemptsRemaining = 3, QString errorMessageKey = {}
  ) const
  {
    return {
        .pairingSessionId = sessionId,
        .peer = peer,
        .state = state,
        .sixDigitSas = std::move(sas),
        .expiresAtUtc = QDateTime::fromMSecsSinceEpoch(1'730'000'060'000LL, QTimeZone::UTC),
        .attemptsRemaining = attemptsRemaining,
        .errorMessageKey = std::move(errorMessageKey),
    };
  }

  void publish(PairingState state, QString sas = {}, int attemptsRemaining = 3, QString errorMessageKey = {})
  {
    service.publish(
        makeSnapshot(state, std::move(sas), attemptsRemaining, std::move(errorMessageKey)), fingerprint
    );
  }

  DeviceSnapshot peer = peerSnapshot();
  QUuid sessionId = QUuid::createUuid();
  QByteArray fingerprint =
      QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  FakePairingService service;
  PairingWizardModel wizard{service};
};

} // namespace

class PairingWizardModelTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void rendersCanonicalServiceSnapshot();
  void confirmsMatchingDisplayedCodeThroughService();
  void validatesAndSubmitsEnteredCodeThroughService();
  void cancelsActiveSessionThroughService();
  void exposesTerminalAndSharedErrorKeys();
  void mapsTypedServiceFailures();
  void rejectsActionsWithoutBoundSession();
  void formatsMissingFingerprintSafely();
};

void PairingWizardModelTests::rendersCanonicalServiceSnapshot()
{
  Fixture fixture;
  QSignalSpy changed(&fixture.wizard, &PairingWizardModel::changed);

  fixture.publish(PairingState::Requesting);
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
  QCOMPARE(changed.count(), 1);
}

void PairingWizardModelTests::confirmsMatchingDisplayedCodeThroughService()
{
  Fixture fixture;
  fixture.publish(PairingState::AwaitingUserComparison, QStringLiteral("000042"));

  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Compare the code on both devices"));
  QCOMPARE(fixture.wizard.sixDigitSas(), QStringLiteral("000042"));
  QCOMPARE(fixture.wizard.attemptsRemaining(), 3);
  QCOMPARE(fixture.wizard.attemptsRemainingText(), QStringLiteral("3 attempts remaining"));
  QVERIFY(fixture.wizard.canConfirmMatchingSas());
  QVERIFY(fixture.wizard.confirmMatchingSas());
  QCOMPARE(fixture.service.confirmCount, 1);
  QCOMPARE(fixture.service.lastConfirmedSession, fixture.sessionId);
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Confirming));
  QVERIFY(fixture.wizard.sixDigitSas().isEmpty());
  QVERIFY(!fixture.wizard.canConfirmMatchingSas());

  fixture.publish(PairingState::Completed);
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Device paired"));
  QVERIFY(fixture.wizard.terminal());
}

void PairingWizardModelTests::validatesAndSubmitsEnteredCodeThroughService()
{
  Fixture fixture;
  QSignalSpy failed(&fixture.wizard, &PairingWizardModel::actionFailed);
  fixture.publish(PairingState::AwaitingUserComparison);

  QVERIFY(!fixture.wizard.submitDisplayedSas(QStringLiteral("12a456")));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("Enter exactly six digits"));
  QCOMPARE(fixture.service.submitCount, 0);

  fixture.service.submitResult = {
      .error = PairingOperationError::InvalidCode,
      .stateError = PairingError::InvalidSas,
      .diagnostic = QStringLiteral("pairing code mismatch"),
  };
  QVERIFY(!fixture.wizard.submitDisplayedSas(QStringLiteral("999999")));
  QCOMPARE(fixture.service.lastSubmittedSession, fixture.sessionId);
  QCOMPARE(fixture.service.lastSubmittedSas, QStringLiteral("999999"));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing code does not match"));

  fixture.service.submitResult = {};
  QVERIFY(fixture.wizard.submitDisplayedSas(QStringLiteral("123456")));
  QCOMPARE(fixture.service.submitCount, 2);
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Confirming));
  QVERIFY(fixture.wizard.errorText().isEmpty());
  QCOMPARE(failed.count(), 2);
}

void PairingWizardModelTests::cancelsActiveSessionThroughService()
{
  Fixture fixture;
  fixture.publish(PairingState::AwaitingUserComparison);
  QVERIFY(fixture.wizard.cancel());
  QCOMPARE(fixture.service.cancelCount, 1);
  QCOMPARE(fixture.service.lastCancelledSession, fixture.sessionId);
  QCOMPARE(fixture.wizard.state(), static_cast<int>(PairingState::Rejected));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("Pairing canceled"));
  QVERIFY(fixture.wizard.terminal());
  QVERIFY(!fixture.wizard.canCancel());
}

void PairingWizardModelTests::exposesTerminalAndSharedErrorKeys()
{
  Fixture fixture;
  fixture.publish(PairingState::Expired, {}, 3, QStringLiteral("pairing.code.expired"));
  QCOMPARE(fixture.wizard.stateText(), QStringLiteral("The pairing code expired. Generate a new code."));
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing code expired. Generate a new code."));

  fixture.publish(PairingState::Failed, {}, 3, QStringLiteral("pairing.certificate_changed"));
  QCOMPARE(
      fixture.wizard.errorText(),
      QStringLiteral("The other device certificate changed. Automatic connection was stopped.")
  );
}

void PairingWizardModelTests::mapsTypedServiceFailures()
{
  Fixture fixture;
  fixture.publish(PairingState::AwaitingUserComparison);
  fixture.service.confirmResult = {
      .error = PairingOperationError::Expired,
      .diagnostic = QStringLiteral("pairing expired"),
  };
  QVERIFY(!fixture.wizard.confirmMatchingSas());
  QCOMPARE(fixture.wizard.errorText(), QStringLiteral("The pairing code expired. Generate a new code."));
}

void PairingWizardModelTests::rejectsActionsWithoutBoundSession()
{
  FakePairingService service;
  PairingWizardModel bound(service);
  QSignalSpy failed(&bound, &PairingWizardModel::actionFailed);
  QVERIFY(!bound.confirmMatchingSas());
  QCOMPARE(bound.errorText(), QStringLiteral("The pairing session is no longer available"));
  QVERIFY(!bound.cancel());

  PairingWizardModel unbound;
  QVERIFY(!unbound.submitDisplayedSas(QStringLiteral("123456")));
  QCOMPARE(failed.count(), 2);
}

void PairingWizardModelTests::formatsMissingFingerprintSafely()
{
  Fixture fixture;
  fixture.service.publish(fixture.makeSnapshot(PairingState::AwaitingUserComparison));
  QCOMPARE(fixture.wizard.shortFingerprint(), QStringLiteral("Fingerprint unavailable"));
  QCOMPARE(fixture.wizard.fullFingerprint(), QStringLiteral("Fingerprint unavailable"));
}

QTEST_MAIN(PairingWizardModelTests)

#include "PairingWizardModelTests.moc"
