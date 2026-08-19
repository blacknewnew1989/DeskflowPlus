/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingFileReceiverWorker.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFuture>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtConcurrentRun>

using namespace deskflow::relaydesk;
using namespace relaydesk::transfer;

namespace {

class FakeFileSafety final : public IPlatformFileSafety
{
public:
  FileSafetyResult verifyReceiveRoot(const VerifyReceiveRootRequest &request) const override
  {
    const QMutexLocker lock(&mutex);
    rootRequests.append(request);
    workerThreads.append(QThread::currentThread());
    return rootResult;
  }

  FileSafetyResult verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &request) const override
  {
    const QMutexLocker lock(&mutex);
    traversalRequests.append(request);
    workerThreads.append(QThread::currentThread());
    return traversalResult;
  }

  FileSafetyResult commitStagedFile(const CommitStagedFileRequest &request) override
  {
    const QMutexLocker lock(&mutex);
    commitRequests.append(request);
    workerThreads.append(QThread::currentThread());
    if (createDestinationRace && commitRequests.size() == 1) {
      QFile raced(request.destinationPath);
      if (!raced.open(QIODevice::WriteOnly) || raced.write(raceBytes) != raceBytes.size()) {
        return {
            .error = FileSafetyError::CommitFailed,
            .diagnostic = QStringLiteral("fake adapter could not create destination race"),
        };
      }
      return {
          .error = FileSafetyError::DestinationExists,
          .diagnostic = QStringLiteral("destination appeared before atomic commit"),
      };
    }
    if (!commitResult.ok()) {
      return commitResult;
    }
    if (request.disposition == CommitDisposition::ReplaceExisting &&
        QFileInfo::exists(request.destinationPath) && !QFile::remove(request.destinationPath)) {
      return {
          .error = FileSafetyError::CommitFailed,
          .diagnostic = QStringLiteral("fake adapter could not replace destination"),
      };
    }
    if (!QFile::rename(request.stagingPath, request.destinationPath)) {
      return {
          .error = FileSafetyError::CommitFailed,
          .diagnostic = QStringLiteral("fake adapter could not move staging file"),
      };
    }
    return {};
  }

  mutable QMutex mutex;
  mutable QList<VerifyReceiveRootRequest> rootRequests;
  mutable QList<VerifyNoLinkTraversalRequest> traversalRequests;
  mutable QList<CommitStagedFileRequest> commitRequests;
  mutable QList<QThread *> workerThreads;
  FileSafetyResult rootResult;
  FileSafetyResult traversalResult;
  FileSafetyResult commitResult;
  bool createDestinationRace = false;
  QByteArray raceBytes = QByteArrayLiteral("external-race");
};

struct WorkerResult
{
  FileReceiverResult begin;
  FileReceiverResult firstAppend;
  FileReceiverResult secondAppend;
  FileReceiverResult finish;
  FileReceiverSnapshot snapshot;
  QThread *workerThread = nullptr;
};

FileReceiveRequest requestFor(const QString &root, QByteArray bytes)
{
  const auto transferId = TransferId::generate();
  const auto fileId = FileId::generate();
  const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  return {
      .receiveRoot = root,
      .entry = {
          .id = fileId,
          .relativeProtocolPath = QStringLiteral("nested/payload.bin"),
          .type = ManifestEntryType::File,
          .size = static_cast<quint64>(bytes.size()),
          .modifiedUtc = QDateTime::currentDateTimeUtc(),
          .sha256 = digest,
      },
      .begin = {
          .transferId = transferId,
          .fileId = fileId,
          .size = static_cast<quint64>(bytes.size()),
          .chunkBytes = 1024U * 1024U,
          .expectedSha256 = digest,
      },
      .manifestSha256 = QByteArray(kSha256Bytes, '\x42'),
  };
}

} // namespace

class IncomingFileReceiverWorkerTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void ownsCompleteFileReceiverLifecycleOnDiskWorker();
  void rejectsPlatformSafetyFailureBeforeCreatingPart();
  void rejectsCrossThreadReuse();
  void retainsPartWhenPlatformCommitFails();
  void checkpointsAndResumesPartOnDiskWorker();
  void retriesAutoRenameWhenDestinationAppearsBeforeCommit();
  void overwritesFileButRejectsDirectoryAndPreservesOriginal();
  void skipsExistingFileWithoutCreatingPart();
  void resolvesAskConflictOnTheSameDiskWorker();
};

