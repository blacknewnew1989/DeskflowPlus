// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/FileReceiver.h"
#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/ManifestBuilder.h"
#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/ResumeMessageCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferSender.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <optional>

using namespace relaydesk::transfer;

namespace {

constexpr quint32 kTestChunkBytes = 64;
const auto kPeerId = *deskflow::relaydesk::DeviceId::fromString(
    QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")
);
const auto kCreatedUtc = QDateTime::fromMSecsSinceEpoch(1'800'000'000'000LL, Qt::UTC);

QByteArray sha256(QByteArrayView bytes)
{
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool writeFile(const QString &path, QByteArrayView bytes)
{
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

TransferSenderRequest senderRequest(
    const PreparedManifestEntry &source, const TransferId &transferId, quint64 streamId, quint64 startOffset = 0
)
{
  return {
      .transferId = transferId,
      .source = source,
      .streamId = streamId,
      .chunkBytes = kTestChunkBytes,
      .startOffset = startOffset,
  };
}

bool decodeWireFrame(const Frame &outgoing, Frame &incoming, QString &diagnostic)
{
  const QByteArray encoded = FrameCodec::encode(outgoing, {}, &diagnostic);
  if (encoded.isEmpty()) {
    return false;
  }

  const qsizetype split = std::min<qsizetype>(13, encoded.size() - 1);
  QByteArray receiveBuffer = encoded.first(split);
  Frame decoded;
  if (FrameCodec::tryDecode(receiveBuffer, decoded).status != FrameDecodeStatus::NeedMoreData) {
    diagnostic = QStringLiteral("fragmented frame was not held until complete");
    return false;
  }
  receiveBuffer.append(encoded.sliced(split));
  const auto result = FrameCodec::tryDecode(receiveBuffer, decoded);
  if (result.status != FrameDecodeStatus::FrameReady || !receiveBuffer.isEmpty()) {
    diagnostic = result.diagnostic.isEmpty() ? QStringLiteral("wire frame did not decode") : result.diagnostic;
    return false;
  }
  incoming = std::move(decoded);
  return true;
}

bool nextWireFrame(TransferSender &sender, Frame &frame, QString &diagnostic)
{
  for (quint64 attempt = 0; attempt < 1'000'000; ++attempt) {
    auto produced = sender.nextFrame();
    if (produced.status == SenderFrameStatus::Preparing) {
      continue;
    }
    if (!produced.ready()) {
      diagnostic = produced.diagnostic.isEmpty() ? QStringLiteral("sender did not produce a frame") : produced.diagnostic;
      return false;
    }
    return decodeWireFrame(*produced.frame, frame, diagnostic);
  }
  diagnostic = QStringLiteral("sender preparation exceeded the deterministic iteration bound");
  return false;
}

bool discardPartialWireFrame(const Frame &frame, QString &diagnostic)
{
  const QByteArray encoded = FrameCodec::encode(frame, {}, &diagnostic);
  if (encoded.size() < 2) {
    diagnostic = QStringLiteral("encoded frame is unexpectedly short");
    return false;
  }
  QByteArray abandonedConnection = encoded.first(encoded.size() / 2);
  Frame output;
  if (FrameCodec::tryDecode(abandonedConnection, output).status != FrameDecodeStatus::NeedMoreData) {
    diagnostic = QStringLiteral("partial in-flight frame was unexpectedly accepted");
    return false;
  }
  abandonedConnection.clear();
  return true;
}

std::optional<FileControlMessage> decodeFileMessage(const Frame &frame, QString &diagnostic)
{
  const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
  if (!decoded.ok()) {
    diagnostic = decoded.diagnostic;
    return std::nullopt;
  }
  return decoded.message;
}

FileReceiveRequest receiveRequest(
    const QString &receiveRoot, const ManifestEntry &entry, const FileBeginMessage &begin,
    const QByteArray &manifestSha256
)
{
  return {
      .receiveRoot = receiveRoot,
      .entry = entry,
      .begin = begin,
      .manifestSha256 = manifestSha256,
  };
}

ResumeState resumeState(
    const TransferId &transferId, const QByteArray &manifestSha256, const FileReceiverSnapshot &snapshot
)
{
  return {
      .transferId = transferId,
      .peerDeviceId = kPeerId,
      .manifestSha256 = manifestSha256,
      .direction = ResumeDirection::Receiving,
      .files =
          {
              {
                  .fileId = snapshot.fileId.value(),
                  .relativeProtocolPath = snapshot.relativeProtocolPath,
                  .durableOffset = 0,
                  .totalBytes = snapshot.expectedSize,
                  .partRelativePath = snapshot.partRelativePath,
              },
          },
      .updatedUtc = kCreatedUtc,
  };
}

TransferSnapshot controlSnapshot(const TransferId &transferId, quint64 totalBytes)
{
  return {
      .id = transferId,
      .peerId = kPeerId,
      .peerDisplayName = QStringLiteral("Loopback peer"),
      .displayName = QStringLiteral("TEST-004"),
      .direction = TransferDirection::Receiving,
      .state = TransferState::Preparing,
      .progress = {.totalBytes = totalBytes, .totalFiles = 1},
      .createdUtc = kCreatedUtc,
  };
}

bool advanceToTransferring(TransferControlStateMachine &control, QString &diagnostic)
{
  const QList<TransferState> states{
      TransferState::Offered,
      TransferState::WaitingForAcceptance,
      TransferState::Queued,
      TransferState::Transferring,
  };
  auto result = control.initialize();
  if (!result.ok()) {
    diagnostic = result.diagnostic;
    return false;
  }
  for (const auto state : states) {
    result = control.advance(state);
    if (!result.ok()) {
      diagnostic = result.diagnostic;
      return false;
    }
  }
  return true;
}

struct RestartScenarioResult
{
  bool ok = false;
  QString diagnostic;
  QString committedPath;
  quint64 durableOffset = 0;
  bool partialFrameDiscarded = false;
  TransferState finalControlState = TransferState::Failed;
};

RestartScenarioResult runRestartScenario(
    const PreparedManifestEntry &source, const TransferId &transferId, const QByteArray &manifestSha256,
    const QString &receiveRoot, quint64 streamId, quint64 checkpointAfterChunks
)
{
  RestartScenarioResult result;
  ResumeStore store(QDir(receiveRoot).filePath(QStringLiteral("resume/active")));
  TransferControlStateMachine control(controlSnapshot(transferId, source.entry.size), [] {
    return QDateTime::fromMSecsSinceEpoch(1'800'000'100'000LL, Qt::UTC);
  });
  if (!advanceToTransferring(control, result.diagnostic)) {
    return result;
  }

  std::optional<ResumeState> checkpointState;
  {
    TransferSender initialSender(senderRequest(source, transferId, streamId));
    FileReceiver initialReceiver;
    Frame frame;
    if (!nextWireFrame(initialSender, frame, result.diagnostic) || frame.type != MessageType::FileBegin) {
      result.diagnostic = result.diagnostic.isEmpty() ? QStringLiteral("initial FILE_BEGIN missing") : result.diagnostic;
      return result;
    }
    const auto beginMessage = decodeFileMessage(frame, result.diagnostic);
    if (!beginMessage || !std::holds_alternative<FileBeginMessage>(*beginMessage)) {
      result.diagnostic = QStringLiteral("initial FILE_BEGIN metadata is invalid");
      return result;
    }
    auto receiverResult = initialReceiver.begin(receiveRequest(
        receiveRoot, source.entry, std::get<FileBeginMessage>(*beginMessage), manifestSha256
    ));
    if (!receiverResult.ok()) {
      result.diagnostic = receiverResult.diagnostic;
      return result;
    }

    quint64 deliveredChunks = 0;
    while (deliveredChunks < checkpointAfterChunks) {
      if (!nextWireFrame(initialSender, frame, result.diagnostic) || frame.type != MessageType::FileChunk) {
        result.diagnostic = result.diagnostic.isEmpty() ? QStringLiteral("checkpoint chunk missing") : result.diagnostic;
        return result;
      }
      const auto chunkMessage = decodeFileMessage(frame, result.diagnostic);
      if (!chunkMessage || !std::holds_alternative<FileChunkMessage>(*chunkMessage)) {
        result.diagnostic = QStringLiteral("checkpoint FILE_CHUNK metadata is invalid");
        return result;
      }
      receiverResult = initialReceiver.append(std::get<FileChunkMessage>(*chunkMessage), frame.payload);
      if (!receiverResult.ok()) {
        result.diagnostic = receiverResult.diagnostic;
        return result;
      }
      ++deliveredChunks;
    }

    checkpointState = resumeState(transferId, manifestSha256, initialReceiver.snapshot());
    const auto checkpoint = initialReceiver.checkpoint(
        store, *checkpointState, QDateTime::fromMSecsSinceEpoch(1'800'000'010'000LL, Qt::UTC)
    );
    if (!checkpoint.ok()) {
      result.diagnostic = checkpoint.diagnostic;
      return result;
    }
    result.durableOffset = checkpoint.message->durableOffset;
    if (!control.updateProgress(result.durableOffset, 0, 0.0).ok()) {
      result.diagnostic = QStringLiteral("control rejected durable progress");
      return result;
    }

    if (!nextWireFrame(initialSender, frame, result.diagnostic) || !discardPartialWireFrame(frame, result.diagnostic)) {
      return result;
    }
    result.partialFrameDiscarded = true;
    const auto interrupted = control.interrupt();
    if (!interrupted.ok() || control.snapshot().state != TransferState::Interrupted) {
      result.diagnostic = interrupted.diagnostic;
      return result;
    }
  }

  const ResumeQueryMessage query{transferId, manifestSha256};
  const auto response = ResumeNegotiator::buildResponse(store, query);
  if (!response.ok()) {
    result.diagnostic = response.diagnostic;
    return result;
  }
  const auto plan = ResumeNegotiator::validateResponse(query, *response.response, {source.entry});
  if (!plan.ok() || plan.plan->files.size() != 1) {
    result.diagnostic = plan.diagnostic.isEmpty() ? QStringLiteral("resume plan is invalid") : plan.diagnostic;
    return result;
  }
  const quint64 startOffset = plan.plan->files.constFirst().durableOffset;
  if (startOffset != result.durableOffset) {
    result.diagnostic = QStringLiteral("negotiated offset differs from durable offset");
    return result;
  }
  const auto loaded = store.load(transferId);
  if (!loaded.ok()) {
    result.diagnostic = loaded.diagnostic;
    return result;
  }

  auto controlResult = control.resume();
  if (!controlResult.ok()) {
    result.diagnostic = controlResult.diagnostic;
    return result;
  }
  controlResult = control.advance(TransferState::Transferring);
  if (!controlResult.ok()) {
    result.diagnostic = controlResult.diagnostic;
    return result;
  }

  TransferSender restartedSender(senderRequest(source, transferId, streamId + 1, startOffset));
  FileReceiver restartedReceiver;
  Frame frame;
  if (!nextWireFrame(restartedSender, frame, result.diagnostic) || frame.type != MessageType::FileBegin) {
    result.diagnostic = result.diagnostic.isEmpty() ? QStringLiteral("resumed FILE_BEGIN missing") : result.diagnostic;
    return result;
  }
  const auto beginMessage = decodeFileMessage(frame, result.diagnostic);
  if (!beginMessage || !std::holds_alternative<FileBeginMessage>(*beginMessage)) {
    result.diagnostic = QStringLiteral("resumed FILE_BEGIN metadata is invalid");
    return result;
  }
  auto receiverResult = restartedReceiver.resume(
      receiveRequest(receiveRoot, source.entry, std::get<FileBeginMessage>(*beginMessage), manifestSha256),
      *loaded.state
  );
  if (!receiverResult.ok()) {
    result.diagnostic = receiverResult.diagnostic;
    return result;
  }

  bool completed = false;
  while (!completed) {
    if (!nextWireFrame(restartedSender, frame, result.diagnostic)) {
      return result;
    }
    const auto message = decodeFileMessage(frame, result.diagnostic);
    if (!message) {
      return result;
    }
    if (std::holds_alternative<FileChunkMessage>(*message)) {
      receiverResult = restartedReceiver.append(std::get<FileChunkMessage>(*message), frame.payload);
      if (!receiverResult.ok()) {
        result.diagnostic = receiverResult.diagnostic;
        return result;
      }
      if (!control.updateProgress(restartedReceiver.snapshot().receivedBytes, 0, 0.0).ok()) {
        result.diagnostic = QStringLiteral("control rejected resumed progress");
        return result;
      }
    } else if (std::holds_alternative<FileEndMessage>(*message)) {
      receiverResult = restartedReceiver.finish(std::get<FileEndMessage>(*message));
      if (!receiverResult.ok()) {
        result.diagnostic = receiverResult.diagnostic;
        return result;
      }
      completed = true;
    } else {
      result.diagnostic = QStringLiteral("resumed sender produced an unexpected message");
      return result;
    }
  }

  if (!control.updateProgress(source.entry.size, 1, 0.0).ok() ||
      !control.advance(TransferState::Verifying).ok() || !control.advance(TransferState::Committing).ok() ||
      !control.advance(TransferState::Completed).ok()) {
    result.diagnostic = QStringLiteral("control could not complete after resumed verification");
    return result;
  }
  result.committedPath = restartedReceiver.snapshot().committedPath;
  result.finalControlState = control.snapshot().state;
  if (readFile(result.committedPath) != readFile(source.canonicalSourcePath) ||
      sha256(readFile(result.committedPath)) != source.entry.sha256) {
    result.diagnostic = QStringLiteral("resumed target content or SHA-256 differs from source manifest");
    return result;
  }
  result.ok = true;
  return result;
}

} // namespace

class TransferRecoveryMatrixTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void networkLossAndProcessRestart_data();
  void networkLossAndProcessRestart();
  void pauseResumeStopsAndRestartsProduction();
  void cancelPolicy_data();
  void cancelPolicy();
  void unicodeFolderAndMultipleFilesRecover();
};

void TransferRecoveryMatrixTests::networkLossAndProcessRestart_data()
{
  QTest::addColumn<QByteArray>("contents");
  QTest::addColumn<quint64>("checkpointChunks");
  QTest::addColumn<quint64>("expectedOffset");
  QTest::newRow("zero-byte-before-end") << QByteArray{} << quint64{0} << quint64{0};
  QTest::newRow("disconnect-at-ten-percent") << QByteArray(640, '\x31') << quint64{1} << quint64{64};
  QTest::newRow("disconnect-at-ninety-percent") << QByteArray(640, '\x32') << quint64{9} << quint64{576};
}

void TransferRecoveryMatrixTests::networkLossAndProcessRestart()
{
  QFETCH(QByteArray, contents);
  QFETCH(quint64, checkpointChunks);
  QFETCH(quint64, expectedOffset);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString sourcePath = temporary.filePath(QStringLiteral("source/网络恢复 😀.bin"));
  QVERIFY(writeFile(sourcePath, contents));
  const TransferId transferId = TransferId::generate();
  const FileId fileId = FileId::generate();
  const auto manifest = ManifestBuilder::buildSingleFile({
      .sourcePath = sourcePath,
      .relativeProtocolPath = QStringLiteral("网络/恢复 😀.bin"),
      .transferId = transferId,
      .fileId = fileId,
  });
  QVERIFY2(manifest.ok(), qPrintable(manifest.diagnostic));
  const PreparedManifestEntry source{
      manifest.manifest->canonicalSourcePath,
      manifest.manifest->protocolCollisionKey,
      manifest.manifest->entry,
  };

  const auto result = runRestartScenario(
      source, transferId, manifest.manifest->summary.canonicalSha256,
      temporary.filePath(QStringLiteral("receive")), 11, checkpointChunks
  );

  QVERIFY2(result.ok, qPrintable(result.diagnostic));
  QCOMPARE(result.durableOffset, expectedOffset);
  QVERIFY(result.partialFrameDiscarded);
  QCOMPARE(result.finalControlState, TransferState::Completed);
  QCOMPARE(readFile(result.committedPath), contents);
}

void TransferRecoveryMatrixTests::pauseResumeStopsAndRestartsProduction()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QByteArray contents(3 * static_cast<qsizetype>(kTestChunkBytes), '\x41');
  const QString sourcePath = temporary.filePath(QStringLiteral("source/pause.bin"));
  QVERIFY(writeFile(sourcePath, contents));
  const TransferId transferId = TransferId::generate();
  const auto manifest = ManifestBuilder::buildSingleFile({
      .sourcePath = sourcePath,
      .relativeProtocolPath = QStringLiteral("暂停/继续.bin"),
      .transferId = transferId,
      .fileId = FileId::generate(),
  });
  QVERIFY2(manifest.ok(), qPrintable(manifest.diagnostic));
  const PreparedManifestEntry source{
      manifest.manifest->canonicalSourcePath,
      manifest.manifest->protocolCollisionKey,
      manifest.manifest->entry,
  };
  TransferSender sender(senderRequest(source, transferId, 21));
  FileReceiver receiver;
  TransferControlStateMachine control(controlSnapshot(transferId, source.entry.size));
  QString diagnostic;
  QVERIFY2(advanceToTransferring(control, diagnostic), qPrintable(diagnostic));

