// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferRecoveryStore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

using namespace relaydesk::transfer;

namespace {

const auto kTransfer = *TransferId::fromString(QStringLiteral("11111111-2222-4333-8444-555555555555"));
const auto kLocal = *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));
const auto kPeer = *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff"));
const auto kFile = *FileId::fromString(QStringLiteral("12345678-1234-4234-8234-1234567890ab"));

ManifestEntry entry()
{
  return {
      .id = kFile,
      .relativeProtocolPath = QStringLiteral("folder/file.bin"),
      .type = ManifestEntryType::File,
      .size = 7,
      .modifiedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC),
      .sha256 = QByteArray(32, '\x21')
  };
}

OutgoingRecoveryState outgoing()
{
  const auto value = entry();
  OutgoingRecoveryState state{
      .transferId = kTransfer,
      .localDeviceId = kLocal,
      .peerDeviceId = kPeer,
      .peerFingerprintSha256 = QByteArray(32, '\x42'),
      .createdUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'123LL, Qt::UTC),
      .sendOptions = {.conflictPolicy = ConflictPolicy::AutoRename},
      .sourceRoots =
          {{.canonicalPath = QDir::rootPath() + QStringLiteral("source"),
            .relativeProtocolPath = QStringLiteral("folder"),
            .type = ManifestEntryType::Directory}},
      .entries =
          {{.canonicalSourcePath = QDir::rootPath() + QStringLiteral("source/file.bin"),
            .protocolCollisionKey = QStringLiteral("folder/file.bin"),
            .entry = value}},
      .summary =
          {.id = kTransfer,
           .displayName = QStringLiteral("folder"),
           .totalBytes = 7,
           .fileCount = 1,
           .directoryCount = 0,
           .canonicalSha256 = ManifestPageCodec::canonicalSha256({value})},
      .effectiveConflictPolicy = ConflictPolicy::AutoRename,
      .progress = {.completedBytes = 2, .completedFiles = 0, .currentEntry = 0}
  };
  const auto plan = ManifestPageCodec::plan({.entries = state.entries, .summary = state.summary});
  Q_ASSERT(plan.ok());
  state.pagePlan = {
      .entryCount = plan.plan->entryCount,
      .pageCount = plan.plan->pageCount(),
      .totalMetadataBytes = plan.plan->totalMetadataBytes
  };
  return state;
}

IncomingRecoveryState incoming()
{
  const auto value = entry();
  const auto hash = ManifestPageCodec::canonicalSha256({value});
  IncomingRecoveryState state{
      .transferId = kTransfer,
      .localDeviceId = kLocal,
      .peerDeviceId = kPeer,
      .peerFingerprintSha256 = QByteArray(32, '\x42'),
      .peerDisplayName = QStringLiteral("Peer"),
      .offer =
          {.transferId = kTransfer,
           .displayName = QStringLiteral("folder"),
           .totalBytes = 7,
           .fileCount = 1,
           .directoryCount = 0,
           .manifestSha256 = hash,
           .manifestPageCount = 1,
           .requestedConflictPolicy = ConflictPolicy::AutoRename,
           .createdAtMs = 1'780'000'000'123ULL},
      .receiveOptions =
          {.destinationRoot = QDir::rootPath() + QStringLiteral("receive"), .conflictPolicy = ConflictPolicy::AutoRename
          },
      .entries = {value},
      .negotiatedCapabilities =
          {.protocolMajorVersion = 1,
           .features = {QStringLiteral("file.v1"), QStringLiteral("resume.v1")},
           .chunkBytes = 1024,
           .maxPayloadBytes = 4096,
           .maxConcurrentTransfers = 2,
           .maxConcurrentFiles = 2,
           .maxManifestEntries = 100,
           .conflictPolicies = {ConflictPolicy::AutoRename},
           .localCanReceiveFiles = true,
           .peerCanReceiveFiles = true}
  };
  const auto plan = ManifestPageCodec::plan(
      {.entries =
           {{.canonicalSourcePath = QDir::rootPath(),
             .protocolCollisionKey = QStringLiteral("folder/file.bin"),
             .entry = value}},
       .summary =
           {.id = kTransfer,
            .displayName = QStringLiteral("folder"),
            .totalBytes = 7,
            .fileCount = 1,
            .directoryCount = 0,
            .canonicalSha256 = hash}}
  );
  Q_ASSERT(plan.ok());
  state.pagePlan = {
      .entryCount = plan.plan->entryCount,
      .pageCount = plan.plan->pageCount(),
      .totalMetadataBytes = plan.plan->totalMetadataBytes
  };
  return state;
}

void writeBytes(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(bytes), bytes.size());
}

} // namespace

class TransferRecoveryStoreTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTripsBothDirections();
  void replacesValidStateAndRejectsInvalidWithoutClobbering();
  void scanIsolatesCorruptAndUnknownSchema();
  void scanBoundsDecodedStateCount();
  void rejectsUnsafePathsAndLimitsBeforeWriting();
  void removalIsIdempotentInBothDirections();
  void rejectsIdentityEnumAndCollisionBoundaries();
  void rejectsInvalidCapabilitiesAndIntegerOverflowWithoutClobbering();
  void isolatesTamperedIncomingRecords();
  void scanReportsSymbolicLinkWithoutFollowingTarget();
};

void TransferRecoveryStoreTests::roundTripsBothDirections()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  const auto expectedOutgoing = outgoing();
  const auto expectedIncoming = incoming();
  QVERIFY(store.saveOutgoing(expectedOutgoing).ok());
  QVERIFY(store.saveIncoming(expectedIncoming).ok());
  const auto loadedOutgoing = store.loadOutgoing(kTransfer);
  const auto loadedIncoming = store.loadIncoming(kTransfer);
  QVERIFY2(loadedOutgoing.ok(), qPrintable(loadedOutgoing.diagnostic));
  QVERIFY2(loadedIncoming.ok(), qPrintable(loadedIncoming.diagnostic));
  QCOMPARE(*loadedOutgoing.state, expectedOutgoing);
  QCOMPARE(*loadedIncoming.state, expectedIncoming);
  const auto outgoingScan = store.scanOutgoing();
  const auto incomingScan = store.scanIncoming();
  QVERIFY(outgoingScan.ok());
  QVERIFY(incomingScan.ok());
  QCOMPARE(outgoingScan.states, QList<OutgoingRecoveryState>{expectedOutgoing});
  QCOMPARE(incomingScan.states, QList<IncomingRecoveryState>{expectedIncoming});
}

void TransferRecoveryStoreTests::replacesValidStateAndRejectsInvalidWithoutClobbering()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  auto original = outgoing();
  QVERIFY(store.saveOutgoing(original).ok());
  auto updated = original;
  updated.progress.completedBytes = 5;
  QVERIFY(store.saveOutgoing(updated).ok());
  const auto loaded = store.loadOutgoing(kTransfer);
  QVERIFY2(loaded.ok(), qPrintable(loaded.diagnostic));
  QCOMPARE(*loaded.state, updated);
  auto invalid = updated;
  invalid.progress.completedBytes = 8;
  QCOMPARE(store.saveOutgoing(invalid).error, TransferRecoveryStoreError::InvalidState);
  const auto afterRejected = store.loadOutgoing(kTransfer);
  QVERIFY2(afterRejected.ok(), qPrintable(afterRejected.diagnostic));
  QCOMPARE(*afterRejected.state, updated);
}

void TransferRecoveryStoreTests::scanIsolatesCorruptAndUnknownSchema()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  QVERIFY(store.saveIncoming(incoming()).ok());
  writeBytes(
      QDir(temporary.path()).filePath(QStringLiteral("incoming/22222222-2222-4222-8222-222222222222.recovery.cbor")),
      QByteArrayLiteral("bad")
  );
  auto scan = store.scanIncoming();
  QCOMPARE(scan.states, QList<IncomingRecoveryState>{incoming()});
  QCOMPARE(scan.issues.size(), 1);
  QCOMPARE(scan.issues.first().error, TransferRecoveryStoreError::MalformedCbor);

  QFile input(store.incomingStatePath(kTransfer));
  QVERIFY(input.open(QIODevice::ReadOnly));
  auto map = QCborValue::fromCbor(input.readAll()).toMap();
  map.insert(QCborValue(1), QCborValue(99));
  writeBytes(store.incomingStatePath(kTransfer), QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps));
  QCOMPARE(store.loadIncoming(kTransfer).error, TransferRecoveryStoreError::UnsupportedSchema);
}

