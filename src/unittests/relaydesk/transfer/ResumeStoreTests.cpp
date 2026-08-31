// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ResumeStore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace relaydesk::transfer;

namespace {

ResumeState makeState(const QString &suffix = QStringLiteral("alpha"))
{
  return {
      .transferId = *TransferId::fromString(QStringLiteral("11111111-2222-4333-8444-555555555555")),
      .peerDeviceId =
          *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .manifestSha256 = QByteArray(32, '\x5a'),
      .direction = ResumeDirection::Receiving,
      .files =
          {
              {
                  .fileId = *FileId::fromString(QStringLiteral("12345678-1234-4234-8234-1234567890ab")),
                  .relativeProtocolPath = QStringLiteral("资料/%1.bin").arg(suffix),
                  .durableOffset = 8U * 1024U * 1024U,
                  .totalBytes = 20U * 1024U * 1024U,
                  .partRelativePath = QStringLiteral("12345678-1234-4234-8234-1234567890ab.part"),
              },
              {
                  .fileId = *FileId::fromString(QStringLiteral("87654321-4321-4321-8321-ba0987654321")),
                  .relativeProtocolPath = QStringLiteral("empty.txt"),
                  .durableOffset = 0,
                  .totalBytes = 0,
                  .partRelativePath = QStringLiteral("87654321-4321-4321-8321-ba0987654321.part"),
              },
          },
      .resolvedTargets =
          {{
              .fileId = *FileId::fromString(QStringLiteral("aaaaaaaa-1234-4234-8234-1234567890ab")),
              .relativeTargetPath = QStringLiteral("资料/resolved.bin"),
              .size = 7,
              .sha256 = QByteArray(32, '\x3c'),
              .decision = IncomingConflictDecision::AutoRename,
          }},
      .updatedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'123LL, Qt::UTC),
  };
}

void writeBytes(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
  QCOMPARE(file.write(bytes), bytes.size());
}

} // namespace

class ResumeStoreTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTripsAndScansStates();
  void loadsLegacyV1WithoutResolvedTargets();
  void replacesAtomicallyWithoutClobberingValidState();
  void rejectsMalformedAndUnknownSchema();
  void rejectsInvalidStateBeforeWriting();
  void scanIsolatesCorruptAndOrphanFiles();
  void removalIsIdempotent();
};

void ResumeStoreTests::roundTripsAndScansStates()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.filePath(QStringLiteral("active")));
  const auto expected = makeState();

  const auto saved = store.save(expected);
  QVERIFY2(saved.ok(), qPrintable(saved.diagnostic));
  const auto loaded = store.load(expected.transferId);
  QVERIFY2(loaded.ok(), qPrintable(loaded.diagnostic));
  QCOMPARE(loaded.schemaVersion, kResumeStateSchemaVersion);
  QCOMPARE(*loaded.state, expected);

  const auto scanned = store.scan();
  QVERIFY(scanned.ok());
  QCOMPARE(scanned.issues.size(), 0);
  QCOMPARE(scanned.states, QList<ResumeState>{expected});
  QCOMPARE(QFileInfo(store.statePath(expected.transferId)).size() > 0, true);
}

void ResumeStoreTests::loadsLegacyV1WithoutResolvedTargets()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.path());
  auto expected = makeState();
  expected.resolvedTargets.clear();
  QCborArray files;
  for (const auto &file : expected.files) {
    files.append(QCborMap{
        {QCborValue(1), file.fileId.toBytes()},
        {QCborValue(2), file.relativeProtocolPath},
        {QCborValue(3), static_cast<qint64>(file.durableOffset)},
        {QCborValue(4), static_cast<qint64>(file.totalBytes)},
        {QCborValue(5), file.partRelativePath},
    });
  }
  const QCborMap legacy{
      {QCborValue(1), static_cast<qint64>(kLegacyResumeStateSchemaVersion)},
      {QCborValue(2), expected.transferId.toBytes()},
      {QCborValue(3), expected.peerDeviceId.toBytes()},
      {QCborValue(4), expected.manifestSha256},
      {QCborValue(5), QStringLiteral("receiving")},
      {QCborValue(6), files},
      {QCborValue(7), expected.updatedUtc.toMSecsSinceEpoch()},
  };
  writeBytes(
      store.statePath(expected.transferId),
      QCborValue(legacy).toCbor(QCborValue::EncodingOption::SortKeysInMaps)
  );
  const auto loaded = store.load(expected.transferId);
  QVERIFY2(loaded.ok(), qPrintable(loaded.diagnostic));
  QCOMPARE(loaded.schemaVersion, kLegacyResumeStateSchemaVersion);
  QCOMPARE(*loaded.state, expected);
}