  Frame frame;
  QVERIFY2(nextWireFrame(sender, frame, diagnostic), qPrintable(diagnostic));
  const auto begin = decodeFileMessage(frame, diagnostic);
  QVERIFY(begin && std::holds_alternative<FileBeginMessage>(*begin));
  auto received = receiver.begin(receiveRequest(
      temporary.filePath(QStringLiteral("receive")), source.entry, std::get<FileBeginMessage>(*begin),
      manifest.manifest->summary.canonicalSha256
  ));
  QVERIFY2(received.ok(), qPrintable(received.diagnostic));
  QVERIFY2(nextWireFrame(sender, frame, diagnostic), qPrintable(diagnostic));
  const auto firstChunk = decodeFileMessage(frame, diagnostic);
  QVERIFY(firstChunk && std::holds_alternative<FileChunkMessage>(*firstChunk));
  QVERIFY(receiver.append(std::get<FileChunkMessage>(*firstChunk), frame.payload).ok());
  QVERIFY(control.updateProgress(receiver.snapshot().receivedBytes, 0, 0.0).ok());

  QVERIFY(control.pause().changed);
  const quint64 producedWhilePausing = sender.bytesProduced();
  const quint64 receivedWhilePausing = receiver.snapshot().receivedBytes;
  for (int schedulerTick = 0; schedulerTick < 100; ++schedulerTick) {
    const bool mayPump = control.snapshot().state == TransferState::Transferring ||
                         control.snapshot().state == TransferState::Resuming;
    QVERIFY(!mayPump);
  }
  QCOMPARE(sender.bytesProduced(), producedWhilePausing);
  QCOMPARE(receiver.snapshot().receivedBytes, receivedWhilePausing);

