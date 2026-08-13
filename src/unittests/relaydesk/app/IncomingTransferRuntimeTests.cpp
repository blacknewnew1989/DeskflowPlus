/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <optional>

using namespace deskflow::relaydesk;
using namespace relaydesk::transfer;

namespace {

class FakeFileSafety final : public IPlatformFileSafety
{
public:
  FileSafetyResult verifyReceiveRoot(const VerifyReceiveRootRequest &request) const override
  {
    verifiedRoot = request.receiveRoot;
    verificationThread = QThread::currentThread();
    return rootResult;
  }

  FileSafetyResult verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &) const override
  {
    return {};
  }

  FileSafetyResult commitStagedFile(const CommitStagedFileRequest &) override
  {
    return {};
  }

  mutable QString verifiedRoot;
  mutable QThread *verificationThread = nullptr;
  FileSafetyResult rootResult;
};

NegotiatedCapabilities receiverCapabilities()
{
  return {
      .protocolMajorVersion = kProtocolMajorVersion,
      .features = {QStringLiteral("file.v1"), QStringLiteral("sha256"),
                   QStringLiteral("file.receive.v1")},
      .chunkBytes = 1024U * 1024U,
      .maxPayloadBytes = 4U * 1024U * 1024U,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 1,
      .maxManifestEntries = 1000,
      .conflictPolicies = {ConflictPolicy::AutoRename},
      .localCanReceiveFiles = true,
      .peerCanReceiveFiles = false,
  };
}

TransferOffer offer()
{
  return {
      .transferId = TransferId::generate(),
      .displayName = QStringLiteral("payload.bin"),
      .totalBytes = 0,
      .fileCount = 1,
      .manifestSha256 = QByteArray(kSha256Bytes, '\x31'),
      .manifestPageCount = 1,
      .requestedConflictPolicy = ConflictPolicy::AutoRename,
      .createdAtMs = 1,
  };
}

const TransferOperationResult &operation(const QSignalSpy &spy, qsizetype index)
{
  const auto &argument = spy.at(index).constFirst();
  const auto *result = static_cast<const TransferOperationResult *>(argument.constData());
  Q_ASSERT(result != nullptr);
  return *result;
}

} // namespace

class IncomingTransferRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void validatesAndPublishesIncomingOffer();
  void acceptsAfterWorkerFileSafetyPreflight();
  void rejectsUnsafeRootAndUntrustedAutomaticAcceptance();
  void rejectsAndPublishesExactlyOneTypedResultPerIntent();
  void refusesUnnegotiatedAndUnknownTransfers();
};

void IncomingTransferRuntimeTests::validatesAndPublishesIncomingOffer()
{
  FakeFileSafety safety;
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  std::optional<IncomingOffer> published;
  connect(&runtime, &IncomingTransferRuntime::incomingOffer, this, [&](const IncomingOffer &incoming) {
    published = incoming;
  });
  const auto incoming = offer();
  const auto peer = DeviceId::generate();
  QString diagnostic;

  QVERIFY2(
      runtime.receiveOffer(
          peer, QStringLiteral("Trusted peer"), true, receiverCapabilities(), incoming, &diagnostic
      ),
      qPrintable(diagnostic)
  );
  QVERIFY(published.has_value());
  QCOMPARE(published->peerDeviceId, peer);
  QCOMPARE(published->peerDisplayName, QStringLiteral("Trusted peer"));
  QCOMPARE(published->offer, incoming);
  QVERIFY(published->peerTrusted);
  QVERIFY(!published->mayAutoAccept);
  QVERIFY(!runtime.receiveOffer(peer, QStringLiteral("Duplicate"), true, receiverCapabilities(), incoming));
}

void IncomingTransferRuntimeTests::acceptsAfterWorkerFileSafetyPreflight()
{
  FakeFileSafety safety;
  QThreadPool pool;
  pool.setMaxThreadCount(1);
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  const auto incoming = offer();
  const auto peer = DeviceId::generate();
  QTemporaryDir receiveRoot;
  QVERIFY(receiveRoot.isValid());
  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), incoming));
  std::optional<TransferAccept> response;
  connect(&runtime, &IncomingTransferRuntime::transferAccepted, this, [&](auto responsePeer, auto acceptance) {
    QCOMPARE(responsePeer, peer);
    response = std::move(acceptance);
  });

  runtime.accept(incoming.transferId, {.destinationRoot = receiveRoot.path()});

  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);
  QVERIFY(response.has_value());
  QCOMPARE(response->transferId, incoming.transferId);
  QCOMPARE(response->effectiveConflictPolicy, ConflictPolicy::AutoRename);
  QVERIFY(!response->autoAccepted);
  QCOMPARE(safety.verifiedRoot, receiveRoot.path());
  QVERIFY(safety.verificationThread != nullptr);
  QVERIFY(safety.verificationThread != QThread::currentThread());
  const auto accepted = operation(operations, 0);
  QCOMPARE(accepted.operation, TransferOperation::Accept);
  QCOMPARE(accepted.outcome, TransferOperationOutcome::Applied);
  QVERIFY(accepted.ok());

  runtime.accept(incoming.transferId, {.destinationRoot = receiveRoot.path()});
  QCOMPARE(operations.count(), 2);
  QCOMPARE(operation(operations, 1).outcome, TransferOperationOutcome::Idempotent);
}

