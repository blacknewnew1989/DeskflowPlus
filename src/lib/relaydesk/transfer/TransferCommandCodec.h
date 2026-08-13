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

enum class TransferCancelReason : quint32
{
  UserRequested = 1,
  ApplicationShutdown = 2,
};

struct TransferPauseMessage
{
  TransferId transferId;

  [[nodiscard]] bool operator==(const TransferPauseMessage &) const = default;
};

struct TransferResumeMessage
{
  TransferId transferId;

  [[nodiscard]] bool operator==(const TransferResumeMessage &) const = default;
};

struct TransferCancelMessage
{
  TransferId transferId;
  TransferCancelReason reason = TransferCancelReason::UserRequested;
  bool keepPartial = true;

  [[nodiscard]] bool operator==(const TransferCancelMessage &) const = default;
};

using TransferCommandMessage = std::variant<TransferPauseMessage, TransferResumeMessage, TransferCancelMessage>;

enum class TransferCommandCodecError
{
  None,
  UnsupportedVersion,
  UnsupportedMessageType,
  TooLarge,
  MalformedCbor,
  InvalidFields,
  InvalidTransferId,
  InvalidReason,
  InvalidKeepPartial,
};

struct TransferCommandDecodeResult
{
  std::optional<TransferCommandMessage> message;
  TransferCommandCodecError error = TransferCommandCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == TransferCommandCodecError::None;
  }
};

class TransferCommandCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const TransferCommandMessage &message, QString *error = nullptr);
  [[nodiscard]] static TransferCommandDecodeResult
  decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata);
};

[[nodiscard]] MessageType messageType(const TransferCommandMessage &message) noexcept;

} // namespace relaydesk::transfer
