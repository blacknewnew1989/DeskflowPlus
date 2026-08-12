// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileReceiver.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

QByteArray sha256(QByteArrayView bytes)
{
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

bool writeFile(const QString &path, QByteArrayView bytes)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes.data(), bytes.size()) == bytes.size();
}

struct Fixture
{
  QTemporaryDir directory;
  TransferId transferId = QUuid::createUuid();
  FileId fileId = QUuid::createUuid();
  QByteArray contents = QByteArrayLiteral("first chunk|second chunk");

  FileReceiveRequest request(QString relativePath = QStringLiteral("资料/报告.txt"), quint32 chunkBytes = 12) const
  {
    const auto digest = sha256(contents);
    return {
        .receiveRoot = directory.path(),
        .entry =
            ManifestEntry{
                .id = fileId,
                .relativeProtocolPath = std::move(relativePath),
                .type = ManifestEntryType::File,
                .size = static_cast<quint64>(contents.size()),
                .sha256 = digest,
            },
        .begin = FileBeginMessage{transferId, fileId, static_cast<quint64>(contents.size()), 0, chunkBytes, digest},
    };
  }
};

} // namespace

class FileReceiverTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void streamsPartThenAtomicallyCommits();
  void commitsZeroByteFile();
  void rejectsUnsafeAndMismatchedRequests();
  void rejectsOutOfOrderAndWrongIdentity();
  void enforcesChunkAndExpectedSizeBounds();
  void hashMismatchNeverCommitsTarget();
  void sizeMismatchNeverCommitsTarget();
  void autoRenamePreservesExistingFile();
  void stalePartRequiresResumeState();
  void cancelDeleteAndKeepAreIdempotent();
};

void FileReceiverTests::streamsPartThenAtomicallyCommits()
{
  Fixture fixture;
  QVERIFY(fixture.directory.isValid());
  FileReceiver receiver;
  const auto request = fixture.request();
  const auto beginResult = receiver.begin(request);
  QVERIFY2(beginResult.ok(), qPrintable(beginResult.diagnostic));
  const auto started = receiver.snapshot();
  QCOMPARE(started.state, FileReceiverState::Receiving);
  QVERIFY(QFileInfo::exists(started.partPath));
  QVERIFY(!QFileInfo::exists(fixture.directory.filePath(QStringLiteral("资料/报告.txt"))));

  const QByteArray first = fixture.contents.first(12);
  const QByteArray second = fixture.contents.sliced(12);
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, first).ok());
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 12, 1}, second).ok());
  const auto finished = receiver.finish(
      {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()), sha256(fixture.contents)}
  );

  QVERIFY2(finished.ok(), qPrintable(finished.diagnostic));
  QVERIFY(finished.fileResult.has_value());
  QCOMPARE(finished.fileResult->code, FileResultCode::Ok);
  QCOMPARE(receiver.snapshot().state, FileReceiverState::Completed);
  QCOMPARE(readFile(receiver.snapshot().committedPath), fixture.contents);
  QVERIFY(!QFileInfo::exists(started.partPath));
}

void FileReceiverTests::commitsZeroByteFile()
{
  Fixture fixture;
  fixture.contents.clear();
  FileReceiver receiver;
  const auto request = fixture.request(QStringLiteral("empty.bin"));
  QVERIFY(receiver.begin(request).ok());
  const auto result = receiver.finish({fixture.transferId, fixture.fileId, 0, sha256(QByteArrayView{})});
  QVERIFY(result.ok());
  QVERIFY(QFileInfo::exists(receiver.snapshot().committedPath));
  QCOMPARE(QFileInfo(receiver.snapshot().committedPath).size(), 0);
}

