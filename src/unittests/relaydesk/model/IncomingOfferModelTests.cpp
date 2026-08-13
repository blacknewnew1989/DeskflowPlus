/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/IncomingOfferModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace relaydesk::transfer;

namespace {

NegotiatedCapabilities capabilities()
{
  return {
      .protocolMajorVersion = kProtocolMajorVersion,
      .features = {QStringLiteral("file.v1"), QStringLiteral("folder.v1")},
      .chunkBytes = 1024,
      .maxPayloadBytes = 4096,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 1000,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask},
  };
}

TransferOffer transferOffer()
{
  return {
      .transferId = TransferId::generate(),
      .displayName = QStringLiteral("Project"),
      .totalBytes = 2048,
      .fileCount = 2,
      .directoryCount = 1,
      .manifestSha256 = QByteArray(kSha256Bytes, '\x2a'),
      .manifestPageCount = 1,
      .requestedConflictPolicy = ConflictPolicy::AutoRename,
      .createdAtMs = 123,
  };
}

IncomingOffer incomingOffer(bool trusted = true, bool mayAutoAccept = false)
{
  return {
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .offer = transferOffer(),
      .peerTrusted = trusted,
      .mayAutoAccept = mayAutoAccept,
  };
}

IncomingOfferSettingsSnapshot settings(bool autoAccept = false)
{
  return {
      .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
      .availableBytes = 1'000'000,
      .autoAcceptTrustedDevices = autoAccept,
      .decisionTimeoutMs = 5000,
  };
}

} // namespace

class IncomingOfferModelTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void presentsAndAcceptsValidatedOffer();
  void rejectsAndIgnoresDuplicateDecisions();
  void autoAcceptRequiresAllExplicitConditions();
  void expiresOnceAndCanBeDismissed();
  void blocksUntrustedPeerAndMapsErrorsSafely();
  void settingsUpdateEmitsAndReevaluatesAvailability();
};

void IncomingOfferModelTests::presentsAndAcceptsValidatedOffer()
{
  TransferOfferStateMachine stateMachine(capabilities());
  IncomingOfferModel model(stateMachine, settings());
  const auto incoming = incomingOffer();
  QSignalSpy changed(&model, &IncomingOfferModel::changed);
  QSignalSpy accepted(&model, &IncomingOfferModel::acceptanceReady);

  QVERIFY(model.receiveOffer(incoming));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::AwaitingDecision);
  QVERIFY(model.visible());
  QVERIFY(model.active());
  QVERIFY(model.canAccept());
  QCOMPARE(model.headingText(), QStringLiteral("Studio Mac wants to send"));
  QCOMPARE(model.offerName(), QStringLiteral("Project"));
  QVERIFY(model.summaryText().startsWith(QStringLiteral("3 items · ")));
  QCOMPARE(model.destinationText(), QStringLiteral("Save to: Downloads/RelayDesk"));
  QCOMPARE(model.conflictText(), QStringLiteral("Conflict: auto rename"));
  QVERIFY(model.errorText().isEmpty());

  QVERIFY(model.accept());
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::Accepted);
  QVERIFY(!model.visible());
  const auto acceptance = model.acceptance();
  QVERIFY(acceptance.has_value());
  QCOMPARE(acceptance->transferId, incoming.offer.transferId);
  QCOMPARE(acceptance->effectiveConflictPolicy, ConflictPolicy::AutoRename);
  QCOMPARE(acceptance->logicalDestination, QStringLiteral("Downloads/RelayDesk"));
  QCOMPARE(acceptance->freeBytes, 1'000'000ULL);
  QVERIFY(!acceptance->autoAccepted);
  QVERIFY(!model.accept());
  QCOMPARE(accepted.count(), 1);
}

void IncomingOfferModelTests::rejectsAndIgnoresDuplicateDecisions()
{
  TransferOfferStateMachine stateMachine(capabilities());
  IncomingOfferModel model(stateMachine, settings());
  const auto incoming = incomingOffer();
  QSignalSpy changed(&model, &IncomingOfferModel::changed);
  QSignalSpy rejected(&model, &IncomingOfferModel::rejectionReady);

  QVERIFY(model.receiveOffer(incoming));
  auto duplicate = incoming;
  duplicate.peerDisplayName = QStringLiteral("Changed duplicate name");
  QVERIFY(model.receiveOffer(duplicate));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.headingText(), QStringLiteral("Studio Mac wants to send"));

  QVERIFY(model.reject());
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::Rejected);
  const auto rejection = model.rejection();
  QVERIFY(rejection.has_value());
  QCOMPARE(rejection->reason, RejectReason::UserDeclined);
  QVERIFY(rejection->diagnostic.isEmpty());
  QVERIFY(!model.reject());
  QVERIFY(model.receiveOffer(duplicate));
  QCOMPARE(rejected.count(), 1);
}

