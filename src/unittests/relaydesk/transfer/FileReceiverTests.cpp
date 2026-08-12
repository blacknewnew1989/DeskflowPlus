// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileReceiver.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <optional>

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

ResumeState resumeStateFor(const Fixture &fixture, const FileReceiverSnapshot &snapshot)
{
  return {
      .transferId = fixture.transferId,
      .peerDeviceId =
          *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .manifestSha256 = QByteArray(32, '\x2a'),
      .direction = ResumeDirection::Receiving,
      .files =
          {
              {
                  .fileId = fixture.fileId,
                  .relativeProtocolPath = snapshot.relativeProtocolPath,
                  .durableOffset = 0,
                  .totalBytes = snapshot.expectedSize,
                  .partRelativePath = snapshot.partRelativePath,
              },
          },
      .updatedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC),
  };
}

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
  void persistsOnlySyncedDurableCheckpoints();
  void rejectsMismatchedCheckpointState();
  void failedCheckpointPersistenceNeverAdvancesState();
  void restartsFromDurableCheckpoint_data();
  void restartsFromDurableCheckpoint();
  void rejectsMissingCorruptAndMismatchedResumeState();
  void rejectsPartSizeMismatch();
  void modifiedPartFailsFinalIntegrity();
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
  QCOMPARE(receiver.begin(resume).error, FileReceiverError::ResumeStateMismatch);

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

void FileReceiverTests::persistsOnlySyncedDurableCheckpoints()
{
  Fixture fixture;
  FileReceiver receiver;
  QVERIFY(receiver.begin(fixture.request()).ok());
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto state = resumeStateFor(fixture, receiver.snapshot());

  const QByteArray first = fixture.contents.first(12);
  const QByteArray second = fixture.contents.sliced(12);
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, first).ok());
  const auto firstCheckpoint =
      receiver.checkpoint(store, state, QDateTime::fromMSecsSinceEpoch(1'780'000'001'000LL, Qt::UTC));
  QVERIFY2(firstCheckpoint.ok(), qPrintable(firstCheckpoint.diagnostic));
  QCOMPARE(firstCheckpoint.message->durableOffset, quint64{12});
  QCOMPARE(state.files.constFirst().durableOffset, quint64{12});
  QCOMPARE(store.load(fixture.transferId).state->files.constFirst().durableOffset, quint64{12});

  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 12, 1}, second).ok());
  QCOMPARE(store.load(fixture.transferId).state->files.constFirst().durableOffset, quint64{12});

  const auto finalCheckpoint =
      receiver.checkpoint(store, state, QDateTime::fromMSecsSinceEpoch(1'780'000'002'000LL, Qt::UTC));
  QVERIFY2(finalCheckpoint.ok(), qPrintable(finalCheckpoint.diagnostic));
  QCOMPARE(finalCheckpoint.message->durableOffset, static_cast<quint64>(fixture.contents.size()));
  const auto persisted = store.load(fixture.transferId);
  QVERIFY(persisted.ok());
  QCOMPARE(persisted.state->files.constFirst().durableOffset, static_cast<quint64>(fixture.contents.size()));
  QCOMPARE(QFileInfo(receiver.snapshot().partPath).size(), fixture.contents.size());
}

void FileReceiverTests::rejectsMismatchedCheckpointState()
{
  Fixture fixture;
  FileReceiver receiver;
  QVERIFY(receiver.begin(fixture.request()).ok());
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(12)).ok());
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto state = resumeStateFor(fixture, receiver.snapshot());

  auto wrongTransfer = state;
  wrongTransfer.transferId = QUuid::createUuid();
  QCOMPARE(receiver.checkpoint(store, wrongTransfer).error, DurableCheckpointError::ResumeStateMismatch);
  auto wrongPart = state;
  wrongPart.files[0].partRelativePath = QStringLiteral("different.part");
  QCOMPARE(receiver.checkpoint(store, wrongPart).error, DurableCheckpointError::ResumeStateMismatch);
  auto offsetAhead = state;
  offsetAhead.files[0].durableOffset = 13;
  QCOMPARE(receiver.checkpoint(store, offsetAhead).error, DurableCheckpointError::ResumeStateMismatch);
  QCOMPARE(store.load(fixture.transferId).error, ResumeStoreError::NotFound);
}