void FileReceiverTests::rejectsUnsafeAndMismatchedRequests()
{
  Fixture fixture;
  auto unsafe = fixture.request(QStringLiteral("../escape.txt"));
  FileReceiver receiver;
  QCOMPARE(receiver.begin(unsafe).error, FileReceiverError::UnsafePath);
  QCOMPARE(receiver.snapshot().state, FileReceiverState::Idle);

  auto mismatch = fixture.request();
  mismatch.begin.expectedSha256 = QByteArray(32, '\x10');
  QCOMPARE(receiver.begin(mismatch).error, FileReceiverError::InvalidRequest);

  auto resume = fixture.request();
  resume.begin.startOffset = 1;
  QCOMPARE(receiver.begin(resume).error, FileReceiverError::InvalidRequest);

  auto unsupported = fixture.request();
  unsupported.conflictPolicy = ConflictPolicy::Overwrite;
  QCOMPARE(receiver.begin(unsupported).error, FileReceiverError::UnsupportedConflictPolicy);
}

void FileReceiverTests::rejectsOutOfOrderAndWrongIdentity()
{
  Fixture fixture;
  FileReceiver wrongTransfer;
  QVERIFY(wrongTransfer.begin(fixture.request()).ok());
  QCOMPARE(
      wrongTransfer.append({QUuid::createUuid(), fixture.fileId, 0, 0}, QByteArrayLiteral("x")).error,
      FileReceiverError::TransferIdMismatch
  );
  QCOMPARE(wrongTransfer.snapshot().state, FileReceiverState::Failed);

  FileReceiver wrongFile;
  const auto secondRequest = fixture.request(QStringLiteral("other.txt"));
  // Use another transfer so its deterministic staging path does not collide
  // with the retained failed partial from the previous receiver.
  auto independent = secondRequest;
  independent.begin.transferId = QUuid::createUuid();
  QVERIFY(wrongFile.begin(independent).ok());
  QCOMPARE(
      wrongFile.append({independent.begin.transferId, QUuid::createUuid(), 0, 0}, QByteArrayLiteral("x")).error,
      FileReceiverError::FileIdMismatch
  );

  Fixture orderedFixture;
  FileReceiver offset;
  QVERIFY(offset.begin(orderedFixture.request()).ok());
  QCOMPARE(
      offset.append({orderedFixture.transferId, orderedFixture.fileId, 1, 0}, QByteArrayLiteral("x")).error,
      FileReceiverError::OffsetMismatch
  );

  Fixture sequenceFixture;
  FileReceiver sequence;
  QVERIFY(sequence.begin(sequenceFixture.request()).ok());
  QCOMPARE(
      sequence.append({sequenceFixture.transferId, sequenceFixture.fileId, 0, 1}, QByteArrayLiteral("x")).error,
      FileReceiverError::SequenceMismatch
  );
}

void FileReceiverTests::enforcesChunkAndExpectedSizeBounds()
{
  Fixture fixture;
  FileReceiver empty;
  QVERIFY(empty.begin(fixture.request()).ok());
  QCOMPARE(
      empty.append({fixture.transferId, fixture.fileId, 0, 0}, QByteArrayView{}).error, FileReceiverError::EmptyChunk
  );

  Fixture chunkFixture;
  FileReceiver oversized;
  QVERIFY(oversized.begin(chunkFixture.request(QStringLiteral("chunk.bin"), 4)).ok());
  QCOMPARE(
      oversized.append({chunkFixture.transferId, chunkFixture.fileId, 0, 0}, QByteArrayLiteral("12345")).error,
      FileReceiverError::ChunkTooLarge
  );

  Fixture sizeFixture;
  sizeFixture.contents = QByteArrayLiteral("abc");
  FileReceiver overflow;
  QVERIFY(overflow.begin(sizeFixture.request(QStringLiteral("size.bin"), 4)).ok());
  QCOMPARE(
      overflow.append({sizeFixture.transferId, sizeFixture.fileId, 0, 0}, QByteArrayLiteral("abcd")).error,
      FileReceiverError::SizeOverflow
  );
}