void IncomingFileReceiverWorkerTests::ownsCompleteFileReceiverLifecycleOnDiskWorker()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray bytes(1024U * 1024U + 17U, '\x5a');
  const auto request = requestFor(root.path(), bytes);
  FakeFileSafety safety;
  QThreadPool pool;
  pool.setMaxThreadCount(1);

  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    WorkerResult result;
    result.workerThread = QThread::currentThread();
    result.begin = worker.begin(request);
    const FileChunkMessage firstChunk{
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
    };
    const qsizetype firstBytes = static_cast<qsizetype>(request.begin.chunkBytes);
    result.firstAppend = worker.append(firstChunk, QByteArrayView(bytes).first(firstBytes));
    result.secondAppend = worker.append(
        {
            .transferId = request.begin.transferId,
            .fileId = request.begin.fileId,
            .offset = static_cast<quint64>(firstBytes),
            .sequence = 1,
        },
        QByteArrayView(bytes).sliced(firstBytes)
    );
    result.finish = worker.finish({
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = request.begin.size,
        .sha256 = request.begin.expectedSha256,
    });
    result.snapshot = worker.snapshot();
    return result;
  });
  future.waitForFinished();
  const auto result = future.result();

  QVERIFY2(result.begin.ok(), qPrintable(result.begin.diagnostic));
  QVERIFY2(result.firstAppend.ok(), qPrintable(result.firstAppend.diagnostic));
  QVERIFY2(result.secondAppend.ok(), qPrintable(result.secondAppend.diagnostic));
  QVERIFY2(result.finish.ok(), qPrintable(result.finish.diagnostic));
  QCOMPARE(result.snapshot.state, FileReceiverState::Completed);
  QVERIFY(!result.snapshot.committedPath.isEmpty());
  QFile committed(result.snapshot.committedPath);
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), bytes);
  QCOMPARE(safety.rootRequests.size(), 1);
  QCOMPARE(safety.traversalRequests.size(), 8);
  QCOMPARE(safety.traversalRequests.first().candidatePath, root.filePath(QStringLiteral("nested")));
  QCOMPARE(
      safety.traversalRequests.at(2).candidatePath,
      root.filePath(QStringLiteral("nested/payload.bin"))
  );
  QVERIFY(
      safety.traversalRequests.at(3).candidatePath.endsWith(QStringLiteral(".incoming"))
  );
  QVERIFY(
      safety.traversalRequests.at(5).candidatePath.endsWith(request.begin.transferId.toString())
  );
  QVERIFY(safety.traversalRequests.last().candidatePath.endsWith(QStringLiteral(".part")));
  QCOMPARE(safety.commitRequests.size(), 1);
  QCOMPARE(safety.commitRequests.first().receiveRoot, root.path());
  QCOMPARE(safety.commitRequests.first().destinationPath, result.snapshot.committedPath);
  QCOMPARE(safety.commitRequests.first().disposition, CommitDisposition::FailIfExists);
  QVERIFY(IncomingFileReceiverWorker::platformCommitWired());
  for (auto *thread : std::as_const(safety.workerThreads)) {
    QCOMPARE(thread, result.workerThread);
    QVERIFY(thread != QThread::currentThread());
  }
}

void IncomingFileReceiverWorkerTests::rejectsPlatformSafetyFailureBeforeCreatingPart()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const auto request = requestFor(root.path(), QByteArray(1, '\x01'));
  FakeFileSafety safety;
  safety.traversalResult = {
      .error = FileSafetyError::LinkTraversalDetected,
      .diagnostic = QStringLiteral("candidate traverses a symbolic link"),
  };
  QThreadPool pool;
  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    return worker.begin(request);
  });
  future.waitForFinished();
  const auto result = future.result();

  QCOMPARE(result.error, FileReceiverError::UnsafePath);
  QVERIFY(!result.ok());
  QCOMPARE(safety.rootRequests.size(), 1);
  QCOMPARE(safety.traversalRequests.size(), 1);
  QCOMPARE(safety.commitRequests.size(), 0);
  const QString partPath = root.filePath(
      QStringLiteral(".incoming/%1/%2.part")
          .arg(request.begin.transferId.toString(), request.begin.fileId.toString())
  );
  QVERIFY(!QFile::exists(partPath));
}

void IncomingFileReceiverWorkerTests::rejectsCrossThreadReuse()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const auto request = requestFor(root.path(), QByteArray(1, '\x01'));
  FakeFileSafety safety;
  IncomingFileReceiverWorker worker(safety);
  QThreadPool pool;

  auto future = QtConcurrent::run(&pool, [&]() { return worker.begin(request); });
  future.waitForFinished();
  const auto result = future.result();

  QCOMPARE(result.error, FileReceiverError::InvalidState);
  QVERIFY(result.diagnostic.contains(QStringLiteral("disk worker")));
  QCOMPARE(safety.rootRequests.size(), 0);
  QCOMPARE(safety.traversalRequests.size(), 0);
}

