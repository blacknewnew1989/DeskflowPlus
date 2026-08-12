// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSender.h"

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestBuilder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>
#include <utility>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId(QStringLiteral("01234567-89ab-cdef-8123-456789abcdef"));
const FileId kFileId(QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"));

bool writeFile(const QString &path, QByteArrayView data)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data.data(), data.size()) == data.size();
}

SingleFileManifest buildManifest(const QString &path, const QString &logicalPath = QStringLiteral("数据/片段.bin"))
{
  const auto result = ManifestBuilder::buildSingleFile({
      .sourcePath = path,
      .relativeProtocolPath = logicalPath,
      .transferId = kTransferId,
      .fileId = kFileId,
  });
  if (!result.ok()) {
    return {};
  }
  return *result.manifest;
}

TransferSenderRequest requestFor(const SingleFileManifest &manifest, quint32 chunkBytes)
{
  return {
      .transferId = manifest.summary.id,
      .source =
          {
              .canonicalSourcePath = manifest.canonicalSourcePath,
              .protocolCollisionKey = manifest.protocolCollisionKey,
              .entry = manifest.entry,
          },
      .streamId = 7,
      .chunkBytes = chunkBytes,
  };
}

quint64 encodedFrameBytes(const Frame &frame)
{
  return static_cast<quint64>(kFixedHeaderBytes) + static_cast<quint64>(frame.metadata.size()) +
         static_cast<quint64>(frame.payload.size());
}

class BoundedFrameSink final : public TransferFrameSink
{
public:
  explicit BoundedFrameSink(quint64 limit) : m_limit(limit)
  {
  }

  [[nodiscard]] quint64 queuedBytes() const noexcept override
  {
    return m_queued;
  }

  [[nodiscard]] SenderFrameSinkResult submit(const Frame &frame) override
  {
    ++submitAttempts;
    const quint64 bytes = encodedFrameBytes(frame);
    if (forceBackpressure || bytes > m_limit || m_queued > m_limit - bytes) {
      return {
          .status = SenderFrameSinkStatus::Backpressured,
          .diagnostic = QStringLiteral("test sink is full"),
      };
    }
    accepted.append(frame);
    m_queued += bytes;
    peakQueued = std::max(peakQueued, m_queued);
    return {.status = SenderFrameSinkStatus::Accepted};
  }

  void drainTo(quint64 bytes)
  {
    m_queued = std::min(m_queued, bytes);
  }

  QList<Frame> accepted;
  quint64 peakQueued = 0;
  quint64 submitAttempts = 0;
  bool forceBackpressure = false;

private:
  quint64 m_limit = 0;
  quint64 m_queued = 0;
};

QList<Frame> produceAll(TransferSender &sender, SenderFrameResult &last)
{
  QList<Frame> frames;
  while (!sender.finished()) {
    last = sender.nextFrame();
    if (!last.ready()) {
      return frames;
    }
    frames.append(std::move(*last.frame));
  }
  return frames;
}

} // namespace

class TransferSenderTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void streamsZeroByteUnicodeAndMultipleChunks_data();
  void streamsZeroByteUnicodeAndMultipleChunks();
  void supportsLargeLogicalFileSizeAtBegin();
  void rejectsShortReadAsSourceChanged();
  void rejectsSameSizeSourceMutation();
  void rejectsUnrepresentableLogicalSize();
  void backpressureBoundsMemoryAndPreservesOrdering();
  void detectsSourceMutationAfterBackpressure();
};

void TransferSenderTests::streamsZeroByteUnicodeAndMultipleChunks_data()
{
  QTest::addColumn<QByteArray>("contents");
  QTest::addColumn<quint32>("chunkBytes");
  QTest::newRow("zero-byte") << QByteArray{} << quint32(4);
  QTest::newRow("multiple-chunks") << QByteArrayLiteral("abcdefghijklmnopq") << quint32(5);
}

