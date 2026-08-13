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
    if (!commitResult.ok()) {
      return commitResult;
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
  QCOMPARE(safety.traversalRequests.size(), 2);
  QCOMPARE(safety.traversalRequests.first().candidatePath, root.filePath(QStringLiteral("nested/payload.bin")));
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

QTEST_MAIN(IncomingFileReceiverWorkerTests)

#include "IncomingFileReceiverWorkerTests.moc"
