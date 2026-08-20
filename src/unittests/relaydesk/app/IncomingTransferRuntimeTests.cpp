/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <memory>
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
  void receivesAcceptedFileThroughAtomicCommit();
  void receivesPagedEmptyDirectoryManifest();
  void disconnectInterruptsAcceptedPipeline();
  void cancelledTransferOnlyIgnoresValidatedTailFrames();
  void activeAndCancelledSessionsRouteDataByTransferId();
  void pendingConflictLifecycle_data();
  void pendingConflictLifecycle();
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

void IncomingTransferRuntimeTests::receivesAcceptedFileThroughAtomicCommit()
{
  class CommitFileSafety final : public IPlatformFileSafety
  {
  public:
    FileSafetyResult verifyReceiveRoot(const VerifyReceiveRootRequest &) const override
    {
      return {};
    }
    FileSafetyResult verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &) const override
    {
      return {};
    }
    FileSafetyResult commitStagedFile(const CommitStagedFileRequest &request) override
    {
      commitRequest = request;
      return QFile::rename(request.stagingPath, request.destinationPath)
                 ? FileSafetyResult{}
                 : FileSafetyResult{
                       .error = FileSafetyError::CommitFailed,
                       .diagnostic = QStringLiteral("test atomic move failed"),
                   };
    }
    std::optional<CommitStagedFileRequest> commitRequest;
  } safety;

  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray bytes(1024U * 1024U + 31U, '\x61');
  const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  const auto transferId = TransferId::generate();
  const auto fileId = FileId::generate();
  const ManifestEntry entry{
      .id = fileId,
      .relativeProtocolPath = QStringLiteral("received.bin"),
      .type = ManifestEntryType::File,
      .size = static_cast<quint64>(bytes.size()),
      .modifiedUtc = QDateTime::currentDateTimeUtc(),
      .sha256 = digest,
  };
  QString diagnostic;
  const QByteArray manifestDigest = ManifestPageCodec::canonicalSha256({entry}, &diagnostic);
  QVERIFY2(!manifestDigest.isEmpty(), qPrintable(diagnostic));
  const TransferOffer incoming{
      .transferId = transferId,
      .displayName = QStringLiteral("received.bin"),
      .totalBytes = static_cast<quint64>(bytes.size()),
      .fileCount = 1,
      .manifestSha256 = manifestDigest,
      .manifestPageCount = 1,
      .requestedConflictPolicy = ConflictPolicy::AutoRename,
      .createdAtMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
  };
  const auto peer = DeviceId::generate();
  QThreadPool pool;
  pool.setMaxThreadCount(2);
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    latest = snapshot;
  });
  std::optional<FileResultMessage> fileResult;
  std::optional<FileCheckpointMessage> checkpoint;
  connect(&runtime, &IncomingTransferRuntime::responseReady, this, [&](auto responsePeer, const Frame &frame) {
    QCOMPARE(responsePeer, peer);
    QCOMPARE(frame.streamId, quint32{1});
    const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
    QVERIFY(decoded.ok());
    if (decoded.ok()) {
      if (const auto *received = std::get_if<FileResultMessage>(&*decoded.message)) {
        fileResult = *received;
      } else if (const auto *received = std::get_if<FileCheckpointMessage>(&*decoded.message)) {
        checkpoint = *received;
      }
    }
  });

  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), incoming));
  runtime.accept(transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);
  QCOMPARE(operation(operations, 0).outcome, TransferOperationOutcome::Applied);

  Frame manifestPage{
      .type = MessageType::ManifestPage,
      .metadata = ManifestPageCodec::encode(
          {.transferId = transferId, .pageIndex = 0, .pageCount = 1, .entries = {entry}},
          {}, &diagnostic
      ),
  };
  QVERIFY2(!manifestPage.metadata.isEmpty(), qPrintable(diagnostic));
  QVERIFY(runtime.enqueueFrame(peer, manifestPage, &diagnostic));
  Frame manifestComplete{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = transferId, .canonicalSha256 = manifestDigest}, &diagnostic
      ),
  };
  QVERIFY(runtime.enqueueFrame(peer, manifestComplete, &diagnostic));

  const FileBeginMessage begin{
      .transferId = transferId,
      .fileId = fileId,
      .size = static_cast<quint64>(bytes.size()),
      .chunkBytes = 1024U * 1024U,
      .expectedSha256 = digest,
  };
  Frame beginFrame{
      .type = MessageType::FileBegin,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(FileControlMessage{begin}, &diagnostic),
  };
  QVERIFY(runtime.enqueueFrame(peer, beginFrame, &diagnostic));
  const qsizetype firstBytes = 1024U * 1024U;
  Frame firstChunk{
      .type = MessageType::FileChunk,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(
          FileControlMessage{FileChunkMessage{.transferId = transferId, .fileId = fileId}},
          &diagnostic
      ),
      .payload = bytes.first(firstBytes),
  };
  QVERIFY(runtime.enqueueFrame(peer, firstChunk, &diagnostic));
  Frame secondChunk{
      .type = MessageType::FileChunk,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(
          FileControlMessage{FileChunkMessage{
              .transferId = transferId,
              .fileId = fileId,
              .offset = static_cast<quint64>(firstBytes),
              .sequence = 1,
          }},
          &diagnostic
      ),
      .payload = bytes.sliced(firstBytes),
  };
  QVERIFY(runtime.enqueueFrame(peer, secondChunk, &diagnostic));
  Frame endFrame{
      .type = MessageType::FileEnd,
      .flags = Final,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(
          FileControlMessage{FileEndMessage{
              .transferId = transferId,
              .fileId = fileId,
              .size = static_cast<quint64>(bytes.size()),
              .sha256 = digest,
          }},
          &diagnostic
      ),
  };
  QVERIFY(runtime.enqueueFrame(peer, endFrame, &diagnostic));

  QTRY_VERIFY_WITH_TIMEOUT(
      latest.has_value() && latest->state == TransferState::Completed, 10'000
  );
  QVERIFY(fileResult.has_value());
  QCOMPARE(fileResult->code, FileResultCode::Ok);
  QVERIFY(checkpoint.has_value());
  QCOMPARE(checkpoint->durableOffset, quint64{1024U * 1024U});
  QVERIFY(safety.commitRequest.has_value());
  QCOMPARE(safety.commitRequest->disposition, CommitDisposition::FailIfExists);
  QFile committed(root.filePath(QStringLiteral("received.bin")));
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), bytes);
}