void IncomingFileReceiverWorkerTests::retainsPartWhenPlatformCommitFails()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray bytes(17, '\x33');
  const auto request = requestFor(root.path(), bytes);
  FakeFileSafety safety;
  safety.commitResult = {
      .error = FileSafetyError::DestinationExists,
      .diagnostic = QStringLiteral("destination appeared before atomic commit"),
  };
  QThreadPool pool;
  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    WorkerResult result;
    result.begin = worker.begin(request);
    result.firstAppend = worker.append(
        {.transferId = request.begin.transferId, .fileId = request.begin.fileId},
        QByteArrayView(bytes)
    );
    result.finish = worker.finish({
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = request.begin.size,
        .sha256 = request.begin.expectedSha256,
    });
    result.snapshot = worker.snapshot();
    return result;
  });
  future.waitForFinished();
  const auto result = future.result();

  QVERIFY(result.begin.ok());
  QVERIFY(result.firstAppend.ok());
  QCOMPARE(result.finish.error, FileReceiverError::TargetExists);
  QVERIFY(result.finish.fileResult.has_value());
  QCOMPARE(result.finish.fileResult->code, FileResultCode::TargetExists);
  QCOMPARE(result.snapshot.state, FileReceiverState::Failed);
  QVERIFY(QFile::exists(result.snapshot.partPath));
  QCOMPARE(safety.commitRequests.size(), 1);
}

void IncomingFileReceiverWorkerTests::checkpointsAndResumesPartOnDiskWorker()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray bytes(1024U * 1024U + 17U, '\x4d');
  auto request = requestFor(root.path(), bytes);
  FakeFileSafety safety;
  ResumeStore store(root.filePath(QStringLiteral("resume/active")));
  QThreadPool pool;
  pool.setMaxThreadCount(1);

  ResumeState state{
      .transferId = request.begin.transferId,
      .peerDeviceId = deskflow::relaydesk::DeviceId::generate(),
      .manifestSha256 = request.manifestSha256,
      .direction = ResumeDirection::Receiving,
      .files =
          {
              {
                  .fileId = request.begin.fileId,
                  .relativeProtocolPath = request.entry.relativeProtocolPath,
                  .durableOffset = 0,
                  .totalBytes = request.entry.size,
                  .partRelativePath =
                      QStringLiteral(".incoming/%1/%2.part")
                          .arg(request.begin.transferId.toString(), request.begin.fileId.toString()),
              },
          },
      .updatedUtc = QDateTime::currentDateTimeUtc(),
  };
  const qsizetype firstBytes = static_cast<qsizetype>(request.begin.chunkBytes);

  auto checkpointFuture = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    const auto begun = worker.begin(request);
    if (!begun.ok()) {
      return DurableCheckpointResult{
          .error = DurableCheckpointError::InvalidReceiverState,
          .diagnostic = begun.diagnostic,
      };
    }
    const auto appended = worker.append(
        {.transferId = request.begin.transferId, .fileId = request.begin.fileId},
        QByteArrayView(bytes).first(firstBytes)
    );
    if (!appended.ok()) {
      return DurableCheckpointResult{
          .error = DurableCheckpointError::InvalidReceiverState,
          .diagnostic = appended.diagnostic,
      };
    }
    return worker.checkpoint(store, state);
  });
  checkpointFuture.waitForFinished();
  const auto checkpoint = checkpointFuture.result();
  QVERIFY2(checkpoint.ok(), qPrintable(checkpoint.diagnostic));
  QCOMPARE(checkpoint.message->durableOffset, static_cast<quint64>(firstBytes));
  QCOMPARE(state.files.constFirst().durableOffset, static_cast<quint64>(firstBytes));
  const auto persisted = store.load(request.begin.transferId);
  QVERIFY2(persisted.ok(), qPrintable(persisted.diagnostic));
  QCOMPARE(*persisted.state, state);

  request.begin.startOffset = static_cast<quint64>(firstBytes);
  auto resumeFuture = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    WorkerResult result;
    result.begin = worker.resume(request, state);
    result.secondAppend = worker.append(
        {
            .transferId = request.begin.transferId,
            .fileId = request.begin.fileId,
            .offset = static_cast<quint64>(firstBytes),
        },
        QByteArrayView(bytes).sliced(firstBytes)
    );
    result.finish = worker.finish({
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = request.begin.size,
        .sha256 = request.begin.expectedSha256,
    });
    result.snapshot = worker.snapshot();
    return result;
  });
  resumeFuture.waitForFinished();
  const auto resumed = resumeFuture.result();
  QVERIFY2(resumed.begin.ok(), qPrintable(resumed.begin.diagnostic));
  QVERIFY2(resumed.secondAppend.ok(), qPrintable(resumed.secondAppend.diagnostic));
  QVERIFY2(resumed.finish.ok(), qPrintable(resumed.finish.diagnostic));
  QCOMPARE(resumed.snapshot.state, FileReceiverState::Completed);
  QFile committed(resumed.snapshot.committedPath);
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), bytes);
  QCOMPARE(safety.commitRequests.size(), 1);
}

