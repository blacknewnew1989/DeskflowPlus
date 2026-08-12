/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryCodec.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QSet>

#include <utility>

namespace deskflow::relaydesk {

namespace {
enum DiscoveryKey : qint64
{
  ProtocolKey = 1,
  VersionKey = 2,
  MessageTypeKey = 3,
  PayloadKey = 4,
};

constexpr qsizetype kMaximumDisplayNameBytes = 256;
constexpr qsizetype kMaximumPlatformBytes = 32;
constexpr qsizetype kMaximumArchitectureBytes = 32;
constexpr qsizetype kMaximumAppVersionBytes = 64;

void setError(QString *errorMessage, const QString &message)
{
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
}

DiscoveryDecodeResult failure(DiscoveryCodecError error, QString diagnostic)
{
  return {
      .datagram = std::nullopt,
      .error = error,
      .diagnostic = std::move(diagnostic),
  };
}

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

bool hasOnlyEnvelopeKeys(const QCborMap &map)
{
  static const QSet<qint64> allowedKeys = {ProtocolKey, VersionKey, MessageTypeKey, PayloadKey};
  if (map.size() != allowedKeys.size()) {
    return false;
  }

  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || !allowedKeys.contains(iterator.key().toInteger())) {
      return false;
    }
  }
  return true;
}

bool fitsUtf8Limit(const QString &value, qsizetype maximumBytes)
{
  return !value.isEmpty() && value.toUtf8().size() <= maximumBytes;
}

bool validateDeviceInfoLimits(const DeviceInfo &device, QString *errorMessage)
{
  if (!fitsUtf8Limit(device.displayName, kMaximumDisplayNameBytes)) {
    setError(errorMessage, QStringLiteral("Discovery display name is empty or exceeds 256 UTF-8 bytes"));
    return false;
  }
  if (!fitsUtf8Limit(device.platform, kMaximumPlatformBytes)) {
    setError(errorMessage, QStringLiteral("Discovery platform is empty or exceeds 32 UTF-8 bytes"));
    return false;
  }
  if (!fitsUtf8Limit(device.architecture, kMaximumArchitectureBytes)) {
    setError(errorMessage, QStringLiteral("Discovery architecture is empty or exceeds 32 UTF-8 bytes"));
    return false;
  }
  if (!fitsUtf8Limit(device.appVersion, kMaximumAppVersionBytes)) {
    setError(errorMessage, QStringLiteral("Discovery app version is empty or exceeds 64 UTF-8 bytes"));
    return false;
  }
  if (!device.certificateFingerprintSha256.isEmpty() && device.certificateFingerprintSha256.size() != 32) {
    setError(errorMessage, QStringLiteral("Discovery certificate fingerprint must contain exactly 32 bytes"));
    return false;
  }
  return true;
}
} // namespace

QByteArray DiscoveryCodec::encodeAdvertisement(const DeviceInfo &device, QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  if (!validateDeviceInfoLimits(device, errorMessage)) {
    return {};
  }

  QString deviceError;
  const auto payload = DeviceInfoCodec::serialize(device, &deviceError);
  if (payload.isEmpty()) {
    setError(errorMessage, QStringLiteral("Unable to encode discovery device information: %1").arg(deviceError));
    return {};
  }
  if (payload.size() > kMaximumDiscoveryPayloadBytes) {
    setError(errorMessage, QStringLiteral("Discovery payload exceeds the local limit"));
    return {};
  }

  const QCborMap envelope = {
      {key(ProtocolKey), QCborValue(QString::fromLatin1(kDiscoveryProtocol))},
      {key(VersionKey), QCborValue(kDiscoveryProtocolVersion)},
      {key(MessageTypeKey), QCborValue(static_cast<qint64>(DiscoveryMessageType::Advertisement))},
      {key(PayloadKey), QCborValue(payload)},
  };
  const auto encoded = QCborValue(envelope).toCbor();
  if (encoded.size() > kMaximumDiscoveryDatagramBytes) {
    setError(errorMessage, QStringLiteral("Discovery datagram exceeds the local limit"));
    return {};
  }
  return encoded;
}

DiscoveryDecodeResult DiscoveryCodec::decode(QByteArrayView datagram)
{
  if (datagram.isEmpty()) {
    return failure(DiscoveryCodecError::EmptyDatagram, QStringLiteral("Discovery datagram is empty"));
  }
  if (datagram.size() > kMaximumDiscoveryDatagramBytes) {
    return failure(DiscoveryCodecError::DatagramTooLarge, QStringLiteral("Discovery datagram exceeds the local limit"));
  }

  QCborParserError parserError;
  const auto value = QCborValue::fromCbor(datagram.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != datagram.size() || !value.isMap()) {
    return failure(DiscoveryCodecError::InvalidCbor, QStringLiteral("Discovery datagram is not a valid CBOR map"));
  }

  const auto envelope = value.toMap();
  if (!hasOnlyEnvelopeKeys(envelope)) {
    return failure(
        DiscoveryCodecError::InvalidEnvelope, QStringLiteral("Discovery datagram contains missing or unknown fields")
    );
  }

  const auto protocol = valueFor(envelope, ProtocolKey);
  if (!protocol.isString() || protocol.toString() != QString::fromLatin1(kDiscoveryProtocol)) {
    return failure(DiscoveryCodecError::UnsupportedProtocol, QStringLiteral("Unsupported discovery protocol"));
  }

  const auto version = valueFor(envelope, VersionKey);
  if (!version.isInteger() || version.toInteger() != kDiscoveryProtocolVersion) {
    return failure(DiscoveryCodecError::UnsupportedVersion, QStringLiteral("Unsupported discovery protocol version"));
  }

  const auto messageType = valueFor(envelope, MessageTypeKey);
  if (!messageType.isInteger() ||
      messageType.toInteger() != static_cast<qint64>(DiscoveryMessageType::Advertisement)) {
    return failure(DiscoveryCodecError::UnknownMessageType, QStringLiteral("Unknown discovery message type"));
  }

  const auto encodedPayload = valueFor(envelope, PayloadKey);
  if (!encodedPayload.isByteArray()) {
    return failure(DiscoveryCodecError::InvalidEnvelope, QStringLiteral("Discovery payload is not a byte string"));
  }
  const auto payload = encodedPayload.toByteArray();
  if (payload.isEmpty() || payload.size() > kMaximumDiscoveryPayloadBytes) {
    return failure(DiscoveryCodecError::PayloadTooLarge, QStringLiteral("Discovery payload is empty or too large"));
  }

  QCborParserError payloadParserError;
  const auto payloadValue = QCborValue::fromCbor(payload, &payloadParserError);
  if (payloadParserError.error != QCborError::NoError || payloadParserError.offset != payload.size() ||
      !payloadValue.isMap()) {
    return failure(DiscoveryCodecError::InvalidDeviceInfo, QStringLiteral("Discovery payload is not one CBOR map"));
  }

  QString deviceError;
  const auto device = DeviceInfoCodec::deserialize(payload, &deviceError);
  if (!device.has_value()) {
    return failure(
        DiscoveryCodecError::InvalidDeviceInfo,
        QStringLiteral("Discovery payload contains invalid device information: %1").arg(deviceError)
    );
  }
  QString limitError;
  if (!validateDeviceInfoLimits(*device, &limitError)) {
    return failure(DiscoveryCodecError::InvalidDeviceInfo, limitError);
  }

  return {
      .datagram = DiscoveryDatagram{.type = DiscoveryMessageType::Advertisement, .device = *device},
      .error = DiscoveryCodecError::None,
      .diagnostic = {},
  };
}

} // namespace deskflow::relaydesk
