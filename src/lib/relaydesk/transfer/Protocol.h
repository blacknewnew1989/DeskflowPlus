// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <optional>
#include <variant>

namespace relaydesk::transfer {

inline constexpr char kProtocolMagic[4] = {'R', 'D', 'F', 'T'};
inline constexpr quint16 kProtocolMajorVersion = 1;
inline constexpr qsizetype kFixedHeaderBytes = 32;
inline constexpr qsizetype kUuidBytes = 16;
inline constexpr qsizetype kSha256Bytes = 32;
inline constexpr qsizetype kMaxControlStringUtf8Bytes = 4096;

using TransferId = QUuid;
using FileId = QUuid;

enum class MessageType : quint16
{
  Hello = 0x0001,
  AuthResult = 0x0002,
  Capabilities = 0x0003,
  Heartbeat = 0x0004,
  HeartbeatAck = 0x0005,

  TransferOffer = 0x0100,
  TransferAccept = 0x0101,
  TransferReject = 0x0102,
  ManifestPage = 0x0103,
  ManifestComplete = 0x0104,

  FileBegin = 0x0200,
  FileChunk = 0x0201,
  FileCheckpoint = 0x0202,
  FileEnd = 0x0203,
  FileResult = 0x0204,

  TransferPause = 0x0300,
  TransferResume = 0x0301,
  TransferCancel = 0x0302,
  TransferComplete = 0x0303,
  TransferResult = 0x0304,

  ResumeQuery = 0x0400,
  ResumeResponse = 0x0401,

  Error = 0x7ffe,
  Goodbye = 0x7fff,
};

enum FrameFlag : quint32
{
  AckRequired = 0x00000001U,
  Response = 0x00000002U,
  Final = 0x00000004U,
  Retryable = 0x00000008U,
  CompressedMetadata = 0x00000010U,
};

enum class ConflictPolicy
{
  AutoRename,
  Overwrite,
  Skip,
  Ask,
};

struct TransferOffer
{
  TransferId transferId;
  QString displayName;
  quint64 totalBytes = 0;
  quint64 fileCount = 0;
  quint64 directoryCount = 0;
  QByteArray manifestSha256;
  quint64 manifestPageCount = 0;
  ConflictPolicy requestedConflictPolicy = ConflictPolicy::Ask;
  quint64 createdAtMs = 0;

  [[nodiscard]] bool operator==(const TransferOffer &) const = default;
};

struct TransferAccept
{
  TransferId transferId;
  ConflictPolicy effectiveConflictPolicy = ConflictPolicy::AutoRename;
  QString logicalDestination;
  quint64 freeBytes = 0;
  bool autoAccepted = false;

  [[nodiscard]] bool operator==(const TransferAccept &) const = default;
};

struct ErrorMessage
{
  quint64 code = 0;
  QString diagnostic;
  bool retryable = false;
  std::optional<TransferId> transferId;
  std::optional<FileId> fileId;

  [[nodiscard]] bool operator==(const ErrorMessage &) const = default;
};

using ControlMessage = std::variant<TransferOffer, TransferAccept, ErrorMessage>;

struct ProtocolLimits
{
  quint32 maxControlMetadataBytes = 1U * 1024U * 1024U;
  quint64 maxDataPayloadBytes = 4ULL * 1024ULL * 1024ULL;
  quint64 maxFrameBytes =
      static_cast<quint64>(kFixedHeaderBytes) + static_cast<quint64>(maxControlMetadataBytes) + maxDataPayloadBytes;
};

struct Frame
{
  quint16 version = kProtocolMajorVersion;
  MessageType type = MessageType::Heartbeat;
  quint32 flags = 0;
  quint64 streamId = 0;
  QByteArray metadata;
  QByteArray payload;

  [[nodiscard]] bool operator==(const Frame &) const = default;
};

[[nodiscard]] inline bool isKnownMessageType(MessageType type) noexcept
{
  switch (type) {
  case MessageType::Hello:
  case MessageType::AuthResult:
  case MessageType::Capabilities:
  case MessageType::Heartbeat:
  case MessageType::HeartbeatAck:
  case MessageType::TransferOffer:
  case MessageType::TransferAccept:
  case MessageType::TransferReject:
  case MessageType::ManifestPage:
  case MessageType::ManifestComplete:
  case MessageType::FileBegin:
  case MessageType::FileChunk:
  case MessageType::FileCheckpoint:
  case MessageType::FileEnd:
  case MessageType::FileResult:
  case MessageType::TransferPause:
  case MessageType::TransferResume:
  case MessageType::TransferCancel:
  case MessageType::TransferComplete:
  case MessageType::TransferResult:
  case MessageType::ResumeQuery:
  case MessageType::ResumeResponse:
  case MessageType::Error:
  case MessageType::Goodbye:
    return true;
  }
  return false;
}

[[nodiscard]] inline MessageType messageType(const ControlMessage &message) noexcept
{
  if (std::holds_alternative<TransferOffer>(message)) {
    return MessageType::TransferOffer;
  }
  if (std::holds_alternative<TransferAccept>(message)) {
    return MessageType::TransferAccept;
  }
  return MessageType::Error;
}

} // namespace relaydesk::transfer