void TransferRecoveryStoreTests::scanBoundsDecodedStateCount()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore writer(temporary.path());
  constexpr quint64 MaximumStates = 32;
  for (quint64 index = 0; index <= MaximumStates; ++index) {
    auto outgoingState = outgoing();
    outgoingState.transferId = TransferId::generate();
    outgoingState.summary.id = outgoingState.transferId;
    QVERIFY(writer.saveOutgoing(outgoingState).ok());
    auto incomingState = incoming();
    incomingState.transferId = TransferId::generate();
    incomingState.offer.transferId = incomingState.transferId;
    QVERIFY(writer.saveIncoming(incomingState).ok());
  }

  TransferRecoveryStore limited(temporary.path(), {.maximumStates = MaximumStates});
  const auto outgoingScan = limited.scanOutgoing();
  const auto incomingScan = limited.scanIncoming();
  QVERIFY(outgoingScan.ok());
  QVERIFY(incomingScan.ok());
  QCOMPARE(outgoingScan.states.size(), static_cast<qsizetype>(MaximumStates));
  QCOMPARE(incomingScan.states.size(), static_cast<qsizetype>(MaximumStates));
  QCOMPARE(outgoingScan.issues.size(), 1);
  QCOMPARE(incomingScan.issues.size(), 1);
  QCOMPARE(outgoingScan.issues.constFirst().error, TransferRecoveryStoreError::TooManyStates);
  QCOMPARE(incomingScan.issues.constFirst().error, TransferRecoveryStoreError::TooManyStates);
}

void TransferRecoveryStoreTests::rejectsUnsafePathsAndLimitsBeforeWriting()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  auto state = outgoing();
  state.entries[0].canonicalSourcePath = QStringLiteral("relative.bin");
  TransferRecoveryStore store(temporary.path());
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidPath);
  state = outgoing();
  state.entries[0].entry.relativeProtocolPath = QStringLiteral("../escape.bin");
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidPath);
  TransferRecoveryStore limited(temporary.filePath(QStringLiteral("limited")), {.maximumEntries = 0});
  QCOMPARE(limited.saveOutgoing(outgoing()).error, TransferRecoveryStoreError::TooManyEntries);
  QVERIFY(!QDir(temporary.filePath(QStringLiteral("limited"))).exists());
}

void TransferRecoveryStoreTests::removalIsIdempotentInBothDirections()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  QVERIFY(store.saveOutgoing(outgoing()).ok());
  QVERIFY(store.saveIncoming(incoming()).ok());
  QVERIFY(store.removeOutgoing(kTransfer).ok());
  QVERIFY(store.removeOutgoing(kTransfer).ok());
  QVERIFY(store.removeIncoming(kTransfer).ok());
  QVERIFY(store.removeIncoming(kTransfer).ok());
  QCOMPARE(store.loadOutgoing(kTransfer).error, TransferRecoveryStoreError::NotFound);
  QCOMPARE(store.loadIncoming(kTransfer).error, TransferRecoveryStoreError::NotFound);
}

void TransferRecoveryStoreTests::rejectsIdentityEnumAndCollisionBoundaries()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  auto state = outgoing();
  state.peerFingerprintSha256.chop(1);
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidState);
  state = outgoing();
  state.sendOptions.conflictPolicy = static_cast<ConflictPolicy>(99);
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidState);
  state = outgoing();
  state.entries.append(state.entries.first());
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidState);
  state = outgoing();
  state.entries[0].protocolCollisionKey = QStringLiteral("wrong");
  QCOMPARE(store.saveOutgoing(state).error, TransferRecoveryStoreError::InvalidState);
  TransferRecoveryStore relative(QStringLiteral("relative"));
  QCOMPARE(relative.saveOutgoing(outgoing()).error, TransferRecoveryStoreError::InvalidStoreDirectory);
}

void TransferRecoveryStoreTests::rejectsInvalidCapabilitiesAndIntegerOverflowWithoutClobbering()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  const auto valid = incoming();
  QVERIFY(store.saveIncoming(valid).ok());
  auto invalid = valid;
  invalid.negotiatedCapabilities.maxConcurrentTransfers = 65;
  QCOMPARE(store.saveIncoming(invalid).error, TransferRecoveryStoreError::InvalidState);
  invalid = valid;
  invalid.negotiatedCapabilities.features.append(QStringLiteral("resume.v1"));
  QCOMPARE(store.saveIncoming(invalid).error, TransferRecoveryStoreError::InvalidState);
  const auto loaded = store.loadIncoming(kTransfer);
  QVERIFY(loaded.ok());
  QCOMPARE(*loaded.state, valid);
  auto overflow = outgoing();
  overflow.entries[0].entry.size = quint64{1} << 63;
  QCOMPARE(store.saveOutgoing(overflow).error, TransferRecoveryStoreError::InvalidState);
  auto incomingOverflow = valid;
  incomingOverflow.offer.createdAtMs = static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1;
  QCOMPARE(store.saveIncoming(incomingOverflow).error, TransferRecoveryStoreError::InvalidState);

  auto invalidSources = outgoing();
  invalidSources.sourceRoots.clear();
  QCOMPARE(store.saveOutgoing(invalidSources).error, TransferRecoveryStoreError::InvalidPath);
  invalidSources = outgoing();
  invalidSources.sourceRoots.append(invalidSources.sourceRoots.first());
  QCOMPARE(store.saveOutgoing(invalidSources).error, TransferRecoveryStoreError::InvalidPath);
}