void IncomingTransferRuntimeTests::receivesPagedEmptyDirectoryManifest()
{
  FakeFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const auto transferId = TransferId::generate();
  const QList<ManifestEntry> entries{
      {
          .id = FileId::generate(),
          .relativeProtocolPath = QStringLiteral("folder"),
          .type = ManifestEntryType::Directory,
          .modifiedUtc = QDateTime::currentDateTimeUtc(),
      },
      {
          .id = FileId::generate(),
          .relativeProtocolPath = QStringLiteral("folder/empty"),
          .type = ManifestEntryType::Directory,
          .modifiedUtc = QDateTime::currentDateTimeUtc(),
      },
  };
  QString diagnostic;
  const auto digest = ManifestPageCodec::canonicalSha256(entries, &diagnostic);
  QVERIFY2(!digest.isEmpty(), qPrintable(diagnostic));
  const TransferOffer incoming{
      .transferId = transferId,
      .displayName = QStringLiteral("folder"),
      .directoryCount = 2,
      .manifestSha256 = digest,
      .manifestPageCount = 2,
      .requestedConflictPolicy = ConflictPolicy::AutoRename,
      .createdAtMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
  };
  const auto peer = DeviceId::generate();
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    latest = snapshot;
  });
  auto capabilities = receiverCapabilities();
  capabilities.features.append(QStringLiteral("folder.v1"));
  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, capabilities, incoming));
  runtime.accept(transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);
  for (qsizetype index = 0; index < entries.size(); ++index) {
    Frame page{
        .type = MessageType::ManifestPage,
        .metadata = ManifestPageCodec::encode(
            {
                .transferId = transferId,
                .pageIndex = static_cast<quint64>(index),
                .pageCount = 2,
                .entries = {entries.at(index)},
            },
            {}, &diagnostic
        ),
    };
    QVERIFY2(!page.metadata.isEmpty(), qPrintable(diagnostic));
    QVERIFY2(runtime.enqueueFrame(peer, page, &diagnostic), qPrintable(diagnostic));
  }
  Frame complete{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = transferId, .canonicalSha256 = digest}, &diagnostic
      ),
  };
  QVERIFY(runtime.enqueueFrame(peer, complete, &diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(
      latest.has_value() && latest->state == TransferState::Completed, 5'000
  );
  QCOMPARE(latest->progress.completedFiles, quint64{0});
  QCOMPARE(latest->progress.totalFiles, quint64{0});
  QCOMPARE(latest->currentRelativeDisplayPath, QStringLiteral("folder"));
  QVERIFY(QDir(root.path()).exists(QStringLiteral("folder/empty")));
}

void IncomingTransferRuntimeTests::disconnectInterruptsAcceptedPipeline()
{
  FakeFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  const auto incoming = offer();
  const auto peer = DeviceId::generate();
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  QSignalSpy changes(&runtime, &IncomingTransferRuntime::transferChanged);
  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    latest = snapshot;
  });
  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), incoming));
  runtime.accept(incoming.transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);

  runtime.peerDisconnected(peer);

  QVERIFY(latest.has_value());
  QCOMPARE(latest->state, TransferState::Interrupted);
  QCOMPARE(changes.count(), 1);
  runtime.peerDisconnected(peer);
  QCOMPARE(changes.count(), 1);
  QString diagnostic;
  QVERIFY(!runtime.enqueueFrame(peer, {.type = MessageType::ManifestPage}, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  runtime.accept(incoming.transferId, {.destinationRoot = root.path()});
  QCOMPARE(operations.count(), 2);
  QCOMPARE(operation(operations, 1).outcome, TransferOperationOutcome::Idempotent);
}

void IncomingTransferRuntimeTests::cancelledTransferOnlyIgnoresValidatedTailFrames()
{
  FakeFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  const auto incoming = offer();
  const auto peer = DeviceId::generate();
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    latest = snapshot;
  });
  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), incoming));
  runtime.accept(incoming.transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);

  QString diagnostic;
  const Frame cancel{
      .type = MessageType::TransferCancel,
      .metadata = TransferCommandCodec::encode(
          TransferCommandMessage{TransferCancelMessage{.transferId = incoming.transferId, .keepPartial = true}},
          &diagnostic
      ),
  };
  QVERIFY2(runtime.receiveCommand(peer, cancel, &diagnostic), qPrintable(diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(latest.has_value() && latest->state == TransferState::Cancelled, 5'000);

  const QByteArray digest(kSha256Bytes, '\x35');
  const Frame validTail{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = incoming.transferId, .canonicalSha256 = digest}, &diagnostic
      ),
  };
  QVERIFY2(runtime.enqueueFrame(peer, validTail, &diagnostic), qPrintable(diagnostic));

  const Frame differentTransfer{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = TransferId::generate(), .canonicalSha256 = digest}, &diagnostic
      ),
  };
  QVERIFY(!runtime.enqueueFrame(peer, differentTransfer, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());

  const Frame malformedTail{
      .type = MessageType::FileChunk,
      .streamId = 1,
      .metadata = QByteArrayLiteral("not-cbor"),
      .payload = QByteArrayLiteral("payload"),
  };
  QVERIFY(!runtime.enqueueFrame(peer, malformedTail, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());
}

void IncomingTransferRuntimeTests::activeAndCancelledSessionsRouteDataByTransferId()
{
  FakeFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QThreadPool pool;
  IncomingTransferRuntime runtime(safety, pool);
  const auto peer = DeviceId::generate();
  const auto cancelledOffer = offer();
  const auto activeOffer = offer();
  QSignalSpy operations(&runtime, &IncomingTransferRuntime::transferOperationFinished);
  QSignalSpy failures(&runtime, &IncomingTransferRuntime::pipelineFailed);
  QHash<TransferId, TransferSnapshot> snapshots;
  connect(&runtime, &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    snapshots.insert(snapshot.id, snapshot);
  });

  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), cancelledOffer));
  runtime.accept(cancelledOffer.transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);
  QString diagnostic;
  const Frame cancel{
      .type = MessageType::TransferCancel,
      .metadata = TransferCommandCodec::encode(
          TransferCommandMessage{TransferCancelMessage{.transferId = cancelledOffer.transferId}}, &diagnostic
      ),
  };
  QVERIFY2(runtime.receiveCommand(peer, cancel, &diagnostic), qPrintable(diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(
      snapshots.constFind(cancelledOffer.transferId) != snapshots.constEnd() &&
          snapshots.constFind(cancelledOffer.transferId)->state == TransferState::Cancelled,
      5'000
  );

  QVERIFY(runtime.receiveOffer(peer, QStringLiteral("Peer"), true, receiverCapabilities(), activeOffer));
  runtime.accept(activeOffer.transferId, {.destinationRoot = root.path()});
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 2, 5'000);

  const Frame cancelledComplete{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = cancelledOffer.transferId, .canonicalSha256 = cancelledOffer.manifestSha256}, &diagnostic
      ),
  };
  const Frame cancelledChunk{
      .type = MessageType::FileChunk,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(
          FileControlMessage{FileChunkMessage{
              .transferId = cancelledOffer.transferId,
              .fileId = FileId::generate(),
          }},
          &diagnostic
      ),
      .payload = QByteArrayLiteral("queued-tail"),
  };
  const Frame activePage{
      .type = MessageType::ManifestPage,
      .metadata = ManifestPageCodec::encode(
          {.transferId = activeOffer.transferId, .pageIndex = 0, .pageCount = 1}, {}, &diagnostic
      ),
  };
  QVERIFY2(runtime.enqueueFrame(peer, cancelledComplete, &diagnostic), qPrintable(diagnostic));
  QVERIFY2(runtime.enqueueFrame(peer, cancelledChunk, &diagnostic), qPrintable(diagnostic));
  QVERIFY2(runtime.enqueueFrame(peer, activePage, &diagnostic), qPrintable(diagnostic));
  QTest::qWait(200);
  QCOMPARE(failures.count(), 0);
}