void TransferSenderTests::streamsZeroByteUnicodeAndMultipleChunks()
{
  QFETCH(QByteArray, contents);
  QFETCH(quint32, chunkBytes);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("源 😀.bin"));
  QVERIFY(writeFile(path, contents));
  const SingleFileManifest manifest = buildManifest(path);
  QVERIFY(!manifest.canonicalSourcePath.isEmpty());
  TransferSender sender(requestFor(manifest, chunkBytes));
  SenderFrameResult last;

  const QList<Frame> frames = produceAll(sender, last);

  QVERIFY2(last.ready(), qPrintable(last.diagnostic));
  const qsizetype expectedChunks = contents.isEmpty() ? 0 : (contents.size() + chunkBytes - 1) / chunkBytes;
  QCOMPARE(frames.size(), expectedChunks + 2);
  QCOMPARE(frames.first().type, MessageType::FileBegin);
  const auto beginResult = FileMessageCodec::decode(frames.first().type, frames.first().metadata);
  QVERIFY2(beginResult.ok(), qPrintable(beginResult.diagnostic));
  const auto &begin = std::get<FileBeginMessage>(*beginResult.message);
  QCOMPARE(begin.transferId, kTransferId);
  QCOMPARE(begin.fileId, kFileId);
  QCOMPARE(begin.size, static_cast<quint64>(contents.size()));
  QCOMPARE(begin.startOffset, 0);
  QCOMPARE(begin.chunkBytes, chunkBytes);

  QByteArray reconstructed;
  for (qsizetype index = 0; index < expectedChunks; ++index) {
    const Frame &frame = frames.at(index + 1);
    QCOMPARE(frame.type, MessageType::FileChunk);
    QCOMPARE(frame.streamId, 7);
    const auto chunkResult = FileMessageCodec::decode(frame.type, frame.metadata);
    QVERIFY2(chunkResult.ok(), qPrintable(chunkResult.diagnostic));
    const auto &chunk = std::get<FileChunkMessage>(*chunkResult.message);
    QCOMPARE(chunk.offset, static_cast<quint64>(reconstructed.size()));
    QCOMPARE(chunk.sequence, static_cast<quint64>(index));
    QVERIFY(static_cast<quint32>(frame.payload.size()) <= chunkBytes);
    reconstructed.append(frame.payload);
  }
  QCOMPARE(reconstructed, contents);

  const Frame &endFrame = frames.last();
  QCOMPARE(endFrame.type, MessageType::FileEnd);
  const auto endResult = FileMessageCodec::decode(endFrame.type, endFrame.metadata);
  QVERIFY2(endResult.ok(), qPrintable(endResult.diagnostic));
  const auto &end = std::get<FileEndMessage>(*endResult.message);
  QCOMPARE(end.size, static_cast<quint64>(contents.size()));
  QCOMPARE(end.sha256, manifest.entry.sha256);
  QCOMPARE(sender.bytesProduced(), static_cast<quint64>(contents.size()));
  QCOMPARE(sender.nextSequence(), static_cast<quint64>(expectedChunks));
  QVERIFY(sender.finished());
}

void TransferSenderTests::supportsLargeLogicalFileSizeAtBegin()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("sparse-large.bin"));
  constexpr qint64 logicalSize = (qint64{4} * 1024 * 1024 * 1024) + 17;
  QFile sparse(path);
  QVERIFY(sparse.open(QIODevice::WriteOnly));
  QVERIFY(sparse.resize(logicalSize));
  sparse.close();
  QFileInfo info(path);
  info.refresh();
  PreparedManifestEntry source{
      .canonicalSourcePath = info.canonicalFilePath(),
      .protocolCollisionKey = QStringLiteral("sparse-large.bin"),
      .entry = {
          .id = kFileId,
          .relativeProtocolPath = QStringLiteral("sparse-large.bin"),
          .type = ManifestEntryType::File,
          .size = static_cast<quint64>(logicalSize),
          .modifiedUtc = info.lastModified(),
          .sha256 = QByteArray(32, '\x2a'),
      },
  };
  TransferSender sender({.transferId = kTransferId, .source = source, .streamId = 9});

  const auto result = sender.nextFrame();

  QVERIFY2(result.ready(), qPrintable(result.diagnostic));
  const auto beginResult = FileMessageCodec::decode(result.frame->type, result.frame->metadata);
  QVERIFY(beginResult.ok());
  const auto &begin = std::get<FileBeginMessage>(*beginResult.message);
  QCOMPARE(begin.size, static_cast<quint64>(logicalSize));
  QCOMPARE(result.frame->payload.size(), 0);
}

void TransferSenderTests::rejectsShortReadAsSourceChanged()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("short.bin"));
  QVERIFY(writeFile(path, QByteArray(32, '\x11')));
  const SingleFileManifest manifest = buildManifest(path, QStringLiteral("short.bin"));
  TransferSender sender(requestFor(manifest, 16));
  QVERIFY(sender.nextFrame().ready());

  QFile truncate(path);
  QVERIFY(truncate.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QVERIFY(truncate.write(QByteArray(3, '\x11')) == 3);
  truncate.close();
  const auto result = sender.nextFrame();

  QCOMPARE(result.error, TransferSenderError::SourceChanged);
  QCOMPARE(result.status, SenderFrameStatus::Failed);
}

void TransferSenderTests::rejectsSameSizeSourceMutation()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("mutated.bin"));
  QVERIFY(writeFile(path, QByteArrayLiteral("original")));
  const SingleFileManifest manifest = buildManifest(path, QStringLiteral("mutated.bin"));
  TransferSender sender(requestFor(manifest, 64));
  QVERIFY(sender.nextFrame().ready());
  QVERIFY(sender.nextFrame().ready());
  QVERIFY(writeFile(path, QByteArrayLiteral("MUTATION")));

  const auto result = sender.nextFrame();

  QCOMPARE(result.error, TransferSenderError::SourceChanged);
  QCOMPARE(result.status, SenderFrameStatus::Failed);
}

