// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace relaydesk::transfer {

enum class ControlMessageError
{
  None,
  UnsupportedVersion,
  UnsupportedMessageType,
  MalformedCbor,
  MetadataNotMap,
  NonIntegerKey,
  MissingField,
  InvalidFieldType,
  InvalidFieldValue,
};

struct ControlMessageDecodeResult
{
  std::optional<ControlMessage> message;
  ControlMessageError error = ControlMessageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == ControlMessageError::None;
  }
};

class ControlMessageCodec final
{
public:
  [[nodiscard]] static QByteArray
  encode(quint16 protocolVersion, const ControlMessage &message, QString *error = nullptr);

  [[nodiscard]] static ControlMessageDecodeResult
  decode(quint16 protocolVersion, MessageType type, const QByteArray &metadata);
};

} // namespace relaydesk::transfer
