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

/** Stable AUTH_RESULT rejection codes. None is valid only for an accepted result. */
enum class AuthResultErrorCode : quint32
{
  None = 0,
  InvalidHello = 1,
  UnsupportedVersion = 2,
  UnknownPeer = 3,
  RevokedPeer = 4,
  FingerprintMismatch = 5,
  InternalError = 6,
};

[[nodiscard]] constexpr bool isKnownAuthResultErrorCode(AuthResultErrorCode code) noexcept
{
  switch (code) {
  case AuthResultErrorCode::InvalidHello:
  case AuthResultErrorCode::UnsupportedVersion:
  case AuthResultErrorCode::UnknownPeer:
  case AuthResultErrorCode::RevokedPeer:
  case AuthResultErrorCode::FingerprintMismatch:
  case AuthResultErrorCode::InternalError:
    return true;
  case AuthResultErrorCode::None:
    return false;
  }
  return false;
}

struct AuthResultMessage
{
  bool accepted = false;
  AuthResultErrorCode errorCode = AuthResultErrorCode::None;
  QString diagnostic;

  [[nodiscard]] bool operator==(const AuthResultMessage &) const = default;
};

struct HeartbeatMessage
{
  quint64 sequence = 0;
  quint64 timestampMs = 0;

  [[nodiscard]] bool operator==(const HeartbeatMessage &) const = default;
};

enum class GoodbyeReason : quint32
{
  Normal = 0,
  ApplicationShutdown = 1,
  ProtocolError = 2,
  IdleTimeout = 3,
};

struct GoodbyeMessage
{
  GoodbyeReason reason = GoodbyeReason::Normal;
  QString diagnostic;

  [[nodiscard]] bool operator==(const GoodbyeMessage &) const = default;
};

enum class SessionMessageError
{
  None,
  UnsupportedVersion,
  UnsupportedMessageType,
  TooLarge,
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
  InvalidSequence,
  InvalidGoodbyeReason,
  InvalidDiagnostic,
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
using HeartbeatDecodeResult = SessionMessageDecodeResult<HeartbeatMessage>;
using GoodbyeDecodeResult = SessionMessageDecodeResult<GoodbyeMessage>;

class SessionMessageCodec final
{
public:
  [[nodiscard]] static QByteArray encodeHello(const HelloMessage &message, QString *error = nullptr);
  [[nodiscard]] static HelloDecodeResult decodeHello(MessageType type, QByteArrayView metadata);

  [[nodiscard]] static QByteArray encodeAuthResult(const AuthResultMessage &message, QString *error = nullptr);
  [[nodiscard]] static AuthResultDecodeResult decodeAuthResult(MessageType type, QByteArrayView metadata);

  // HEARTBEAT and HEARTBEAT_ACK use the same canonical metadata. The ACK
  // echoes both fields exactly so the initiator can reject stale responses.
  [[nodiscard]] static QByteArray
  encodeHeartbeat(MessageType type, const HeartbeatMessage &message, QString *error = nullptr);
  [[nodiscard]] static HeartbeatDecodeResult
  decodeHeartbeat(quint16 protocolVersion, MessageType type, QByteArrayView metadata);

  [[nodiscard]] static QByteArray encodeGoodbye(const GoodbyeMessage &message, QString *error = nullptr);
  [[nodiscard]] static GoodbyeDecodeResult
  decodeGoodbye(quint16 protocolVersion, MessageType type, QByteArrayView metadata);
};

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::AuthResultErrorCode)
