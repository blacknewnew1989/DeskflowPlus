// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>
#include <variant>

namespace relaydesk::transfer {

inline constexpr quint64 kMaximumCompletedTransferFiles = 100'000;
inline constexpr qsizetype kMaximumTransferResultDiagnosticUtf8Bytes = 512;

enum class TransferResultCode : quint32
{
  Ok = 0,
  Partial = 1,
  Cancelled = 2,
  Failed = 3,
};

struct TransferCompleteMessage
{
  TransferId transferId;
  quint64 completedFiles = 0;
  quint64 skippedFiles = 0;
  quint64 totalBytes = 0;

  [[nodiscard]] bool operator==(const TransferCompleteMessage &) const = default;
};

struct TransferResultMessage
{
  TransferId transferId;
  TransferResultCode code = TransferResultCode::Ok;
  QString diagnostic;

  [[nodiscard]] bool operator==(const TransferResultMessage &) const = default;
};

using TransferCompletionMessage = std::variant<TransferCompleteMessage, TransferResultMessage>;

enum class TransferCompletionCodecError
{
  None,
  UnsupportedVersion,
  UnsupportedMessageType,
  TooLarge,
  MalformedCbor,
  InvalidFields,
  InvalidTransferId,
  InvalidFileCount,
  InvalidTotalBytes,
  InvalidResultCode,
  InvalidDiagnostic,
};

struct TransferCompletionDecodeResult
{
  std::optional<TransferCompletionMessage> message;
  TransferCompletionCodecError error = TransferCompletionCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == TransferCompletionCodecError::None;
  }
};

class TransferCompletionCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const TransferCompletionMessage &message, QString *error = nullptr);
  [[nodiscard]] static TransferCompletionDecodeResult
  decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata);
};

[[nodiscard]] MessageType messageType(const TransferCompletionMessage &message) noexcept;

} // namespace relaydesk::transfer