void ResumeStoreTests::replacesAtomicallyWithoutClobberingValidState()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.filePath(QStringLiteral("active")));
  auto original = makeState();
  QVERIFY(store.save(original).ok());

  auto updated = original;
  updated.files[0].durableOffset += 4096;
  updated.updatedUtc = updated.updatedUtc.addSecs(1);
  QVERIFY(store.save(updated).ok());
  QCOMPARE(*store.load(updated.transferId).state, updated);

  auto invalid = updated;
  invalid.files[0].durableOffset = invalid.files[0].totalBytes + 1;
  QCOMPARE(store.save(invalid).error, ResumeStoreError::InvalidState);
  const auto afterRejectedUpdate = store.load(updated.transferId);
  QVERIFY(afterRejectedUpdate.ok());
  QCOMPARE(*afterRejectedUpdate.state, updated);

  QDir directory(QFileInfo(store.statePath(updated.transferId)).absolutePath());
  QCOMPARE(directory.entryList(QDir::Files | QDir::Hidden).size(), 1);
}

void ResumeStoreTests::rejectsMalformedAndUnknownSchema()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.path());
  const auto state = makeState();

  writeBytes(store.statePath(state.transferId), QByteArrayLiteral("not-cbor"));
  QCOMPARE(store.load(state.transferId).error, ResumeStoreError::MalformedCbor);

  QVERIFY(store.save(state).ok());
  QFile validSchema(store.statePath(state.transferId));
  QVERIFY(validSchema.open(QIODevice::ReadOnly));
  auto unsupportedMap = QCborValue::fromCbor(validSchema.readAll()).toMap();
  validSchema.close();
  unsupportedMap.insert(QCborValue(1), QCborValue(99));
  const QByteArray unsupported = QCborValue(unsupportedMap).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
  writeBytes(store.statePath(state.transferId), unsupported);
  QCOMPARE(store.load(state.transferId).error, ResumeStoreError::UnsupportedSchema);

  QVERIFY(store.save(state).ok());
  QFile valid(store.statePath(state.transferId));
  QVERIFY(valid.open(QIODevice::ReadOnly));
  QByteArray withTrailing = valid.readAll();
  valid.close();
  withTrailing.append('\0');
  writeBytes(store.statePath(state.transferId), withTrailing);
  QCOMPARE(store.load(state.transferId).error, ResumeStoreError::MalformedCbor);
}

void ResumeStoreTests::rejectsInvalidStateBeforeWriting()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.filePath(QStringLiteral("active")));

  auto hash = makeState();
  hash.manifestSha256.chop(1);
  QCOMPARE(store.save(hash).error, ResumeStoreError::InvalidState);

  auto traversal = makeState();
  traversal.files[0].relativeProtocolPath = QStringLiteral("../escape.bin");
  QCOMPARE(store.save(traversal).error, ResumeStoreError::InvalidPath);

  auto collision = makeState();
  collision.files[1].relativeProtocolPath = QStringLiteral("资料/ALPHA.bin");
  QCOMPARE(store.save(collision).error, ResumeStoreError::InvalidPath);

  auto duplicateId = makeState();
  duplicateId.files[1].fileId = duplicateId.files[0].fileId;
  QCOMPARE(store.save(duplicateId).error, ResumeStoreError::InvalidState);

  auto resolvedDuplicate = makeState();
  resolvedDuplicate.resolvedTargets[0].fileId = resolvedDuplicate.files[0].fileId;
  QCOMPARE(store.save(resolvedDuplicate).error, ResumeStoreError::InvalidState);

  auto cancelledDecision = makeState();
  cancelledDecision.resolvedTargets[0].decision = IncomingConflictDecision::CancelTransfer;
  QCOMPARE(store.save(cancelledDecision).error, ResumeStoreError::InvalidState);

  auto skippedWithPath = makeState();
  skippedWithPath.resolvedTargets[0].decision = IncomingConflictDecision::Skip;
  QCOMPARE(store.save(skippedWithPath).error, ResumeStoreError::InvalidPath);

  ResumeStore limited(temporary.filePath(QStringLiteral("limited")), {.maximumFiles = 1});
  QCOMPARE(limited.save(makeState()).error, ResumeStoreError::TooManyFiles);
  QVERIFY(!QDir(temporary.filePath(QStringLiteral("active"))).exists());
}

void ResumeStoreTests::scanIsolatesCorruptAndOrphanFiles()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.path());
  const auto state = makeState();
  QVERIFY(store.save(state).ok());

  writeBytes(temporary.filePath(QStringLiteral("bad.resume.cbor")), QByteArrayLiteral("bad"));
  writeBytes(temporary.filePath(QStringLiteral("orphan.part")), QByteArrayLiteral("partial"));

  const auto scan = store.scan();
  QVERIFY(scan.ok());
  QCOMPARE(scan.states, QList<ResumeState>{state});
  QCOMPARE(scan.issues.size(), 1);
  QCOMPARE(scan.issues.constFirst().error, ResumeStoreError::InvalidFields);
}

void ResumeStoreTests::removalIsIdempotent()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.path());
  const auto state = makeState();
  QVERIFY(store.save(state).ok());
  QVERIFY(store.remove(state.transferId).ok());
  QVERIFY(store.remove(state.transferId).ok());
  QCOMPARE(store.load(state.transferId).error, ResumeStoreError::NotFound);
}

QTEST_GUILESS_MAIN(ResumeStoreTests)
#include "ResumeStoreTests.moc"
