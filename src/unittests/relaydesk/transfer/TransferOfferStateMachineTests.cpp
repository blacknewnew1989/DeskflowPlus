// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferOfferStateMachine.h"

#include "relaydesk/transfer/TransferTypes.h"

#include <QTest>

#include <type_traits>

using namespace relaydesk::transfer;

namespace {

NegotiatedCapabilities capabilities()
{
  return {
      .protocolMajorVersion = 1,
      .features = {QStringLiteral("file.v1"), QStringLiteral("folder.v1"), QStringLiteral("sha256")},
      .chunkBytes = 1024,
      .maxPayloadBytes = 4096,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 100,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask},
  };
}

TransferOffer offer()
{
  return {
      .transferId = QUuid::createUuid(),
      .displayName = QStringLiteral("Project"),
      .totalBytes = 1024,
      .fileCount = 2,
      .directoryCount = 1,
      .manifestSha256 = QByteArray(32, '\x23'),
      .manifestPageCount = 1,
      .requestedConflictPolicy = ConflictPolicy::Ask,
      .createdAtMs = 1'730'000'000'000ULL,
  };
}

} // namespace

class TransferOfferStateMachineTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void outgoingOfferAcceptsMatchingResponse();
  void outgoingOfferHandlesRejection();
  void incomingOfferProducesAcceptance();
  void incomingOfferProducesRejection();
  void rejectsConcurrentAndRepeatedDecisions();
  void rejectsTransferIdMismatch();
  void enforcesNegotiatedFeaturesEntriesAndPolicies();
  void rejectsInvalidDestinationAndFreeSpace();
  void failIsTerminalUntilReset();
  void exposesCopyableUiContract();
};

void TransferOfferStateMachineTests::outgoingOfferAcceptsMatchingResponse()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.beginOutgoing(source).ok());
  QCOMPARE(machine.snapshot()->state, OfferState::AwaitingPeerDecision);
  QVERIFY(machine
              .receiveAccept(
                  {source.transferId, ConflictPolicy::AutoRename, QStringLiteral("Downloads/RelayDesk"), 2048, false}
              )
              .ok());
  QCOMPARE(machine.snapshot()->state, OfferState::Accepted);
  QVERIFY(machine.snapshot()->acceptance.has_value());
}

void TransferOfferStateMachineTests::outgoingOfferHandlesRejection()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.beginOutgoing(source).ok());
  QVERIFY(machine.receiveReject({source.transferId, RejectReason::UserDeclined, {}}).ok());
  QCOMPARE(machine.snapshot()->state, OfferState::Rejected);
  QCOMPARE(machine.snapshot()->rejection->reason, RejectReason::UserDeclined);
}

void TransferOfferStateMachineTests::incomingOfferProducesAcceptance()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.receiveIncoming(source).ok());
  QCOMPARE(machine.snapshot()->direction, OfferDirection::Incoming);
  QVERIFY(machine.acceptIncoming(ConflictPolicy::AutoRename, QStringLiteral(" Downloads/RelayDesk "), 4096, true).ok());
  QCOMPARE(machine.snapshot()->state, OfferState::Accepted);
  QCOMPARE(machine.snapshot()->acceptance->transferId, source.transferId);
  QCOMPARE(machine.snapshot()->acceptance->logicalDestination, QStringLiteral("Downloads/RelayDesk"));
  QVERIFY(machine.snapshot()->acceptance->autoAccepted);
}

void TransferOfferStateMachineTests::incomingOfferProducesRejection()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.receiveIncoming(source).ok());
  QVERIFY(machine.rejectIncoming(RejectReason::InsufficientSpace, QStringLiteral("disk space snapshot too small")).ok()
  );
  QCOMPARE(machine.snapshot()->state, OfferState::Rejected);
  QCOMPARE(machine.snapshot()->rejection->transferId, source.transferId);
}

void TransferOfferStateMachineTests::rejectsConcurrentAndRepeatedDecisions()
{
  TransferOfferStateMachine machine(capabilities());
  const auto first = offer();
  QVERIFY(machine.beginOutgoing(first).ok());
  QCOMPARE(machine.receiveIncoming(offer()).error, OfferStateError::ActiveOfferExists);
  QVERIFY(machine.receiveReject({first.transferId, RejectReason::Busy, {}}).ok());
  QCOMPARE(machine.receiveReject({first.transferId, RejectReason::Busy, {}}).error, OfferStateError::InvalidState);
}

