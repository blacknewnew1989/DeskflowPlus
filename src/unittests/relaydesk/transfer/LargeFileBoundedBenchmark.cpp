// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSender.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace relaydesk::transfer;

namespace {

constexpr quint64 kLogicalBytes = 10ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr quint64 kChunkBytes = 1ULL * 1024ULL * 1024ULL;
constexpr quint64 kHighWaterBytes = 4ULL * 1024ULL * 1024ULL;
constexpr quint64 kLowWaterBytes = 2ULL * 1024ULL * 1024ULL;

quint64 encodedBytes(const Frame &frame)
{
  return static_cast<quint64>(kFixedHeaderBytes) + static_cast<quint64>(frame.metadata.size()) +
         static_cast<quint64>(frame.payload.size());
}

class MeasuringBoundedSink final : public TransferFrameSink
{
public:
  [[nodiscard]] quint64 queuedBytes() const noexcept override
  {
    return queued;
  }

  [[nodiscard]] SenderFrameSinkResult submit(const Frame &frame) override
  {
    const quint64 frameBytes = encodedBytes(frame);
    largestFrame = std::max(largestFrame, frameBytes);
    if (frameBytes > kHighWaterBytes || queued > kHighWaterBytes - frameBytes) {
      return {.status = SenderFrameSinkStatus::Backpressured};
    }
    queued += frameBytes;
    peakQueued = std::max(peakQueued, queued);
    ++acceptedFrames;
    return {.status = SenderFrameSinkStatus::Accepted};
  }

  quint64 queued = 0;
  quint64 peakQueued = 0;
  quint64 largestFrame = 0;
  quint64 acceptedFrames = 0;
};

} // namespace

class LargeFileBoundedBenchmark final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void tenGiBLogicalSourceKeepsOwnedBuffersBounded();
};

void LargeFileBoundedBenchmark::tenGiBLogicalSourceKeepsOwnedBuffersBounded()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString sourcePath = directory.filePath(QStringLiteral("10gib-sparse.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QVERIFY(source.resize(static_cast<qint64>(kLogicalBytes)));
  source.close();
  QFileInfo info(sourcePath);
  info.refresh();
  QCOMPARE(static_cast<quint64>(info.size()), kLogicalBytes);

  const TransferId transferId = QUuid::createUuid();
  const FileId fileId = QUuid::createUuid();
  MeasuringBoundedSink sink;
  TransferSenderPump pump(
      {
          .transferId = transferId,
          .source =
              {
                  .canonicalSourcePath = info.canonicalFilePath(),
                  .protocolCollisionKey = QStringLiteral("10gib-sparse.bin"),
                  .entry =
                      {
                          .id = fileId,
                          .relativeProtocolPath = QStringLiteral("10gib-sparse.bin"),
                          .type = ManifestEntryType::File,
                          .size = kLogicalBytes,
                          .modifiedUtc = info.lastModified(),
                          // This benchmark intentionally stops at high water;
                          // it does not claim a full-file integrity pass.
                          .sha256 = QByteArray(kSha256Bytes, '\0'),
                      },
              },
          .streamId = 73,
          .chunkBytes = static_cast<quint32>(kChunkBytes),
      },
      sink, {kHighWaterBytes, kLowWaterBytes}
  );

  SenderPumpResult result;
  for (int step = 0; step < 32 && !pump.paused(); ++step) {
    result = pump.pump();
    QVERIFY(result.status == SenderPumpStatus::Progressed || result.status == SenderPumpStatus::Backpressured);
  }

  QCOMPARE(result.status, SenderPumpStatus::Backpressured);
  QVERIFY(pump.paused());
  QVERIFY(pump.bytesProduced() < kLogicalBytes);
  QVERIFY(sink.peakQueued <= kHighWaterBytes);
  QVERIFY(sink.largestFrame <= kChunkBytes + 1024);
  QVERIFY(pump.bufferedFrameBytes() <= kChunkBytes + 1024);
  const quint64 peakOwnedBytes = sink.peakQueued + pump.bufferedFrameBytes();
  QVERIFY(peakOwnedBytes <= kHighWaterBytes + kChunkBytes + 1024);

  qInfo().nospace() << "TEST-002 logicalBytes=" << kLogicalBytes << " chunkBytes=" << kChunkBytes
                    << " highWaterBytes=" << kHighWaterBytes << " peakQueuedBytes=" << sink.peakQueued
                    << " pendingFrameBytes=" << pump.bufferedFrameBytes()
                    << " observedOwnedUpperBound=" << peakOwnedBytes
                    << " sourceBytesReadBeforePause=" << pump.bytesProduced();
}

QTEST_MAIN(LargeFileBoundedBenchmark)

#include "LargeFileBoundedBenchmark.moc"
