// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/FileId.h"
#include "relaydesk/transfer/TransferId.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
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

enum class MessageType : quint16
{
#define RDFT_MESSAGE(name, value, ...) name = value,
#include "relaydesk/transfer/ProtocolMessageRegistry.def"
#undef RDFT_MESSAGE
};

inline constexpr qsizetype kProtocolMessageTypeCount =
    0
#define RDFT_MESSAGE(...) +1
#include "relaydesk/transfer/ProtocolMessageRegistry.def"
#undef RDFT_MESSAGE
    ;

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

enum class RejectReason : quint32
{
  UserDeclined = 1,
  NotTrusted = 2,
  PolicyDenied = 3,
  InsufficientSpace = 4,
  TooManyFiles = 5,
  PathInvalid = 6,
  UnsupportedCapability = 7,
  Busy = 8,
  InternalError = 9,
};

enum class TransferCancelReason : quint32
{
  UserRequested = 1,
  ApplicationShutdown = 2,
};

enum class FileResultCode : quint32
{
  Ok = 0,
  HashMismatch = 1,
  SizeMismatch = 2,
  SourceChanged = 3,
  TargetExists = 4,
  DiskFull = 5,
  PermissionDenied = 6,
  PathInvalid = 7,
  IoError = 8,
  Cancelled = 9,
};

/** Stable RDFT ERROR codes. Values not listed here are invalid in protocol v1. */
enum class ProtocolErrorCode : quint64
{
  None = 0,
  UnsupportedVersion = 1001,
  InvalidFrame = 1002,
  UnsupportedMessage = 1003,
  InvalidState = 1004,
  TemporarilyUnavailable = 1005,
  InternalError = 1006,
};

[[nodiscard]] constexpr bool isKnownProtocolErrorCode(ProtocolErrorCode code) noexcept
{
  switch (code) {
  case ProtocolErrorCode::UnsupportedVersion:
  case ProtocolErrorCode::InvalidFrame:
  case ProtocolErrorCode::UnsupportedMessage:
  case ProtocolErrorCode::InvalidState:
  case ProtocolErrorCode::TemporarilyUnavailable:
  case ProtocolErrorCode::InternalError:
    return true;
  case ProtocolErrorCode::None:
    return false;
  }
  return false;
}

/** Retryability is a property of the frozen code catalog, not caller input. */
[[nodiscard]] constexpr bool protocolErrorIsRetryable(ProtocolErrorCode code) noexcept
{
  return code == ProtocolErrorCode::TemporarilyUnavailable;
}

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

struct TransferReject
{
  TransferId transferId;
  RejectReason reason = RejectReason::UserDeclined;
  QString diagnostic;

  [[nodiscard]] bool operator==(const TransferReject &) const = default;
};

struct ErrorMessage
{
  ProtocolErrorCode code = ProtocolErrorCode::None;
  QString diagnostic;
  std::optional<TransferId> transferId;
  std::optional<FileId> fileId;

  [[nodiscard]] bool operator==(const ErrorMessage &) const = default;
};

using ControlMessage = std::variant<TransferOffer, TransferAccept, TransferReject, ErrorMessage>;

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

[[nodiscard]] bool isKnownMessageType(MessageType type) noexcept;

[[nodiscard]] inline MessageType messageType(const ControlMessage &message) noexcept
{
  if (std::holds_alternative<TransferOffer>(message)) {
    return MessageType::TransferOffer;
  }
  if (std::holds_alternative<TransferAccept>(message)) {
    return MessageType::TransferAccept;
  }
  if (std::holds_alternative<TransferReject>(message)) {
    return MessageType::TransferReject;
  }
  return MessageType::Error;
}

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::ConflictPolicy)
Q_DECLARE_METATYPE(relaydesk::transfer::RejectReason)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferCancelReason)
Q_DECLARE_METATYPE(relaydesk::transfer::ProtocolErrorCode)
