// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSender.h"

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/PathPolicy.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

SenderFrameResult failure(TransferSenderError error, QString diagnostic)
{
  return {
      .frame = std::nullopt,
      .status = SenderFrameStatus::Failed,
      .error = error,
      .diagnostic = std::move(diagnostic),
  };
}

SenderFrameResult frameReady(Frame frame)
{
  return {
      .frame = std::move(frame),
      .status = SenderFrameStatus::FrameReady,
  };
}

bool snapshotMatches(const QFileInfo &info, const ManifestEntry &entry)
{
  return info.exists() && !info.isSymLink() && info.isFile() && info.size() >= 0 &&
         static_cast<quint64>(info.size()) == entry.size &&
         info.lastModified().toMSecsSinceEpoch() == entry.modifiedUtc.toMSecsSinceEpoch();
}

} // namespace

class TransferSender::Impl final
{
public:
  explicit Impl(TransferSenderRequest input) : request(std::move(input))
  {
  }

  [[nodiscard]] SenderFrameResult nextFrame();
  [[nodiscard]] SenderFrameResult initialize();
  [[nodiscard]] SenderFrameResult produceChunk();
  [[nodiscard]] SenderFrameResult finishFile();
  [[nodiscard]] SenderFrameResult encodeFrame(MessageType type, const QByteArray &metadata, QByteArray payload = {});
  [[nodiscard]] std::optional<TransferSenderError> validate(QString &diagnostic) const;

  enum class State
  {
    Initial,
    Chunks,
    EndPending,
    Finished,
    Failed,
  };

  TransferSenderRequest request;
  QFile source;
  QCryptographicHash hash{QCryptographicHash::Sha256};
  quint64 offset = 0;
  quint64 sequence = 0;
  State state = State::Initial;
};

std::optional<TransferSenderError> TransferSender::Impl::validate(QString &diagnostic) const
{
  const ManifestEntry &entry = request.source.entry;
  if (request.transferId.isNull() || entry.id.isNull() || request.streamId == 0 || request.chunkBytes == 0 ||
      request.chunkBytes > kMaxSenderChunkBytes || request.chunkBytes > ProtocolLimits{}.maxDataPayloadBytes) {
    diagnostic = QStringLiteral("sender identifiers, stream, or chunk size are invalid");
    return TransferSenderError::InvalidRequest;
  }
  if (request.source.canonicalSourcePath.isEmpty() || entry.type != ManifestEntryType::File ||
      !entry.modifiedUtc.isValid() || entry.modifiedUtc.toMSecsSinceEpoch() < 0 ||
      entry.sha256.size() != kSha256Bytes) {
    diagnostic = QStringLiteral("sender requires a prepared regular-file manifest entry with SHA-256");
    return TransferSenderError::InvalidRequest;
  }
  const PathValidationResult path = PathPolicy::validateRelative(entry.relativeProtocolPath);
  if (!path.ok || path.normalized != entry.relativeProtocolPath) {
    diagnostic = QStringLiteral("sender manifest path is not a normalized RDFT/1 path");
    return TransferSenderError::InvalidRequest;
  }
  if (entry.size > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    diagnostic = QStringLiteral("source size exceeds QFile's supported range");
    return TransferSenderError::InvalidRequest;
  }
  return std::nullopt;
}

SenderFrameResult TransferSender::Impl::encodeFrame(MessageType type, const QByteArray &metadata, QByteArray payload)
{
  if (metadata.isEmpty()) {
    state = State::Failed;
    return failure(TransferSenderError::ProtocolError, QStringLiteral("file message metadata could not be encoded"));
  }
  return frameReady(
      Frame{
          .version = kProtocolMajorVersion,
          .type = type,
          .flags = 0,
          .streamId = request.streamId,
          .metadata = metadata,
          .payload = std::move(payload),
      }
  );
}