void IncomingFileReceiverWorkerTests::retriesAutoRenameWhenDestinationAppearsBeforeCommit()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray originalBytes = QByteArrayLiteral("keep-original");
  const QByteArray receivedBytes = QByteArrayLiteral("received-payload");
  auto request = requestFor(root.path(), receivedBytes);
  request.entry.relativeProtocolPath = QStringLiteral("payload.bin");
  QFile original(root.filePath(QStringLiteral("payload.bin")));
  QVERIFY(original.open(QIODevice::WriteOnly));
  QCOMPARE(original.write(originalBytes), qint64(originalBytes.size()));
  original.close();
  FakeFileSafety safety;
  safety.createDestinationRace = true;
  QThreadPool pool;
  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    WorkerResult result;
    result.begin = worker.begin(request);
    result.firstAppend = worker.append(
        {.transferId = request.begin.transferId, .fileId = request.begin.fileId},
        QByteArrayView(receivedBytes)
    );
    result.finish = worker.finish({
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = request.begin.size,
        .sha256 = request.begin.expectedSha256,
    });
    result.snapshot = worker.snapshot();
    return result;
  });
  future.waitForFinished();
  const auto result = future.result();

  QVERIFY2(result.begin.ok(), qPrintable(result.begin.diagnostic));
  QVERIFY2(result.finish.ok(), qPrintable(result.finish.diagnostic));
  QCOMPARE(safety.commitRequests.size(), 2);
  QVERIFY(safety.commitRequests.at(0).destinationPath.endsWith(QStringLiteral("payload (1).bin")));
  QVERIFY(safety.commitRequests.at(1).destinationPath.endsWith(QStringLiteral("payload (2).bin")));
  QFile preserved(root.filePath(QStringLiteral("payload.bin")));
  QFile raced(root.filePath(QStringLiteral("payload (1).bin")));
  QFile committed(root.filePath(QStringLiteral("payload (2).bin")));
  QVERIFY(preserved.open(QIODevice::ReadOnly));
  QVERIFY(raced.open(QIODevice::ReadOnly));
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(preserved.readAll(), originalBytes);
  QCOMPARE(raced.readAll(), safety.raceBytes);
  QCOMPARE(committed.readAll(), receivedBytes);
}

void IncomingFileReceiverWorkerTests::overwritesFileButRejectsDirectoryAndPreservesOriginal()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray oldBytes = QByteArrayLiteral("old");
  const QByteArray newBytes = QByteArrayLiteral("new-payload");
  auto request = requestFor(root.path(), newBytes);
  request.entry.relativeProtocolPath = QStringLiteral("replace.bin");
  request.conflictPolicy = ConflictPolicy::Overwrite;
  QFile existing(root.filePath(QStringLiteral("replace.bin")));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  QCOMPARE(existing.write(oldBytes), qint64(oldBytes.size()));
  existing.close();
  FakeFileSafety safety;
  QThreadPool pool;
  auto overwriteFuture = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    WorkerResult result;
    result.begin = worker.begin(request);
    result.firstAppend = worker.append(
        {.transferId = request.begin.transferId, .fileId = request.begin.fileId},
        QByteArrayView(newBytes)
    );
    result.finish = worker.finish({
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = request.begin.size,
        .sha256 = request.begin.expectedSha256,
    });
    return result;
  });
  overwriteFuture.waitForFinished();
  const auto overwritten = overwriteFuture.result();
  QVERIFY2(overwritten.begin.ok(), qPrintable(overwritten.begin.diagnostic));
  QVERIFY2(overwritten.finish.ok(), qPrintable(overwritten.finish.diagnostic));
  QCOMPARE(safety.commitRequests.size(), 1);
  QCOMPARE(safety.commitRequests.first().disposition, CommitDisposition::ReplaceExisting);
  QFile replaced(root.filePath(QStringLiteral("replace.bin")));
  QVERIFY(replaced.open(QIODevice::ReadOnly));
  QCOMPARE(replaced.readAll(), newBytes);

  auto directoryRequest = requestFor(root.path(), newBytes);
  directoryRequest.entry.relativeProtocolPath = QStringLiteral("existing-directory");
  directoryRequest.conflictPolicy = ConflictPolicy::Overwrite;
  QVERIFY(QDir().mkdir(root.filePath(QStringLiteral("existing-directory"))));
  auto directoryFuture = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    return worker.begin(directoryRequest);
  });
  directoryFuture.waitForFinished();
  const auto directoryResult = directoryFuture.result();
  QCOMPARE(directoryResult.error, FileReceiverError::UnsupportedConflictPolicy);
  QVERIFY(QDir(root.filePath(QStringLiteral("existing-directory"))).exists());
  const QString partPath = root.filePath(
      QStringLiteral(".incoming/%1/%2.part")
          .arg(directoryRequest.begin.transferId.toString(), directoryRequest.begin.fileId.toString())
  );
  QVERIFY(!QFileInfo::exists(partPath));
}