void TransferOfferStateMachineTests::rejectsTransferIdMismatch()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.beginOutgoing(source).ok());
  QCOMPARE(
      machine.receiveAccept({QUuid::createUuid(), ConflictPolicy::Ask, QStringLiteral("Downloads"), 4096, false}).error,
      OfferStateError::TransferIdMismatch
  );
  QCOMPARE(machine.snapshot()->state, OfferState::AwaitingPeerDecision);
}

void TransferOfferStateMachineTests::enforcesNegotiatedFeaturesEntriesAndPolicies()
{
  auto negotiated = capabilities();
  auto source = offer();
  negotiated.features.removeAll(QStringLiteral("folder.v1"));
  TransferOfferStateMachine noFolders(negotiated);
  QCOMPARE(noFolders.beginOutgoing(source).error, OfferStateError::CapabilityUnavailable);

  negotiated = capabilities();
  negotiated.maxManifestEntries = 2;
  TransferOfferStateMachine bounded(negotiated);
  QCOMPARE(bounded.beginOutgoing(source).error, OfferStateError::InvalidOffer);

  source.directoryCount = 0;
  source.fileCount = 1;
  source.requestedConflictPolicy = ConflictPolicy::Overwrite;
  TransferOfferStateMachine noOverwrite(capabilities());
  QCOMPARE(noOverwrite.beginOutgoing(source).error, OfferStateError::ConflictPolicyUnavailable);
}

void TransferOfferStateMachineTests::rejectsInvalidDestinationAndFreeSpace()
{
  TransferOfferStateMachine machine(capabilities());
  const auto source = offer();
  QVERIFY(machine.receiveIncoming(source).ok());
  QCOMPARE(machine.acceptIncoming(ConflictPolicy::Ask, {}, 4096, false).error, OfferStateError::InvalidResponse);
  QCOMPARE(
      machine.acceptIncoming(ConflictPolicy::Ask, QStringLiteral("Downloads"), 100, false).error,
      OfferStateError::InvalidResponse
  );
  QCOMPARE(
      machine.acceptIncoming(ConflictPolicy::Overwrite, QStringLiteral("Downloads"), 4096, false).error,
      OfferStateError::ConflictPolicyUnavailable
  );
  QCOMPARE(machine.snapshot()->state, OfferState::AwaitingLocalDecision);
}

void TransferOfferStateMachineTests::failIsTerminalUntilReset()
{
  TransferOfferStateMachine machine(capabilities());
  QVERIFY(machine.receiveIncoming(offer()).ok());
  QVERIFY(machine.fail(QStringLiteral("transfer.offer.failed")).ok());
  QCOMPARE(machine.snapshot()->state, OfferState::Failed);
  QCOMPARE(machine.fail(QStringLiteral("again")).error, OfferStateError::InvalidState);
  machine.reset();
  QVERIFY(!machine.snapshot().has_value());
  QVERIFY(machine.receiveIncoming(offer()).ok());
}

void TransferOfferStateMachineTests::exposesCopyableUiContract()
{
  static_assert(std::is_copy_constructible_v<SendOptions>);
  static_assert(std::is_copy_constructible_v<IncomingOffer>);
  QCOMPARE(SendOptions{}.conflictPolicy, ConflictPolicy::AutoRename);

  const auto source = offer();
  const IncomingOffer incoming{
      .peerDeviceId = deskflow::relaydesk::DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Office Mac"),
      .offer = source,
      .peerTrusted = true,
      .mayAutoAccept = false,
  };
  const IncomingOffer copy = incoming;
  QCOMPARE(copy, incoming);
  QCOMPARE(copy.offer, source);
  QVERIFY(QMetaType::fromType<IncomingOffer>().isValid());
  QVERIFY(QMetaType::fromType<SendOptions>().isValid());
}

QTEST_MAIN(TransferOfferStateMachineTests)

#include "TransferOfferStateMachineTests.moc"