void IncomingTransferRuntimeTests::rejectsAndPublishesExactlyOneTypedResultPerIntent()
{
  FakeFileSafety safety;
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  const auto incoming = offer();
  const auto peer = DeviceId::generate();
  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), false, receiverCapabilities(), incoming));
  std::optional<TransferReject> response;
  connect(&runtime, &IncomingTransferRuntime::transferRejected, this, [&](auto responsePeer, auto rejection) {
    QCOMPARE(responsePeer, peer);
    response = std::move(rejection);
  });

  runtime.reject(incoming.transferId, RejectReason::UserDeclined);
  QCOMPARE(operations.count(), 1);
  QVERIFY(response.has_value());
  QCOMPARE(response->transferId, incoming.transferId);
  QCOMPARE(response->reason, RejectReason::UserDeclined);
  QCOMPARE(operation(operations, 0).outcome, TransferOperationOutcome::Applied);

  runtime.reject(incoming.transferId, RejectReason::UserDeclined);
  QCOMPARE(operations.count(), 2);
  QCOMPARE(operation(operations, 1).outcome, TransferOperationOutcome::Idempotent);
}

void IncomingTransferRuntimeTests::rejectsUnsafeRootAndUntrustedAutomaticAcceptance()
{
  FakeFileSafety safety;
  safety.rootResult = {
      .error = FileSafetyError::LinkTraversalDetected,
      .diagnostic = QStringLiteral("receive root is a symbolic link"),
  };
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  const auto incoming = offer();
  QTemporaryDir receiveRoot;
  QVERIFY(receiveRoot.isValid());
  QVERIFY(runtime.receiveOffer(
      DeviceId::generate(), QStringLiteral("Untrusted peer"), false, receiverCapabilities(), incoming
  ));

  runtime.accept(
      incoming.transferId,
      {.destinationRoot = receiveRoot.path(),
       .acceptanceOrigin = AcceptanceOrigin::TrustedDevicePolicy}
  );
  QCOMPARE(operations.count(), 1);
  QCOMPARE(operation(operations, 0).outcome, TransferOperationOutcome::Rejected);
  QCOMPARE(operation(operations, 0).error, TransferOperationError::InvalidState);
  QVERIFY(safety.verifiedRoot.isEmpty());

  runtime.accept(incoming.transferId, {.destinationRoot = receiveRoot.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 2, 5'000);
  QCOMPARE(operation(operations, 1).outcome, TransferOperationOutcome::Rejected);
  QCOMPARE(operation(operations, 1).error, TransferOperationError::InvalidState);
  QCOMPARE(safety.verifiedRoot, receiveRoot.path());
}

void IncomingTransferRuntimeTests::refusesUnnegotiatedAndUnknownTransfers()
{
  FakeFileSafety safety;
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  auto capabilities = receiverCapabilities();
  capabilities.localCanReceiveFiles = false;
  const auto incoming = offer();
  QString diagnostic;
  QVERIFY(!runtime.receiveOffer(
      DeviceId::generate(), QStringLiteral("Peer"), true, capabilities, incoming, &diagnostic
  ));
  QVERIFY(diagnostic.contains(QStringLiteral("file.receive.v1")));

  const auto unknown = TransferId::generate();
  runtime.accept(unknown, {});
  runtime.reject(unknown, RejectReason::UserDeclined);
  QCOMPARE(operations.count(), 2);
  QCOMPARE(operation(operations, 0).error, TransferOperationError::UnknownTransfer);
  QCOMPARE(operation(operations, 1).error, TransferOperationError::UnknownTransfer);
}

QTEST_MAIN(IncomingTransferRuntimeTests)

#include "IncomingTransferRuntimeTests.moc"
