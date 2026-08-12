/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QString>
#include <QUuid>

#include <optional>
#include <variant>

namespace deskflow::relaydesk {

inline constexpr auto kPairingProtocol = "relaydesk-pairing";
inline constexpr qint64 kPairingProtocolVersion = 1;
inline constexpr qsizetype kMaximumPairingMessageBytes = 16 * 1024;

enum class PairingMessageType : qint64
{
  Request = 1,
  CodeSubmission = 2,
  Result = 3,
};

struct PairingRequest
{
  QUuid pairingSessionId;
  DeviceInfo sender;
  QDateTime expiresAtUtc;

  bool operator==(const PairingRequest &) const = default;
};

struct PairingCodeSubmission
{
  QUuid pairingSessionId;
  DeviceInfo sender;
  QString sixDigitSas;

  bool operator==(const PairingCodeSubmission &) const = default;
};

struct PairingResultMessage
{
  QUuid pairingSessionId;
  bool accepted = false;
  QString errorMessageKey;

  bool operator==(const PairingResultMessage &) const = default;
};

using PairingMessage = std::variant<PairingRequest, PairingCodeSubmission, PairingResultMessage>;

enum class PairingMessageError
{
  None,
  TooLarge,
  MalformedCbor,
  InvalidEnvelope,
  UnsupportedProtocol,
  UnsupportedVersion,
  UnsupportedMessageType,
  InvalidPayload,
  InvalidSessionId,
  InvalidDeviceInfo,
  InvalidFingerprint,
  InvalidExpiry,
  InvalidSas,
  InvalidResult,
};

struct PairingMessageDecodeResult
{
  std::optional<PairingMessage> message;
  PairingMessageError error = PairingMessageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == PairingMessageError::None;
  }
};

class PairingMessageCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const PairingMessage &message, QString *errorMessage = nullptr);
  [[nodiscard]] static PairingMessageDecodeResult decode(QByteArrayView bytes);
};

} // namespace deskflow::relaydesk