void IncomingTransferRuntimeTests::pendingConflictLifecycle_data()
{
  QTest::addColumn<QString>("action");
  QTest::newRow("stop") << QStringLiteral("stop");
  QTest::newRow("disconnect") << QStringLiteral("disconnect");
  QTest::newRow("local-cancel") << QStringLiteral("local-cancel");
  QTest::newRow("remote-cancel") << QStringLiteral("remote-cancel");
}

void IncomingTransferRuntimeTests::pendingConflictLifecycle()
{
  QFETCH(QString, action);

  FakeFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString relativePath = QStringLiteral("nested/conflict.bin");
  QVERIFY(QDir(root.path()).mkpath(QStringLiteral("nested")));
  const QByteArray originalBytes = QByteArrayLiteral("keep-original");
  QFile original(root.filePath(relativePath));
  QVERIFY(original.open(QIODevice::WriteOnly));
  QCOMPARE(original.write(originalBytes), qint64(originalBytes.size()));
  original.close();

  const QByteArray bytes(64 * 1024 + 17, '\x67');
  const QByteArray fileDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  const auto transferId = TransferId::generate();
  const auto fileId = FileId::generate();
  const ManifestEntry entry{
      .id = fileId,
      .relativeProtocolPath = relativePath,
      .type = ManifestEntryType::File,
      .size = static_cast<quint64>(bytes.size()),
      .modifiedUtc = QDateTime::currentDateTimeUtc(),
      .sha256 = fileDigest,
  };
  QString diagnostic;
  const QByteArray manifestDigest = ManifestPageCodec::canonicalSha256({entry}, &diagnostic);
  QVERIFY2(!manifestDigest.isEmpty(), qPrintable(diagnostic));
  const TransferOffer incoming{
      .transferId = transferId,
      .displayName = QStringLiteral("conflict.bin"),
      .totalBytes = static_cast<quint64>(bytes.size()),
      .fileCount = 1,
      .manifestSha256 = manifestDigest,
      .manifestPageCount = 1,
      .requestedConflictPolicy = ConflictPolicy::Ask,
      .createdAtMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
  };
  const auto peer = DeviceId::generate();
  auto capabilities = receiverCapabilities();
  capabilities.conflictPolicies.append(ConflictPolicy::Ask);
  QThreadPool pool;
  pool.setMaxThreadCount(2);
  auto runtime = std::make_unique<IncomingTransferRuntime>(safety, pool);
  QSignalSpy operations(runtime.get(), &IncomingTransferRuntime::transferOperationFinished);
  QSignalSpy prompts(runtime.get(), &IncomingTransferRuntime::incomingConflictDecisionRequired);
  QSignalSpy changes(runtime.get(), &IncomingTransferRuntime::transferChanged);
  QVERIFY(operations.isValid());
  QVERIFY(prompts.isValid());
  QVERIFY(changes.isValid());
  std::optional<TransferSnapshot> latest;
  connect(runtime.get(), &IncomingTransferRuntime::transferAdded, this, [&](const auto &snapshot) {
    latest = snapshot;
  });
  connect(runtime.get(), &IncomingTransferRuntime::transferChanged, this, [&](const auto &snapshot) {
    latest = snapshot;
  });

  QVERIFY(runtime->receiveOffer(peer, QStringLiteral("Peer"), true, capabilities, incoming));
  runtime->accept(
      transferId, {.destinationRoot = root.path(), .conflictPolicy = ConflictPolicy::Ask}
  );
  QTRY_COMPARE_WITH_TIMEOUT(operations.count(), 1, 5'000);
  const Frame manifestPage{
      .type = MessageType::ManifestPage,
      .metadata = ManifestPageCodec::encode(
          {.transferId = transferId, .pageIndex = 0, .pageCount = 1, .entries = {entry}},
          {}, &diagnostic
      ),
  };
  QVERIFY2(!manifestPage.metadata.isEmpty(), qPrintable(diagnostic));
  QVERIFY2(runtime->enqueueFrame(peer, manifestPage, &diagnostic), qPrintable(diagnostic));
  const Frame manifestComplete{
      .type = MessageType::ManifestComplete,
      .metadata = ManifestPageCodec::encodeComplete(
          {.transferId = transferId, .canonicalSha256 = manifestDigest}, &diagnostic
      ),
  };
  QVERIFY2(runtime->enqueueFrame(peer, manifestComplete, &diagnostic), qPrintable(diagnostic));
  const FileBeginMessage begin{
      .transferId = transferId,
      .fileId = fileId,
      .size = static_cast<quint64>(bytes.size()),
      .chunkBytes = static_cast<quint32>(bytes.size()),
      .expectedSha256 = fileDigest,
  };
  const Frame beginFrame{
      .type = MessageType::FileBegin,
      .streamId = 1,
      .metadata = FileMessageCodec::encode(FileControlMessage{begin}, &diagnostic),
  };
  QVERIFY2(!beginFrame.metadata.isEmpty(), qPrintable(diagnostic));
  QVERIFY2(runtime->enqueueFrame(peer, beginFrame, &diagnostic), qPrintable(diagnostic));
  QTRY_COMPARE_WITH_TIMEOUT(prompts.count(), 1, 5'000);
  const auto *promptArgument = static_cast<const IncomingConflictPrompt *>(
      prompts.constFirst().constFirst().constData()
  );
  QVERIFY(promptArgument != nullptr);
  const IncomingConflictPrompt prompt = *promptArgument;
  QCOMPARE(prompt.transferId, transferId);
  QCOMPARE(prompt.relativeProtocolPath, relativePath);
  QVERIFY(!QDir::isAbsolutePath(prompt.relativeProtocolPath));
  QVERIFY(!prompt.relativeProtocolPath.contains(root.path(), Qt::CaseInsensitive));
  QVERIFY(runtime->hasPendingIncomingConflict(transferId, prompt.conflictId));
  QVERIFY(latest.has_value());
  QCOMPARE(latest->state, TransferState::Queued);

  if (action == QStringLiteral("stop")) {
    QElapsedTimer elapsed;
    elapsed.start();
    runtime.reset();
    QVERIFY2(elapsed.elapsed() < 5'000, "destroying a pending conflict pipeline timed out");
    QVERIFY(!QFileInfo::exists(
        QDir(root.path()).filePath(
            QStringLiteral(".incoming/%1/%2.part").arg(transferId.toString(), fileId.toString())
        )
    ));
    QFile preserved(root.filePath(relativePath));
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), originalBytes);
    return;
  }

  QElapsedTimer elapsed;
  elapsed.start();
  if (action == QStringLiteral("disconnect")) {
    runtime->peerDisconnected(peer);
  } else {
    const TransferCommandMessage cancel = TransferCancelMessage{
        .transferId = transferId,
        .reason = TransferCancelReason::UserRequested,
        .keepPartial = false,
    };
    if (action == QStringLiteral("local-cancel")) {
      DeviceId commandPeer = DeviceId::generate();
      TransferOperationOutcome outcome = TransferOperationOutcome::Rejected;
      QVERIFY2(runtime->validateLocalCommand(cancel, &commandPeer, &diagnostic), qPrintable(diagnostic));
      QCOMPARE(commandPeer, peer);
      QVERIFY2(
          runtime->applyLocalCommand(cancel, nullptr, &diagnostic, &outcome),
          qPrintable(diagnostic)
      );
      QCOMPARE(outcome, TransferOperationOutcome::Applied);
    } else {
      const Frame cancelFrame{
          .type = MessageType::TransferCancel,
          .metadata = TransferCommandCodec::encode(cancel, &diagnostic),
      };
      QVERIFY2(!cancelFrame.metadata.isEmpty(), qPrintable(diagnostic));
      QVERIFY2(runtime->receiveCommand(peer, cancelFrame, &diagnostic), qPrintable(diagnostic));
    }
  }
  QVERIFY2(elapsed.elapsed() < 5'000, "terminating a pending conflict pipeline timed out");
  if (action == QStringLiteral("disconnect")) {
    QVERIFY(latest.has_value());
    QCOMPARE(latest->state, TransferState::Interrupted);
  } else {
    QTRY_VERIFY_WITH_TIMEOUT(
        latest.has_value() && latest->state == TransferState::Cancelled, 5'000
    );
  }
  QVERIFY(!runtime->hasPendingIncomingConflict(transferId, prompt.conflictId));
  const auto changeCount = changes.count();
  runtime->resolveIncomingConflict(
      transferId, prompt.conflictId, IncomingConflictDecision::AutoRename
  );
  QTest::qWait(100);
  QCOMPARE(changes.count(), changeCount);
  QVERIFY(!QFileInfo::exists(
      QDir(root.path()).filePath(
          QStringLiteral(".incoming/%1/%2.part").arg(transferId.toString(), fileId.toString())
      )
  ));
  QFile preserved(root.filePath(relativePath));
  QVERIFY(preserved.open(QIODevice::ReadOnly));
  QCOMPARE(preserved.readAll(), originalBytes);
}

QTEST_MAIN(IncomingTransferRuntimeTests)

#include "IncomingTransferRuntimeTests.moc"