  QVERIFY(control.resume().changed);
  QVERIFY(control.advance(TransferState::Transferring).changed);
  bool completed = false;
  while (!completed) {
    QVERIFY2(nextWireFrame(sender, frame, diagnostic), qPrintable(diagnostic));
    const auto message = decodeFileMessage(frame, diagnostic);
    QVERIFY2(message.has_value(), qPrintable(diagnostic));
    if (std::holds_alternative<FileChunkMessage>(*message)) {
      received = receiver.append(std::get<FileChunkMessage>(*message), frame.payload);
      QVERIFY2(received.ok(), qPrintable(received.diagnostic));
    } else {
      QVERIFY(std::holds_alternative<FileEndMessage>(*message));
      received = receiver.finish(std::get<FileEndMessage>(*message));
      QVERIFY2(received.ok(), qPrintable(received.diagnostic));
      completed = true;
    }
  }
  QCOMPARE(readFile(receiver.snapshot().committedPath), contents);
}

void TransferRecoveryMatrixTests::cancelPolicy_data()
{
  QTest::addColumn<PartialDisposition>("disposition");
  QTest::newRow("cancel-delete-partial") << PartialDisposition::Remove;
  QTest::newRow("cancel-keep-partial") << PartialDisposition::Keep;
}

void TransferRecoveryMatrixTests::cancelPolicy()
{
  QFETCH(PartialDisposition, disposition);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QByteArray contents(2 * static_cast<qsizetype>(kTestChunkBytes), '\x55');
  const QString sourcePath = temporary.filePath(QStringLiteral("source/cancel.bin"));
  QVERIFY(writeFile(sourcePath, contents));
  const TransferId transferId = TransferId::generate();
  const auto manifest = ManifestBuilder::buildSingleFile({
      .sourcePath = sourcePath,
      .relativeProtocolPath = QStringLiteral("取消/partial.bin"),
      .transferId = transferId,
      .fileId = FileId::generate(),
  });
  QVERIFY2(manifest.ok(), qPrintable(manifest.diagnostic));
  const PreparedManifestEntry source{
      manifest.manifest->canonicalSourcePath,
      manifest.manifest->protocolCollisionKey,
      manifest.manifest->entry,
  };
  TransferSender sender(senderRequest(source, transferId, 31));
  FileReceiver receiver;
  const QString receiveRoot = temporary.filePath(QStringLiteral("receive"));
  ResumeStore store(temporary.filePath(QStringLiteral("resume/active")));
  TransferControlStateMachine control(controlSnapshot(transferId, source.entry.size));
  QString diagnostic;
  QVERIFY2(advanceToTransferring(control, diagnostic), qPrintable(diagnostic));

  Frame frame;
  QVERIFY2(nextWireFrame(sender, frame, diagnostic), qPrintable(diagnostic));
  const auto begin = decodeFileMessage(frame, diagnostic);
  QVERIFY(begin && std::holds_alternative<FileBeginMessage>(*begin));
  QVERIFY(receiver.begin(receiveRequest(
      receiveRoot, source.entry, std::get<FileBeginMessage>(*begin), manifest.manifest->summary.canonicalSha256
  )).ok());
  QVERIFY2(nextWireFrame(sender, frame, diagnostic), qPrintable(diagnostic));
  const auto chunk = decodeFileMessage(frame, diagnostic);
  QVERIFY(chunk && std::holds_alternative<FileChunkMessage>(*chunk));
  QVERIFY(receiver.append(std::get<FileChunkMessage>(*chunk), frame.payload).ok());
  auto state = resumeState(transferId, manifest.manifest->summary.canonicalSha256, receiver.snapshot());
  QVERIFY(receiver.checkpoint(store, state, kCreatedUtc.addSecs(1)).ok());
  const QString partPath = receiver.snapshot().partPath;
  QVERIFY(QFileInfo::exists(partPath));

  QVERIFY(control.cancel().changed);
  QVERIFY(receiver.cancel(disposition).ok());
  QVERIFY(control.confirmCancelled().changed);
  const bool keepPartial = disposition == PartialDisposition::Keep;
  if (!keepPartial) {
    QVERIFY(store.remove(transferId).ok());
  }
  QCOMPARE(QFileInfo::exists(partPath), keepPartial);
  QCOMPARE(store.load(transferId).ok(), keepPartial);
  QCOMPARE(control.snapshot().state, TransferState::Cancelled);
  QCOMPARE(receiver.snapshot().state, FileReceiverState::Cancelled);
  QVERIFY(!control.cancel().changed);
  QVERIFY(receiver.cancel(disposition).ok());
}