void FileReceiverTests::failedCheckpointPersistenceNeverAdvancesState()
{
  Fixture fixture;
  FileReceiver receiver;
  QVERIFY(receiver.begin(fixture.request()).ok());
  QVERIFY(receiver.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(12)).ok());
  auto state = resumeStateFor(fixture, receiver.snapshot());

  const QString blocker = fixture.directory.filePath(QStringLiteral("not-a-directory"));
  QVERIFY(writeFile(blocker, QByteArrayLiteral("block")));
  ResumeStore blockedStore(blocker);
  const auto failed = receiver.checkpoint(blockedStore, state);
  QCOMPARE(failed.error, DurableCheckpointError::PersistFailed);
  QVERIFY(!failed.message.has_value());
  QCOMPARE(state.files.constFirst().durableOffset, quint64{0});

  ResumeStore workingStore(fixture.directory.filePath(QStringLiteral("resume/active")));
  const auto recovered = receiver.checkpoint(workingStore, state);
  QVERIFY2(recovered.ok(), qPrintable(recovered.diagnostic));
  QCOMPARE(recovered.message->durableOffset, quint64{12});
  QCOMPARE(workingStore.load(fixture.transferId).state->files.constFirst().durableOffset, quint64{12});
}

void FileReceiverTests::restartsFromDurableCheckpoint_data()
{
  QTest::addColumn<quint64>("durableOffset");
  QTest::newRow("zero") << quint64{0};
  QTest::newRow("middle") << quint64{12};
  QTest::newRow("near-end") << quint64{23};
}

void FileReceiverTests::restartsFromDurableCheckpoint()
{
  QFETCH(quint64, durableOffset);
  Fixture fixture;
  const QByteArray manifestSha256(32, '\x5a');
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto initialRequest = fixture.request(QStringLiteral("restart/报告.bin"), 12);
  std::optional<ResumeState> state;
  QString partPath;
  {
    FileReceiver initial;
    QVERIFY(initial.begin(initialRequest).ok());
    quint64 offset = 0;
    quint64 sequence = 0;
    while (offset < durableOffset) {
      const qsizetype bytes = static_cast<qsizetype>(std::min<quint64>(12, durableOffset - offset));
      const QByteArray payload = fixture.contents.sliced(static_cast<qsizetype>(offset), bytes);
      QVERIFY(initial.append({fixture.transferId, fixture.fileId, offset, sequence}, payload).ok());
      offset += static_cast<quint64>(bytes);
      ++sequence;
    }
    state = resumeStateFor(fixture, initial.snapshot());
    state->manifestSha256 = manifestSha256;
    const auto checkpoint = initial.checkpoint(store, *state);
    QVERIFY2(checkpoint.ok(), qPrintable(checkpoint.diagnostic));
    QCOMPARE(checkpoint.message->durableOffset, durableOffset);
    partPath = initial.snapshot().partPath;
  }
  QVERIFY(QFileInfo::exists(partPath));
  const auto loaded = store.load(fixture.transferId);
  QVERIFY2(loaded.ok(), qPrintable(loaded.diagnostic));

  auto resumeRequest = initialRequest;
  resumeRequest.begin.startOffset = durableOffset;
  resumeRequest.manifestSha256 = manifestSha256;
  FileReceiver restarted;
  const auto resumed = restarted.resume(resumeRequest, *loaded.state);
  QVERIFY2(resumed.ok(), qPrintable(resumed.diagnostic));
  QCOMPARE(restarted.snapshot().receivedBytes, durableOffset);
  QCOMPARE(restarted.snapshot().nextSequence, quint64{0});

  quint64 offset = durableOffset;
  quint64 sequence = 0;
  while (offset < static_cast<quint64>(fixture.contents.size())) {
    const qsizetype bytes =
        static_cast<qsizetype>(std::min<quint64>(12, static_cast<quint64>(fixture.contents.size()) - offset));
    const QByteArray payload = fixture.contents.sliced(static_cast<qsizetype>(offset), bytes);
    QVERIFY(restarted.append({fixture.transferId, fixture.fileId, offset, sequence}, payload).ok());
    offset += static_cast<quint64>(bytes);
    ++sequence;
  }
  const auto completed = restarted.finish(
      {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()), sha256(fixture.contents)}
  );
  QVERIFY2(completed.ok(), qPrintable(completed.diagnostic));
  QCOMPARE(restarted.snapshot().state, FileReceiverState::Completed);
  QCOMPARE(readFile(restarted.snapshot().committedPath), fixture.contents);
  QVERIFY(!QFileInfo::exists(partPath));
}