void TransferRecoveryStoreTests::isolatesTamperedIncomingRecords()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  const auto valid = incoming();
  QVERIFY(store.saveIncoming(valid).ok());
  auto tampered = valid;
  tampered.transferId = *TransferId::fromString(QStringLiteral("22222222-2222-4222-8222-222222222222"));
  tampered.offer.transferId = tampered.transferId;
  tampered.offer.manifestSha256 = ManifestPageCodec::canonicalSha256({tampered.entries.first()});
  const auto savedTampered = store.saveIncoming(tampered);
  QVERIFY2(savedTampered.ok(), qPrintable(savedTampered.diagnostic));
  QFile input(store.incomingStatePath(tampered.transferId));
  QVERIFY(input.open(QIODevice::ReadOnly));
  const auto original = QCborValue::fromCbor(input.readAll()).toMap();
  input.close();
  for (int mutation = 0; mutation < 5; ++mutation) {
    auto map = original;
    if (mutation == 0) {
      auto offer = map.value(QCborValue(15)).toMap();
      offer.insert(QCborValue(6), QByteArray(32, '\x7f'));
      map.insert(QCborValue(15), offer);
    } else if (mutation == 1) {
      auto plan = map.value(QCborValue(11)).toMap();
      plan.insert(QCborValue(2), plan.value(QCborValue(2)).toInteger() + 1);
      map.insert(QCborValue(11), plan);
    } else if (mutation == 2) {
      map.insert(QCborValue(99), QCborValue(1));
    } else if (mutation == 3) {
      auto caps = map.value(QCborValue(17)).toMap();
      caps.insert(QCborValue(5), QCborValue(65));
      map.insert(QCborValue(17), caps);
    } else {
      auto entries = map.value(QCborValue(9)).toArray();
      auto first = entries.first().toMap();
      first.insert(QCborValue(7), QCborValue(qint64{4'294'967'296LL}));
      entries[0] = first;
      map.insert(QCborValue(9), entries);
    }
    writeBytes(
        store.incomingStatePath(tampered.transferId), QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps)
    );
    QCOMPARE(store.loadIncoming(tampered.transferId).error, TransferRecoveryStoreError::InvalidFields);
    const auto scan = store.scanIncoming();
    QCOMPARE(scan.states, QList<IncomingRecoveryState>{valid});
    QCOMPARE(scan.issues.size(), 1);
    const auto restored = store.saveIncoming(tampered);
    QVERIFY2(restored.ok(), qPrintable(restored.diagnostic));
  }

  auto outgoingState = outgoing();
  QVERIFY(store.saveOutgoing(outgoingState).ok());
  QFile outgoingInput(store.outgoingStatePath(outgoingState.transferId));
  QVERIFY(outgoingInput.open(QIODevice::ReadOnly));
  auto outgoingMap = QCborValue::fromCbor(outgoingInput.readAll()).toMap();
  outgoingInput.close();
  auto sources = outgoingMap.value(QCborValue(8)).toArray();
  sources.append(sources.first());
  outgoingMap.insert(QCborValue(8), sources);
  writeBytes(
      store.outgoingStatePath(outgoingState.transferId),
      QCborValue(outgoingMap).toCbor(QCborValue::EncodingOption::SortKeysInMaps)
  );
  QCOMPARE(store.loadOutgoing(outgoingState.transferId).error, TransferRecoveryStoreError::InvalidFields);
}

void TransferRecoveryStoreTests::scanReportsSymbolicLinkWithoutFollowingTarget()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferRecoveryStore store(temporary.path());
  QVERIFY(store.saveIncoming(incoming()).ok());
  const auto linkPath = QDir(temporary.path()).filePath(QStringLiteral("incoming/link.recovery.cbor"));
  if (!QFile::link(store.incomingStatePath(kTransfer), linkPath))
    QSKIP("the current Windows token cannot create a symbolic link");
  if (!QFileInfo(linkPath).isSymLink()) {
    QFile::remove(linkPath);
    QSKIP("platform did not create symbolic link");
  }
  const auto scan = store.scanIncoming();
  QCOMPARE(scan.states, QList<IncomingRecoveryState>{incoming()});
  QCOMPARE(scan.issues.size(), 1);
  QCOMPARE(scan.issues.first().error, TransferRecoveryStoreError::InvalidPath);
}

QTEST_GUILESS_MAIN(TransferRecoveryStoreTests)
#include "TransferRecoveryStoreTests.moc"
