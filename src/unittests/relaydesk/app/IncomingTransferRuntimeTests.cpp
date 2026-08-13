/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"

#include <QCryptographicHash>
#include <QFile>
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
  void receivesAcceptedFileThroughAtomicCommit();
  void receivesPagedEmptyDirectoryManifest();
  void disconnectInterruptsAcceptedPipeline();
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
  connect(&runtime, &IncomingTransferRuntime::responseReady, this, [&](auto responsePeer, const Frame &frame) {
    QCOMPARE(responsePeer, peer);
    QCOMPARE(frame.streamId, quint32{1});
    const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
    QVERIFY(decoded.ok());
    if (decoded.ok()) {
      fileResult = std::get<FileResultMessage>(*decoded.message);
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
  QVERIFY(diagnostic.contains(QStringLiteral("no accepted receive session")));
  runtime.accept(incoming.transferId, {.destinationRoot = root.path()});
  QCOMPARE(operations.count(), 2);
  QCOMPARE(operation(operations, 1).outcome, TransferOperationOutcome::Idempotent);
}

QTEST_MAIN(IncomingTransferRuntimeTests)

#include "IncomingTransferRuntimeTests.moc"