void FileReceiverTests::rejectsMissingCorruptAndMismatchedResumeState()
{
  Fixture fixture;
  const QByteArray manifestSha256(32, '\x6b');
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto request = fixture.request(QStringLiteral("state.bin"), 12);
  std::optional<ResumeState> state;
  {
    FileReceiver initial;
    QVERIFY(initial.begin(request).ok());
    QVERIFY(initial.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(12)).ok());
    state = resumeStateFor(fixture, initial.snapshot());
    state->manifestSha256 = manifestSha256;
    QVERIFY(initial.checkpoint(store, *state).ok());
  }
  request.begin.startOffset = 12;
  request.manifestSha256 = manifestSha256;
  FileReceiver missing;
  QCOMPARE(missing.begin(request).error, FileReceiverError::ResumeStateMismatch);

  auto wrongManifest = request;
  wrongManifest.manifestSha256 = QByteArray(32, '\x7c');
  FileReceiver mismatched;
  QCOMPARE(mismatched.resume(wrongManifest, *state).error, FileReceiverError::ResumeStateMismatch);

  QVERIFY(writeFile(store.statePath(fixture.transferId), QByteArrayLiteral("not-cbor")));
  QCOMPARE(store.load(fixture.transferId).error, ResumeStoreError::MalformedCbor);
  FileReceiver corrupt;
  QCOMPARE(corrupt.begin(request).error, FileReceiverError::ResumeStateMismatch);
}

void FileReceiverTests::rejectsPartSizeMismatch()
{
  Fixture fixture;
  const QByteArray manifestSha256(32, '\x3d');
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto request = fixture.request(QStringLiteral("size-state.bin"), 12);
  std::optional<ResumeState> state;
  QString partPath;
  {
    FileReceiver initial;
    QVERIFY(initial.begin(request).ok());
    QVERIFY(initial.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(12)).ok());
    state = resumeStateFor(fixture, initial.snapshot());
    state->manifestSha256 = manifestSha256;
    QVERIFY(initial.checkpoint(store, *state).ok());
    partPath = initial.snapshot().partPath;
  }
  QFile truncated(partPath);
  QVERIFY(truncated.open(QIODevice::ReadWrite));
  QVERIFY(truncated.resize(11));
  truncated.close();
  request.begin.startOffset = 12;
  request.manifestSha256 = manifestSha256;

  FileReceiver restarted;
  QCOMPARE(restarted.resume(request, *state).error, FileReceiverError::StagingSizeMismatch);
  QCOMPARE(restarted.snapshot().state, FileReceiverState::Idle);
}

void FileReceiverTests::modifiedPartFailsFinalIntegrity()
{
  Fixture fixture;
  const QByteArray manifestSha256(32, '\x4e');
  ResumeStore store(fixture.directory.filePath(QStringLiteral("resume/active")));
  auto request = fixture.request(QStringLiteral("modified-part.bin"), 12);
  std::optional<ResumeState> state;
  QString partPath;
  {
    FileReceiver initial;
    QVERIFY(initial.begin(request).ok());
    QVERIFY(initial.append({fixture.transferId, fixture.fileId, 0, 0}, fixture.contents.first(12)).ok());
    state = resumeStateFor(fixture, initial.snapshot());
    state->manifestSha256 = manifestSha256;
    QVERIFY(initial.checkpoint(store, *state).ok());
    partPath = initial.snapshot().partPath;
  }
  QVERIFY(writeFile(partPath, QByteArray(12, '\x55')));
  request.begin.startOffset = 12;
  request.manifestSha256 = manifestSha256;
  FileReceiver restarted;
  QVERIFY(restarted.resume(request, *state).ok());
  QVERIFY(restarted.append({fixture.transferId, fixture.fileId, 12, 0}, fixture.contents.sliced(12)).ok());

  const auto completed = restarted.finish(
      {fixture.transferId, fixture.fileId, static_cast<quint64>(fixture.contents.size()), sha256(fixture.contents)}
  );
  QCOMPARE(completed.error, FileReceiverError::HashMismatch);
  QVERIFY(QFileInfo::exists(partPath));
  QVERIFY(!QFileInfo::exists(fixture.directory.filePath(QStringLiteral("modified-part.bin"))));
}

QTEST_MAIN(FileReceiverTests)

#include "FileReceiverTests.moc"
