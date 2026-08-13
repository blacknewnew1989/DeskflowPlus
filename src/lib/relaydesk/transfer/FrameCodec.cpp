// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/ProtocolMessageRegistry.h"

#include <QtEndian>

#include <cstring>
#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

template <typename T> void writeBigEndian(char *destination, T value)
{
  qToBigEndian<T>(value, reinterpret_cast<uchar *>(destination));
}

template <typename T> T readBigEndian(const char *source)
{
  return qFromBigEndian<T>(reinterpret_cast<const uchar *>(source));
}

FrameDecodeResult protocolError(FrameDecodeError error, QString diagnostic)
{
  return {
      .status = FrameDecodeStatus::ProtocolError,
      .error = error,
      .diagnostic = std::move(diagnostic),
      .consumedBytes = 0,
  };
}

FrameDecodeResult needMoreData()
{
  return {
      .status = FrameDecodeStatus::NeedMoreData,
      .error = FrameDecodeError::None,
      .diagnostic = {},
      .consumedBytes = 0,
  };
}

bool checkedFrameBytes(quint32 metadataBytes, quint64 payloadBytes, quint64 &totalBytes)
{
  constexpr quint64 fixed = static_cast<quint64>(kFixedHeaderBytes);
  if (payloadBytes > std::numeric_limits<quint64>::max() - fixed) {
    return false;
  }
  const quint64 fixedAndPayload = fixed + payloadBytes;
  if (static_cast<quint64>(metadataBytes) > std::numeric_limits<quint64>::max() - fixedAndPayload) {
    return false;
  }
  totalBytes = fixedAndPayload + static_cast<quint64>(metadataBytes);
  return true;
}

bool validateLengths(
    quint32 metadataBytes, quint64 payloadBytes, const ProtocolLimits &limits, FrameDecodeResult &failure,
    quint64 &frameBytes
)
{
  if (metadataBytes > limits.maxControlMetadataBytes) {
    failure = protocolError(
        FrameDecodeError::ControlMetadataTooLarge, QStringLiteral("control metadata exceeds the local limit")
    );
    return false;
  }
  if (payloadBytes > limits.maxDataPayloadBytes) {
    failure =
        protocolError(FrameDecodeError::DataPayloadTooLarge, QStringLiteral("data payload exceeds the local limit"));
    return false;
  }
  if (!checkedFrameBytes(metadataBytes, payloadBytes, frameBytes)) {
    failure = protocolError(FrameDecodeError::LengthOverflow, QStringLiteral("frame length arithmetic overflow"));
    return false;
  }
  if (frameBytes > limits.maxFrameBytes) {
    failure = protocolError(FrameDecodeError::FrameTooLarge, QStringLiteral("frame exceeds the local frame limit"));
    return false;
  }
  if (frameBytes > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    failure = protocolError(
        FrameDecodeError::LengthOverflow, QStringLiteral("frame length cannot be represented in local address space")
    );
    return false;
  }
  return true;
}

FrameDecodeError frameError(ProtocolEnvelopeError error)
{
  switch (error) {
  case ProtocolEnvelopeError::None:
    return FrameDecodeError::None;
  case ProtocolEnvelopeError::UnknownMessageType:
    return FrameDecodeError::UnknownMessageType;
  case ProtocolEnvelopeError::ReservedMessageType:
    return FrameDecodeError::ReservedMessageType;
  case ProtocolEnvelopeError::InvalidFlags:
    return FrameDecodeError::InvalidFlags;
  case ProtocolEnvelopeError::InvalidStreamId:
    return FrameDecodeError::InvalidStreamId;
  case ProtocolEnvelopeError::MissingMetadata:
    return FrameDecodeError::MissingMetadata;
  case ProtocolEnvelopeError::UnexpectedMetadata:
    return FrameDecodeError::UnexpectedMetadata;
  case ProtocolEnvelopeError::MissingPayload:
    return FrameDecodeError::MissingPayload;
  case ProtocolEnvelopeError::UnexpectedPayload:
    return FrameDecodeError::UnexpectedPayload;
  }
  return FrameDecodeError::UnknownMessageType;
}

} // namespace