void IncomingOfferModelTests::autoAcceptRequiresAllExplicitConditions()
{
  struct Case
  {
    bool trusted;
    bool mayAutoAccept;
    bool explicitSetting;
    bool expectedAccepted;
  };
  const QList<Case> cases{
      {true, true, true, true},
      {false, true, true, false},
      {true, false, true, false},
      {true, true, false, false},
  };

  for (const auto &testCase : cases) {
    TransferOfferStateMachine stateMachine(capabilities());
    IncomingOfferModel model(stateMachine, settings(testCase.explicitSetting));
    QSignalSpy accepted(&model, &IncomingOfferModel::acceptanceReady);
    QVERIFY(model.receiveOffer(incomingOffer(testCase.trusted, testCase.mayAutoAccept)));
    QCOMPARE(model.status() == IncomingOfferModel::Status::Accepted, testCase.expectedAccepted);
    QCOMPARE(accepted.count(), testCase.expectedAccepted ? 1 : 0);
    if (testCase.expectedAccepted) {
      QVERIFY(model.acceptance().has_value());
      QVERIFY(model.acceptance()->autoAccepted);
    }
  }
}

void IncomingOfferModelTests::expiresOnceAndCanBeDismissed()
{
  qint64 now = 10'000;
  TransferOfferStateMachine stateMachine(capabilities());
  auto expirySettings = settings();
  expirySettings.decisionTimeoutMs = 1000;
  IncomingOfferModel model(stateMachine, expirySettings, [&now]() { return now; });
  const auto incoming = incomingOffer();
  QSignalSpy rejected(&model, &IncomingOfferModel::rejectionReady);

  QVERIFY(model.receiveOffer(incoming));
  now += 999;
  QVERIFY(!model.expireIfNeeded());
  now += 1;
  QVERIFY(model.expireIfNeeded());
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::Expired);
  QVERIFY(model.visible());
  QCOMPARE(model.errorText(), QStringLiteral("This transfer request expired"));
  QVERIFY(model.rejection().has_value());
  QCOMPARE(model.rejection()->reason, RejectReason::PolicyDenied);
  QVERIFY(model.rejection()->diagnostic.isEmpty());
  QVERIFY(!model.expireIfNeeded());
  QCOMPARE(rejected.count(), 1);

  model.dismiss();
  QVERIFY(!model.visible());
  QVERIFY(model.receiveOffer(incoming));
  QCOMPARE(rejected.count(), 1);
}

void IncomingOfferModelTests::blocksUntrustedPeerAndMapsErrorsSafely()
{
  TransferOfferStateMachine stateMachine(capabilities());
  IncomingOfferModel model(stateMachine, settings(true));
  QVERIFY(model.receiveOffer(incomingOffer(false, true)));
  QVERIFY(model.visible());
  QVERIFY(!model.canAccept());
  QVERIFY(!model.accept());
  QCOMPARE(model.errorText(), QStringLiteral("Pair this device before receiving files"));
  QVERIFY(model.reject());

  TransferOfferStateMachine invalidStateMachine(capabilities());
  IncomingOfferModel invalidModel(invalidStateMachine, settings());
  auto invalid = incomingOffer();
  invalid.offer.displayName.clear();
  QVERIFY(!invalidModel.receiveOffer(invalid));
  QCOMPARE(invalidModel.status(), IncomingOfferModel::Status::Error);
  QCOMPARE(invalidModel.errorText(), QStringLiteral("The incoming transfer request is invalid"));
  QVERIFY(!invalidModel.errorText().contains(QStringLiteral("fields exceed")));
  QVERIFY(!invalidModel.errorText().contains(QStringLiteral("diagnostic")));
}

void IncomingOfferModelTests::settingsUpdateEmitsAndReevaluatesAvailability()
{
  TransferOfferStateMachine stateMachine(capabilities());
  auto insufficient = settings();
  insufficient.availableBytes = 1;
  IncomingOfferModel model(stateMachine, insufficient);
  QVERIFY(model.receiveOffer(incomingOffer()));
  QCOMPARE(model.errorText(), QStringLiteral("Not enough disk space"));
  QVERIFY(!model.canAccept());
  QSignalSpy changed(&model, &IncomingOfferModel::changed);

  model.setSettings(settings());
  QCOMPARE(changed.count(), 1);
  QVERIFY(model.canAccept());
  QVERIFY(model.errorText().isEmpty());
  model.setSettings(settings());
  QCOMPARE(changed.count(), 1);
}

QTEST_MAIN(IncomingOfferModelTests)

#include "IncomingOfferModelTests.moc"
