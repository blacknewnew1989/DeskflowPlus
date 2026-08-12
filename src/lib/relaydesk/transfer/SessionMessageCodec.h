// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

namespace relaydesk::transfer {

struct HelloMessage
{
  deskflow::relaydesk::DeviceId deviceId;
  QUuid sessionId;
  QString appVersion;
  QList<quint16> supportedMajorVersions;
  QByteArray certificateFingerprintSha256;
  quint64 timestampMs = 0;

  [[nodiscard]] bool operator==(const HelloMessage &) const = default;
};

struct AuthResultMessage
{
  bool accepted = false;
  quint32 errorCode = 0;
  QString diagnostic;

  [[nodiscard]] bool operator==(const AuthResultMessage &) const = default;
};

enum class SessionMessageError
{
  None,
  UnsupportedMessageType,
  MalformedCbor,
  MetadataNotMap,
  InvalidFields,
  InvalidDeviceId,
  InvalidSessionId,
  InvalidAppVersion,
  InvalidVersions,
  InvalidFingerprint,
  InvalidTimestamp,
  InvalidAuthResult,
};

template <typename Message> struct SessionMessageDecodeResult
{
  std::optional<Message> message;
  SessionMessageError error = SessionMessageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == SessionMessageError::None;
  }
};

using HelloDecodeResult = SessionMessageDecodeResult<HelloMessage>;
using AuthResultDecodeResult = SessionMessageDecodeResult<AuthResultMessage>;

class SessionMessageCodec final
{
public:
  [[nodiscard]] static QByteArray encodeHello(const HelloMessage &message, QString *error = nullptr);
  [[nodiscard]] static HelloDecodeResult decodeHello(MessageType type, QByteArrayView metadata);

  [[nodiscard]] static QByteArray encodeAuthResult(const AuthResultMessage &message, QString *error = nullptr);
  [[nodiscard]] static AuthResultDecodeResult decodeAuthResult(MessageType type, QByteArrayView metadata);
};

} // namespace relaydesk::transfer
