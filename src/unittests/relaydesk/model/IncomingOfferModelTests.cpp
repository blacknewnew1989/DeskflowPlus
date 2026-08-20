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
  void preservesAskConflictPolicyOnAcceptance();
  void presentsEveryConfiguredConflictPolicy();
};

void IncomingOfferModelTests::presentsAndAcceptsValidatedOffer()
{
  IncomingOfferModel model(settings());
  const auto incoming = incomingOffer();
  QSignalSpy changed(&model, &IncomingOfferModel::changed);
  QSignalSpy accepted(&model, &IncomingOfferModel::acceptRequested);

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
  const auto acceptance = accepted.constFirst();
  QCOMPARE(*static_cast<const TransferId *>(acceptance.at(0).constData()), incoming.offer.transferId);
  const auto options = acceptance.at(1).value<ReceiveOptions>();
  QCOMPARE(options.destinationRoot, QStringLiteral("Downloads/RelayDesk"));
  QCOMPARE(options.conflictPolicy, ConflictPolicy::AutoRename);
  QCOMPARE(options.acceptanceOrigin, AcceptanceOrigin::UserDecision);
  QVERIFY(!model.accept());
  QCOMPARE(accepted.count(), 1);
}

void IncomingOfferModelTests::rejectsAndIgnoresDuplicateDecisions()
{
  IncomingOfferModel model(settings());
  const auto incoming = incomingOffer();
  QSignalSpy changed(&model, &IncomingOfferModel::changed);
  QSignalSpy rejected(&model, &IncomingOfferModel::rejectRequested);

  QVERIFY(model.receiveOffer(incoming));
  auto duplicate = incoming;
  duplicate.peerDisplayName = QStringLiteral("Changed duplicate name");
  QVERIFY(model.receiveOffer(duplicate));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.headingText(), QStringLiteral("Studio Mac wants to send"));

  QVERIFY(model.reject());
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::Rejected);
  const auto rejection = rejected.constFirst();
  QCOMPARE(*static_cast<const TransferId *>(rejection.at(0).constData()), incoming.offer.transferId);
  QCOMPARE(rejection.at(1).value<RejectReason>(), RejectReason::UserDeclined);
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
    IncomingOfferModel model(settings(testCase.explicitSetting));
    QSignalSpy accepted(&model, &IncomingOfferModel::acceptRequested);
    QVERIFY(model.receiveOffer(incomingOffer(testCase.trusted, testCase.mayAutoAccept)));
    QCOMPARE(model.status() == IncomingOfferModel::Status::Accepted, testCase.expectedAccepted);
    QCOMPARE(accepted.count(), testCase.expectedAccepted ? 1 : 0);
    if (testCase.expectedAccepted) {
      QCOMPARE(
          *static_cast<const TransferId *>(accepted.constFirst().at(0).constData()), model.offer()->offer.transferId
      );
      QCOMPARE(
          accepted.constFirst().at(1).value<ReceiveOptions>().acceptanceOrigin, AcceptanceOrigin::TrustedDevicePolicy
      );
    }
  }
}

void IncomingOfferModelTests::expiresOnceAndCanBeDismissed()
{
  qint64 now = 10'000;
  auto expirySettings = settings();
  expirySettings.decisionTimeoutMs = 1000;
  IncomingOfferModel model(expirySettings, [&now]() { return now; });
  const auto incoming = incomingOffer();
  QSignalSpy rejected(&model, &IncomingOfferModel::rejectRequested);

  QVERIFY(model.receiveOffer(incoming));
  now += 999;
  QVERIFY(!model.expireIfNeeded());
  now += 1;
  QVERIFY(model.expireIfNeeded());
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(model.status(), IncomingOfferModel::Status::Expired);
  QVERIFY(model.visible());
  QCOMPARE(model.errorText(), QStringLiteral("This transfer request expired"));
  QCOMPARE(*static_cast<const TransferId *>(rejected.constFirst().at(0).constData()), incoming.offer.transferId);
  QCOMPARE(rejected.constFirst().at(1).value<RejectReason>(), RejectReason::PolicyDenied);
  QVERIFY(!model.expireIfNeeded());
  QCOMPARE(rejected.count(), 1);

  model.dismiss();
  QVERIFY(!model.visible());
  QVERIFY(model.receiveOffer(incoming));
  QCOMPARE(rejected.count(), 1);
}

void IncomingOfferModelTests::blocksUntrustedPeerAndMapsErrorsSafely()
{
  IncomingOfferModel model(settings(true));
  QVERIFY(model.receiveOffer(incomingOffer(false, true)));
  QVERIFY(model.visible());
  QVERIFY(!model.canAccept());
  QVERIFY(!model.accept());
  QCOMPARE(model.errorText(), QStringLiteral("Pair this device before receiving files"));
  QVERIFY(model.reject());
}

void IncomingOfferModelTests::settingsUpdateEmitsAndReevaluatesAvailability()
{
  auto insufficient = settings();
  insufficient.availableBytes = 1;
  IncomingOfferModel model(insufficient);
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

void IncomingOfferModelTests::preservesAskConflictPolicyOnAcceptance()
{
  auto askSettings = settings();
  askSettings.defaultConflictPolicy = ConflictPolicy::Ask;
  IncomingOfferModel model(askSettings);
  QSignalSpy accepted(&model, &IncomingOfferModel::acceptRequested);

  QVERIFY(model.receiveOffer(incomingOffer()));
  QCOMPARE(model.conflictText(), QStringLiteral("Conflict: ask when a file already exists"));
  QVERIFY(model.accept());
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(accepted.constFirst().at(1).value<ReceiveOptions>().conflictPolicy, ConflictPolicy::Ask);
}

void IncomingOfferModelTests::presentsEveryConfiguredConflictPolicy()
{
  struct Case
  {
    ConflictPolicy policy;
    QString expected;
  };
  const QList<Case> cases{
      {ConflictPolicy::AutoRename, QStringLiteral("Conflict: auto rename")},
      {ConflictPolicy::Ask, QStringLiteral("Conflict: ask when a file already exists")},
      {ConflictPolicy::Overwrite, QStringLiteral("Replace")},
      {ConflictPolicy::Skip, QStringLiteral("Skip")},
  };
  for (const auto &testCase : cases) {
    auto incomingSettings = settings();
    incomingSettings.defaultConflictPolicy = testCase.policy;
    IncomingOfferModel model(incomingSettings);
    QCOMPARE(model.conflictText(), testCase.expected);
  }
}

QTEST_MAIN(IncomingOfferModelTests)

#include "IncomingOfferModelTests.moc"