SenderFrameResult TransferSender::Impl::initialize()
{
  QString diagnostic;
  if (const auto error = validate(diagnostic); error.has_value()) {
    state = State::Failed;
    return failure(*error, std::move(diagnostic));
  }

  QFileInfo initialInfo(request.source.canonicalSourcePath);
  initialInfo.refresh();
  if (!snapshotMatches(initialInfo, request.source.entry)) {
    state = State::Failed;
    return failure(TransferSenderError::SourceChanged, QStringLiteral("source changed after manifest creation"));
  }
  source.setFileName(request.source.canonicalSourcePath);
  if (!source.open(QIODevice::ReadOnly)) {
    state = State::Failed;
    return failure(
        TransferSenderError::SourceOpenFailed,
        QStringLiteral("source file could not be opened: %1").arg(source.errorString())
    );
  }

  FileBeginMessage message{
      .transferId = request.transferId,
      .fileId = request.source.entry.id,
      .size = request.source.entry.size,
      .startOffset = 0,
      .chunkBytes = request.chunkBytes,
      .expectedSha256 = request.source.entry.sha256,
  };
  QString encodeError;
  const QByteArray metadata = FileMessageCodec::encode(FileControlMessage{message}, &encodeError);
  if (metadata.isEmpty()) {
    state = State::Failed;
    return failure(TransferSenderError::ProtocolError, std::move(encodeError));
  }
  state = request.source.entry.size == 0 ? State::EndPending : State::Chunks;
  return encodeFrame(MessageType::FileBegin, metadata);
}

SenderFrameResult TransferSender::Impl::produceChunk()
{
  const quint64 remaining = request.source.entry.size - offset;
  const quint64 wanted = std::min<quint64>(remaining, request.chunkBytes);
  QByteArray payload = source.read(static_cast<qint64>(wanted));
  if (payload.isNull() && source.error() != QFileDevice::NoError) {
    state = State::Failed;
    return failure(
        TransferSenderError::SourceReadFailed,
        QStringLiteral("source file could not be read: %1").arg(source.errorString())
    );
  }
  if (static_cast<quint64>(payload.size()) != wanted) {
    state = State::Failed;
    return failure(TransferSenderError::SourceChanged, QStringLiteral("source file produced a short read"));
  }

  FileChunkMessage message{
      .transferId = request.transferId,
      .fileId = request.source.entry.id,
      .offset = offset,
      .sequence = sequence,
  };
  QString encodeError;
  const QByteArray metadata = FileMessageCodec::encode(FileControlMessage{message}, &encodeError);
  if (metadata.isEmpty()) {
    state = State::Failed;
    return failure(TransferSenderError::ProtocolError, std::move(encodeError));
  }
  hash.addData(QByteArrayView(payload));
  offset += wanted;
  ++sequence;
  if (offset == request.source.entry.size) {
    state = State::EndPending;
  }
  return encodeFrame(MessageType::FileChunk, metadata, std::move(payload));
}

SenderFrameResult TransferSender::Impl::finishFile()
{
  source.close();
  QFileInfo finalInfo(request.source.canonicalSourcePath);
  finalInfo.refresh();
  const QByteArray actualSha256 = hash.result();
  if (offset != request.source.entry.size || !snapshotMatches(finalInfo, request.source.entry) ||
      actualSha256 != request.source.entry.sha256) {
    state = State::Failed;
    return failure(
        TransferSenderError::SourceChanged, QStringLiteral("source size, timestamp, or SHA-256 changed while sending")
    );
  }

  FileEndMessage message{
      .transferId = request.transferId,
      .fileId = request.source.entry.id,
      .size = offset,
      .sha256 = actualSha256,
  };
  QString encodeError;
  const QByteArray metadata = FileMessageCodec::encode(FileControlMessage{message}, &encodeError);
  if (metadata.isEmpty()) {
    state = State::Failed;
    return failure(TransferSenderError::ProtocolError, std::move(encodeError));
  }
  state = State::Finished;
  return encodeFrame(MessageType::FileEnd, metadata);
}

SenderFrameResult TransferSender::Impl::nextFrame()
{
  switch (state) {
  case State::Initial:
    return initialize();
  case State::Chunks:
    return produceChunk();
  case State::EndPending:
    return finishFile();
  case State::Finished:
    return {
        .status = SenderFrameStatus::Finished,
    };
  case State::Failed:
    return failure(TransferSenderError::AlreadyFinished, QStringLiteral("sender is in a terminal failed state"));
  }
  return failure(TransferSenderError::AlreadyFinished, QStringLiteral("sender state is invalid"));
}

TransferSender::TransferSender(TransferSenderRequest request) : m_impl(std::make_unique<Impl>(std::move(request)))
{
}

TransferSender::~TransferSender() = default;

SenderFrameResult TransferSender::nextFrame()
{
  return m_impl->nextFrame();
}

quint64 TransferSender::bytesProduced() const noexcept
{
  return m_impl->offset;
}

quint64 TransferSender::nextSequence() const noexcept
{
  return m_impl->sequence;
}

bool TransferSender::finished() const noexcept
{
  return m_impl->state == Impl::State::Finished;
}