QByteArray FrameCodec::encode(const Frame &frame, const ProtocolLimits &limits, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  const auto fail = [error](const QString &diagnostic) {
    if (error != nullptr) {
      *error = diagnostic;
    }
    return QByteArray{};
  };

  if (frame.version != kProtocolMajorVersion) {
    return fail(QStringLiteral("unsupported RDFT major version"));
  }
  if (!isKnownMessageType(frame.type)) {
    return fail(QStringLiteral("unknown RDFT message type"));
  }
  if (static_cast<quint64>(frame.metadata.size()) > std::numeric_limits<quint32>::max()) {
    return fail(QStringLiteral("control metadata length cannot be represented on the wire"));
  }

  const auto metadataBytes = static_cast<quint32>(frame.metadata.size());
  const auto payloadBytes = static_cast<quint64>(frame.payload.size());
  const auto envelope =
      validateProtocolEnvelope(frame.type, frame.flags, frame.streamId, metadataBytes, payloadBytes);
  if (!envelope.ok()) {
    return fail(envelope.diagnostic);
  }
  FrameDecodeResult failure;
  quint64 frameBytes = 0;
  if (!validateLengths(metadataBytes, payloadBytes, limits, failure, frameBytes)) {
    return fail(failure.diagnostic);
  }

  QByteArray encoded(static_cast<qsizetype>(frameBytes), Qt::Uninitialized);
  char *header = encoded.data();
  std::memcpy(header, kProtocolMagic, sizeof(kProtocolMagic));
  writeBigEndian<quint16>(header + 4, frame.version);
  writeBigEndian<quint16>(header + 6, static_cast<quint16>(frame.type));
  writeBigEndian<quint32>(header + 8, frame.flags);
  writeBigEndian<quint32>(header + 12, metadataBytes);
  writeBigEndian<quint64>(header + 16, payloadBytes);
  writeBigEndian<quint64>(header + 24, frame.streamId);

  if (!frame.metadata.isEmpty()) {
    std::memcpy(header + kFixedHeaderBytes, frame.metadata.constData(), static_cast<size_t>(frame.metadata.size()));
  }
  if (!frame.payload.isEmpty()) {
    std::memcpy(
        header + kFixedHeaderBytes + frame.metadata.size(), frame.payload.constData(),
        static_cast<size_t>(frame.payload.size())
    );
  }
  return encoded;
}

FrameDecodeResult FrameCodec::tryDecode(QByteArray &buffer, Frame &output, const ProtocolLimits &limits)
{
  if (buffer.size() < kFixedHeaderBytes) {
    return needMoreData();
  }

  const char *header = buffer.constData();
  if (std::memcmp(header, kProtocolMagic, sizeof(kProtocolMagic)) != 0) {
    return protocolError(FrameDecodeError::InvalidMagic, QStringLiteral("invalid RDFT magic"));
  }

  const quint16 version = readBigEndian<quint16>(header + 4);
  if (version != kProtocolMajorVersion) {
    return protocolError(
        FrameDecodeError::UnsupportedMajorVersion, QStringLiteral("unsupported RDFT major version %1").arg(version)
    );
  }

  const auto type = static_cast<MessageType>(readBigEndian<quint16>(header + 6));
  if (!isKnownMessageType(type)) {
    return protocolError(FrameDecodeError::UnknownMessageType, QStringLiteral("unknown RDFT message type"));
  }

  const quint32 metadataBytes = readBigEndian<quint32>(header + 12);
  const quint64 payloadBytes = readBigEndian<quint64>(header + 16);
  FrameDecodeResult failure;
  quint64 frameBytes = 0;
  if (!validateLengths(metadataBytes, payloadBytes, limits, failure, frameBytes)) {
    return failure;
  }

  const quint32 flags = readBigEndian<quint32>(header + 8);
  const quint64 streamId = readBigEndian<quint64>(header + 24);
  const auto envelope = validateProtocolEnvelope(type, flags, streamId, metadataBytes, payloadBytes);
  if (!envelope.ok()) {
    return protocolError(frameError(envelope.error), envelope.diagnostic);
  }

  const auto localFrameBytes = static_cast<qsizetype>(frameBytes);
  if (buffer.size() < localFrameBytes) {
    return needMoreData();
  }

  const auto localMetadataBytes = static_cast<qsizetype>(metadataBytes);
  const auto localPayloadBytes = static_cast<qsizetype>(payloadBytes);
  Frame decoded;
  decoded.version = version;
  decoded.type = type;
  decoded.flags = flags;
  decoded.streamId = streamId;
  decoded.metadata = QByteArray(header + kFixedHeaderBytes, localMetadataBytes);
  decoded.payload = QByteArray(header + kFixedHeaderBytes + localMetadataBytes, localPayloadBytes);

  output = std::move(decoded);
  buffer.remove(0, localFrameBytes);
  return {
      .status = FrameDecodeStatus::FrameReady,
      .error = FrameDecodeError::None,
      .diagnostic = {},
      .consumedBytes = localFrameBytes,
  };
}

} // namespace relaydesk::transfer