void TransferSenderTests::rejectsUnrepresentableLogicalSize()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("small.bin"));
  QVERIFY(writeFile(path, QByteArrayView{}));
  QFileInfo info(path);
  PreparedManifestEntry source{
      .canonicalSourcePath = info.canonicalFilePath(),
      .entry = {
          .id = kFileId,
          .relativeProtocolPath = QStringLiteral("small.bin"),
          .size = static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1,
          .modifiedUtc = info.lastModified(),
          .sha256 = QByteArray(32, '\0'),
      },
  };
  TransferSender sender({.transferId = kTransferId, .source = source, .streamId = 1});

  QCOMPARE(sender.nextFrame().error, TransferSenderError::InvalidRequest);
}

void TransferSenderTests::backpressureBoundsMemoryAndPreservesOrdering()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("bounded.bin"));
  QByteArray contents(4096, '\0');
  for (qsizetype index = 0; index < contents.size(); ++index) {
    contents[index] = static_cast<char>(index % 251);
  }
  QVERIFY(writeFile(path, contents));
  const SingleFileManifest manifest = buildManifest(path, QStringLiteral("bounded.bin"));
  constexpr quint64 highWater = 700;
  constexpr quint64 lowWater = 200;
  BoundedFrameSink sink(highWater);
  TransferSenderPump pump(requestFor(manifest, 256), sink, {highWater, lowWater});

  SenderPumpResult result;
  for (int iteration = 0; iteration < 200 && !pump.paused(); ++iteration) {
    result = pump.pump();
    QVERIFY(result.status == SenderPumpStatus::Progressed || result.status == SenderPumpStatus::Backpressured);
  }
  QCOMPARE(result.status, SenderPumpStatus::Backpressured);
  QVERIFY(pump.paused());
  QVERIFY(sink.peakQueued <= highWater);
  QVERIFY(pump.bufferedFrameBytes() <= 256 + 512);
  const quint64 producedAtPause = pump.bytesProduced();
  const quint64 attemptsAtPause = sink.submitAttempts;

  QCOMPARE(pump.pump().status, SenderPumpStatus::Backpressured);
  QCOMPARE(pump.bytesProduced(), producedAtPause);
  QCOMPARE(sink.submitAttempts, attemptsAtPause);
  sink.drainTo(lowWater);
  const auto resumed = pump.pump();
  QCOMPARE(resumed.status, SenderPumpStatus::Progressed);
  QCOMPARE(pump.bytesProduced(), producedAtPause);

  for (int iteration = 0; iteration < 1000 && !pump.finished(); ++iteration) {
    result = pump.pump();
    if (result.status == SenderPumpStatus::Backpressured) {
      sink.drainTo(0);
    } else {
      QVERIFY(result.status == SenderPumpStatus::Progressed || result.status == SenderPumpStatus::Finished);
    }
  }
  QVERIFY(pump.finished());
  QVERIFY(sink.peakQueued <= highWater);
  QCOMPARE(pump.bufferedFrameBytes(), 0);

  QByteArray reconstructed;
  quint64 expectedOffset = 0;
  quint64 expectedSequence = 0;
  for (const Frame &frame : std::as_const(sink.accepted)) {
    if (frame.type != MessageType::FileChunk) {
      continue;
    }
    const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    const auto &chunk = std::get<FileChunkMessage>(*decoded.message);
    QCOMPARE(chunk.offset, expectedOffset);
    QCOMPARE(chunk.sequence, expectedSequence);
    expectedOffset += static_cast<quint64>(frame.payload.size());
    ++expectedSequence;
    reconstructed.append(frame.payload);
  }
  QCOMPARE(reconstructed, contents);
  QCOMPARE(sink.accepted.first().type, MessageType::FileBegin);
  QCOMPARE(sink.accepted.last().type, MessageType::FileEnd);
}

void TransferSenderTests::detectsSourceMutationAfterBackpressure()
{
  QTemporaryDir directory;
  const QString path = directory.filePath(QStringLiteral("paused-mutation.bin"));
  QVERIFY(writeFile(path, QByteArray(1024, '\x41')));
  const SingleFileManifest manifest = buildManifest(path, QStringLiteral("paused-mutation.bin"));
  BoundedFrameSink sink(4096);
  TransferSenderPump pump(requestFor(manifest, 256), sink, {4096, 512});
  QCOMPARE(pump.pump().status, SenderPumpStatus::Progressed);
  sink.forceBackpressure = true;
  QCOMPARE(pump.pump().status, SenderPumpStatus::Backpressured);
  QCOMPARE(pump.bytesProduced(), 256);
  QVERIFY(pump.bufferedFrameBytes() > 0);
  QVERIFY(writeFile(path, QByteArray(1024, '\x42')));

  sink.forceBackpressure = false;
  sink.drainTo(0);
  SenderPumpResult result;
  for (int iteration = 0; iteration < 20; ++iteration) {
    result = pump.pump();
    if (result.status == SenderPumpStatus::Failed) {
      break;
    }
    if (result.status == SenderPumpStatus::Backpressured) {
      sink.drainTo(0);
    }
  }
  QCOMPARE(result.status, SenderPumpStatus::Failed);
  QCOMPARE(result.senderError, TransferSenderError::SourceChanged);
}

QTEST_MAIN(TransferSenderTests)

#include "TransferSenderTests.moc"