namespace {

quint64 frameMemoryBytes(const Frame &frame) noexcept
{
  return static_cast<quint64>(kFixedHeaderBytes) + static_cast<quint64>(frame.metadata.size()) +
         static_cast<quint64>(frame.payload.size());
}

} // namespace

class TransferSenderPump::Impl final
{
public:
  Impl(TransferSenderRequest request, TransferFrameSink &output, SenderBackpressureLimits watermarks)
      : sender(std::move(request)),
        sink(output),
        limits(watermarks)
  {
  }

  [[nodiscard]] SenderPumpResult pump();

  TransferSender sender;
  TransferFrameSink &sink;
  SenderBackpressureLimits limits;
  std::optional<Frame> pendingFrame;
  bool isPaused = false;
  bool isFailed = false;
};

SenderPumpResult TransferSenderPump::Impl::pump()
{
  if (isFailed) {
    return {
        .status = SenderPumpStatus::Failed,
        .senderError = TransferSenderError::AlreadyFinished,
        .diagnostic = QStringLiteral("sender pump is in a terminal failed state"),
    };
  }
  if (sender.finished() && !pendingFrame.has_value()) {
    return {.status = SenderPumpStatus::Finished};
  }
  if (limits.highWaterBytes == 0 || limits.lowWaterBytes >= limits.highWaterBytes) {
    isFailed = true;
    return {
        .status = SenderPumpStatus::Failed,
        .senderError = TransferSenderError::InvalidRequest,
        .diagnostic = QStringLiteral("sender high/low watermarks are invalid"),
    };
  }

  const quint64 queued = sink.queuedBytes();
  if (isPaused && queued > limits.lowWaterBytes) {
    return {.status = SenderPumpStatus::Backpressured};
  }
  isPaused = false;
  if (!pendingFrame.has_value() && queued >= limits.highWaterBytes) {
    isPaused = true;
    return {.status = SenderPumpStatus::Backpressured};
  }

  if (!pendingFrame.has_value()) {
    SenderFrameResult produced = sender.nextFrame();
    if (produced.status == SenderFrameStatus::Finished) {
      return {.status = SenderPumpStatus::Finished};
    }
    if (!produced.ready()) {
      isFailed = true;
      return {
          .status = SenderPumpStatus::Failed,
          .senderError = produced.error,
          .diagnostic = std::move(produced.diagnostic),
      };
    }
    pendingFrame = std::move(*produced.frame);
  }

  SenderFrameSinkResult submitted = sink.submit(*pendingFrame);
  switch (submitted.status) {
  case SenderFrameSinkStatus::Accepted:
    pendingFrame.reset();
    if (sender.finished()) {
      return {.status = SenderPumpStatus::Finished};
    }
    if (sink.queuedBytes() >= limits.highWaterBytes) {
      isPaused = true;
      return {.status = SenderPumpStatus::Backpressured};
    }
    return {.status = SenderPumpStatus::Progressed};
  case SenderFrameSinkStatus::Backpressured:
    isPaused = true;
    return {.status = SenderPumpStatus::Backpressured, .diagnostic = std::move(submitted.diagnostic)};
  case SenderFrameSinkStatus::Failed:
    isFailed = true;
    return {
        .status = SenderPumpStatus::Failed,
        .senderError = TransferSenderError::ProtocolError,
        .diagnostic = std::move(submitted.diagnostic),
    };
  }
  isFailed = true;
  return {
      .status = SenderPumpStatus::Failed,
      .senderError = TransferSenderError::ProtocolError,
      .diagnostic = QStringLiteral("sender sink returned an invalid status"),
  };
}

TransferSenderPump::TransferSenderPump(
    TransferSenderRequest request, TransferFrameSink &sink, SenderBackpressureLimits limits
)
    : m_impl(std::make_unique<Impl>(std::move(request), sink, limits))
{
}

TransferSenderPump::~TransferSenderPump() = default;

SenderPumpResult TransferSenderPump::pump()
{
  return m_impl->pump();
}

bool TransferSenderPump::paused() const noexcept
{
  return m_impl->isPaused;
}

bool TransferSenderPump::finished() const noexcept
{
  return m_impl->sender.finished() && !m_impl->pendingFrame.has_value();
}

quint64 TransferSenderPump::bufferedFrameBytes() const noexcept
{
  return m_impl->pendingFrame.has_value() ? frameMemoryBytes(*m_impl->pendingFrame) : 0;
}

quint64 TransferSenderPump::bytesProduced() const noexcept
{
  return m_impl->sender.bytesProduced();
}

} // namespace relaydesk::transfer