void TransferRecoveryMatrixTests::unicodeFolderAndMultipleFilesRecover()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString sourceRoot = temporary.filePath(QStringLiteral("source/发送根"));
  QVERIFY(QDir().mkpath(QDir(sourceRoot).filePath(QStringLiteral("空目录"))));
  const QByteArray unicodeContents(2 * static_cast<qsizetype>(kTestChunkBytes) + 7, '\x6a');
  QVERIFY(writeFile(QDir(sourceRoot).filePath(QStringLiteral("资料/报告 😀.txt")), unicodeContents));
  QVERIFY(writeFile(QDir(sourceRoot).filePath(QStringLiteral("零字节.dat")), {}));
  const TransferId transferId = TransferId::generate();
  const auto manifest = ManifestBuilder::buildTransfer({
      .sources = {{sourceRoot, QStringLiteral("共享 📁")}},
      .transferId = transferId,
  });
  QVERIFY2(manifest.ok(), qPrintable(manifest.diagnostic));
  QCOMPARE(manifest.manifest->summary.fileCount, quint64{2});
  QVERIFY(manifest.manifest->summary.directoryCount >= 3);

  const QString receiveRoot = temporary.filePath(QStringLiteral("receive"));
  quint64 streamId = 100;
  bool sawEmptyDirectory = false;
  bool sawZeroByte = false;
  for (const auto &entry : manifest.manifest->entries) {
    if (entry.entry.type == ManifestEntryType::Directory) {
      QString target;
      const auto joined = PathPolicy::joinLexicallyUnderRoot(receiveRoot, entry.entry.relativeProtocolPath, target);
      QVERIFY2(joined.ok, qPrintable(joined.diagnostic));
      QVERIFY(QDir().mkpath(target));
      sawEmptyDirectory = sawEmptyDirectory || entry.entry.relativeProtocolPath.endsWith(QStringLiteral("空目录"));
      continue;
    }
    sawZeroByte = sawZeroByte || entry.entry.size == 0;
    const quint64 checkpointChunks = entry.entry.size == 0 ? 0 : 1;
    const auto result = runRestartScenario(
        entry, transferId, manifest.manifest->summary.canonicalSha256, receiveRoot, streamId, checkpointChunks
    );
    QVERIFY2(result.ok, qPrintable(result.diagnostic));
    QCOMPARE(readFile(result.committedPath), readFile(entry.canonicalSourcePath));
    streamId += 2;
  }
  QVERIFY(sawEmptyDirectory);
  QVERIFY(sawZeroByte);
  QVERIFY(QFileInfo(QDir(receiveRoot).filePath(QStringLiteral("共享 📁/空目录"))).isDir());
  QCOMPARE(readFile(QDir(receiveRoot).filePath(QStringLiteral("共享 📁/资料/报告 😀.txt"))), unicodeContents);
  QVERIFY(QFileInfo::exists(QDir(receiveRoot).filePath(QStringLiteral("共享 📁/零字节.dat"))));
}

QTEST_GUILESS_MAIN(TransferRecoveryMatrixTests)

#include "TransferRecoveryMatrixTests.moc"