void IncomingFileReceiverWorkerTests::skipsExistingFileWithoutCreatingPart()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray oldBytes = QByteArrayLiteral("preserve-me");
  auto request = requestFor(root.path(), QByteArrayLiteral("incoming"));
  request.entry.relativeProtocolPath = QStringLiteral("skip.bin");
  request.conflictPolicy = ConflictPolicy::Skip;
  QFile existing(root.filePath(QStringLiteral("skip.bin")));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  QCOMPARE(existing.write(oldBytes), qint64(oldBytes.size()));
  existing.close();
  FakeFileSafety safety;
  QThreadPool pool;
  IncomingFileDisposition disposition = IncomingFileDisposition::Receive;
  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    const auto result = worker.begin(request);
    disposition = worker.disposition();
    return result;
  });
  future.waitForFinished();
  const auto result = future.result();
  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(disposition, IncomingFileDisposition::Skip);
  QCOMPARE(safety.commitRequests.size(), 0);
  const QString partPath = root.filePath(
      QStringLiteral(".incoming/%1/%2.part")
          .arg(request.begin.transferId.toString(), request.begin.fileId.toString())
  );
  QVERIFY(!QFileInfo::exists(partPath));
  QFile preserved(root.filePath(QStringLiteral("skip.bin")));
  QVERIFY(preserved.open(QIODevice::ReadOnly));
  QCOMPARE(preserved.readAll(), oldBytes);
}

void IncomingFileReceiverWorkerTests::resolvesAskConflictOnTheSameDiskWorker()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QByteArray bytes = QByteArrayLiteral("replacement");
  auto request = requestFor(root.path(), bytes);
  request.entry.relativeProtocolPath = QStringLiteral("ask.bin");
  request.conflictPolicy = ConflictPolicy::Ask;
  QFile existing(root.filePath(QStringLiteral("ask.bin")));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  existing.write("old");
  existing.close();
  FakeFileSafety safety;
  QThreadPool pool;
  auto future = QtConcurrent::run(&pool, [&]() {
    IncomingFileReceiverWorker worker(safety);
    const auto pending = worker.begin(request);
    const auto prompt = worker.pendingConflict();
    if (!prompt.has_value()) {
      return std::pair{pending, FileReceiverResult{}};
    }
    const auto resolved = worker.resolveConflict(IncomingConflictDecision::AutoRename);
    if (!resolved.ok()) {
      return std::pair{pending, resolved};
    }
    FileChunkMessage chunk{
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .offset = 0,
        .sequence = 0,
    };
    const auto appended = worker.append(chunk, bytes);
    FileEndMessage end{
        .transferId = request.begin.transferId,
        .fileId = request.begin.fileId,
        .size = static_cast<quint64>(bytes.size()),
        .sha256 = request.entry.sha256,
    };
    return std::pair{pending, appended.ok() ? worker.finish(end) : appended};
  });
  future.waitForFinished();
  QVERIFY(!future.result().first.ok());
  QVERIFY2(future.result().second.ok(), qPrintable(future.result().second.diagnostic));
  QFile renamed(root.filePath(QStringLiteral("ask (1).bin")));
  QVERIFY(renamed.open(QIODevice::ReadOnly));
  QCOMPARE(renamed.readAll(), bytes);
}

QTEST_MAIN(IncomingFileReceiverWorkerTests)

#include "IncomingFileReceiverWorkerTests.moc"