void FileReceiverTests::hashMismatchNeverCommitsTarget()
{
  Fixture fixture;
  FileReceiver receiver;
  const auto request = fixture.request(QStringLiteral("hash.bin"), 64);
  QVERIFY(receiver.begin(request).ok());
  QByteArray corrupted = fixture.contents;
  corrupted[0] = 'X';
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, corrupted).ok());
  const QString partPath = receiver.snapshot().partPath;
  const auto result = receiver.finish(
      {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()), sha256(fixture.contents)}
  );
  QCOMPARE(result.error, FileReceiverError::HashMismatch);
  QCOMPARE(result.fileResult->code, FileResultCode::HashMismatch);
  QVERIFY(QFileInfo::exists(partPath));
  QVERIFY(!QFileInfo::exists(fixture.directory.filePath(QStringLiteral("hash.bin"))));
}

void FileReceiverTests::sizeMismatchNeverCommitsTarget()
{
  Fixture fixture;
  FileReceiver receiver;
  const auto request = fixture.request(QStringLiteral("size-mismatch.bin"), 64);
  QVERIFY(receiver.begin(request).ok());
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(3)).ok());
  const auto result = receiver.finish(
      {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()), sha256(fixture.contents)}
  );
  QCOMPARE(result.error, FileReceiverError::SizeMismatch);
  QCOMPARE(result.fileResult->code, FileResultCode::SizeMismatch);
  QVERIFY(!QFileInfo::exists(fixture.directory.filePath(QStringLiteral("size-mismatch.bin"))));
}

void FileReceiverTests::autoRenamePreservesExistingFile()
{
  Fixture fixture;
  const QString original = fixture.directory.filePath(QStringLiteral("report.txt"));
  QVERIFY(writeFile(original, QByteArrayLiteral("keep me")));
  FileReceiver receiver;
  const auto request = fixture.request(QStringLiteral("report.txt"), 64);
  QVERIFY(receiver.begin(request).ok());
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents).ok());
  QVERIFY(receiver
              .finish(
                  {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()),
                   sha256(fixture.contents)}
              )
              .ok());
  QCOMPARE(readFile(original), QByteArrayLiteral("keep me"));
  QCOMPARE(QFileInfo(receiver.snapshot().committedPath).fileName(), QStringLiteral("report (1).txt"));
  QCOMPARE(readFile(receiver.snapshot().committedPath), fixture.contents);
}

void FileReceiverTests::stalePartRequiresResumeState()
{
  Fixture fixture;
  const auto request = fixture.request(QStringLiteral("stale.bin"));
  QString partPath;
  {
    FileReceiver first;
    QVERIFY(first.begin(request).ok());
    partPath = first.snapshot().partPath;
  }
  QVERIFY(QFileInfo::exists(partPath));
  FileReceiver second;
  QCOMPARE(second.begin(request).error, FileReceiverError::StagingExists);
  QVERIFY(QFileInfo::exists(partPath));
}

void FileReceiverTests::cancelDeleteAndKeepAreIdempotent()
{
  Fixture fixture;
  FileReceiver remove;
  QVERIFY(remove.begin(fixture.request(QStringLiteral("remove.bin"), 64)).ok());
  QVERIFY(remove.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(3)).ok());
  const QString removedPart = remove.snapshot().partPath;
  const auto firstCancel = remove.cancel(false);
  QVERIFY(firstCancel.ok());
  QCOMPARE(firstCancel.fileResult->code, FileResultCode::Cancelled);
  QVERIFY(!QFileInfo::exists(removedPart));
  QVERIFY(remove.cancel(false).ok());

  Fixture keepFixture;
  FileReceiver keep;
  QVERIFY(keep.begin(keepFixture.request(QStringLiteral("keep.bin"), 64)).ok());
  QVERIFY(keep.append({keepFixture.transferId, keepFixture.fileId, 0, 0}, keepFixture.contents.first(3)).ok());
  const QString keptPart = keep.snapshot().partPath;
  QVERIFY(keep.cancel(true).ok());
  QVERIFY(QFileInfo::exists(keptPart));
  QCOMPARE(readFile(keptPart), keepFixture.contents.first(3));
}

QTEST_MAIN(FileReceiverTests)

#include "FileReceiverTests.moc"
