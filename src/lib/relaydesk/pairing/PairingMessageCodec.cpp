/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingMessageCodec.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <limits>
#include <utility>

namespace deskflow::relaydesk {
namespace {

enum EnvelopeKey : qint64
{
  ProtocolKey = 1,
  VersionKey = 2,
  MessageTypeKey = 3,
  PayloadKey = 4,
};

enum PayloadKey : qint64
{
  SessionIdKey = 1,
  SenderKey = 2,
  ValueKey = 3,
  DiagnosticKey = 4,
};

const auto kSixDigitPattern = QRegularExpression(QStringLiteral("^[0-9]{6}$"));
constexpr qsizetype kSha256Bytes = 32;
constexpr qsizetype kMaximumDiagnosticBytes = 512;

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

void setError(QString *errorMessage, const QString &message)
{
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
}

PairingMessageDecodeResult failure(PairingMessageError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool hasOnlyKeys(const QCborMap &map, const QSet<qint64> &allowed, qsizetype requiredSize)
{
  if (map.size() != requiredSize) {
    return false;
  }
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || !allowed.contains(iterator.key().toInteger())) {
      return false;
    }
  }
  return true;
}

std::optional<QUuid> readSessionId(const QCborMap &payload)
{
  const auto encoded = valueFor(payload, SessionIdKey);
  if (!encoded.isByteArray() || encoded.toByteArray().size() != 16) {
    return std::nullopt;
  }
  const auto sessionId = QUuid::fromRfc4122(encoded.toByteArray());
  return sessionId.isNull() ? std::nullopt : std::optional<QUuid>(sessionId);
}

std::optional<DeviceInfo> readSender(const QCborMap &payload, QString *diagnostic)
{
  const auto encoded = valueFor(payload, SenderKey);
  if (!encoded.isByteArray()) {
    setError(diagnostic, QStringLiteral("pairing sender must be encoded device information"));
    return std::nullopt;
  }
  const QByteArray senderBytes = encoded.toByteArray();
  QCborParserError parserError;
  const auto senderRoot = QCborValue::fromCbor(senderBytes, &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != senderBytes.size() || !senderRoot.isMap()) {
    setError(diagnostic, QStringLiteral("pairing sender is not one device information CBOR map"));
    return std::nullopt;
  }
  auto sender = DeviceInfoCodec::deserialize(senderBytes, diagnostic);
  if (!sender.has_value()) {
    return std::nullopt;
  }
  if (sender->certificateFingerprintSha256.size() != kSha256Bytes) {
    setError(diagnostic, QStringLiteral("pairing sender must include a SHA-256 certificate fingerprint"));
    return std::nullopt;
  }
  return sender;
}

bool validateSender(const DeviceInfo &sender, QByteArray &encoded, QString *errorMessage)
{
  if (sender.certificateFingerprintSha256.size() != kSha256Bytes) {
    setError(errorMessage, QStringLiteral("pairing sender must include a SHA-256 certificate fingerprint"));
    return false;
  }
  encoded = DeviceInfoCodec::serialize(sender, errorMessage);
  return !encoded.isEmpty();
}

PairingMessageType messageType(const PairingMessage &message)
{
  if (std::holds_alternative<PairingRequest>(message)) {
    return PairingMessageType::Request;
  }
  if (std::holds_alternative<PairingCodeSubmission>(message)) {
    return PairingMessageType::CodeSubmission;
  }
  return PairingMessageType::Result;
}

} // namespace

QByteArray PairingMessageCodec::encode(const PairingMessage &message, QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }

  QCborMap payload;
  bool valid = std::visit(
      [&](const auto &typedMessage) {
        using T = std::decay_t<decltype(typedMessage)>;
        if (typedMessage.pairingSessionId.isNull()) {
          setError(errorMessage, QStringLiteral("pairing session ID is null"));
          return false;
        }
        payload.insert(key(SessionIdKey), typedMessage.pairingSessionId.toRfc4122());

        if constexpr (std::is_same_v<T, PairingRequest>) {
          QByteArray sender;
          if (!validateSender(typedMessage.sender, sender, errorMessage)) {
            return false;
          }
          const QDateTime expiry = typedMessage.expiresAtUtc.toUTC();
          if (!expiry.isValid()) {
            setError(errorMessage, QStringLiteral("pairing expiry is invalid"));
            return false;
          }
          payload.insert(key(SenderKey), sender);
          payload.insert(key(ValueKey), expiry.toMSecsSinceEpoch());
          return true;
        } else if constexpr (std::is_same_v<T, PairingCodeSubmission>) {
          QByteArray sender;
          if (!validateSender(typedMessage.sender, sender, errorMessage)) {
            return false;
          }
          if (!kSixDigitPattern.match(typedMessage.sixDigitSas).hasMatch()) {
            setError(errorMessage, QStringLiteral("pairing code must contain six digits"));
            return false;
          }
          payload.insert(key(SenderKey), sender);
          // This user-entered SAS is confirmation data only. TLS never derives
          // keys from it and the value is never persisted by this codec.
          payload.insert(key(ValueKey), typedMessage.sixDigitSas);
          return true;
        } else {
          if (!isKnownPairingFailureReason(typedMessage.failureReason)) {
            setError(errorMessage, QStringLiteral("pairing result failure reason is unknown"));
            return false;
          }
          const bool successful = typedMessage.failureReason == PairingFailureReason::None;
          if (typedMessage.accepted != successful) {
            setError(errorMessage, QStringLiteral("pairing result acceptance and failure reason disagree"));
            return false;
          }
          if (typedMessage.accepted && !typedMessage.diagnostic.isEmpty()) {
            setError(errorMessage, QStringLiteral("accepted pairing result cannot contain a diagnostic"));
            return false;
          }
          if (typedMessage.diagnostic.toUtf8().size() > kMaximumDiagnosticBytes) {
            setError(errorMessage, QStringLiteral("pairing result diagnostic exceeds the local limit"));
            return false;
          }
          payload.insert(key(SenderKey), typedMessage.accepted);
          payload.insert(key(ValueKey), static_cast<qint64>(typedMessage.failureReason));
          if (!typedMessage.diagnostic.isEmpty()) {
            payload.insert(key(DiagnosticKey), typedMessage.diagnostic);
          }
          return true;
        }
      },
      message
  );
  if (!valid) {
    return {};
  }

  const QCborMap envelope = {
      {key(ProtocolKey), QString::fromLatin1(kPairingProtocol)},
      {key(VersionKey), kPairingProtocolVersion},
      {key(MessageTypeKey), static_cast<qint64>(messageType(message))},
      {key(PayloadKey), payload},
  };
  const QByteArray encoded = QCborValue(envelope).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
  if (encoded.size() > kMaximumPairingMessageBytes) {
    setError(errorMessage, QStringLiteral("pairing message exceeds the local limit"));
    return {};
  }
  return encoded;
}

PairingMessageDecodeResult PairingMessageCodec::decode(QByteArrayView bytes)
{
  if (bytes.isEmpty()) {
    return failure(PairingMessageError::MalformedCbor, QStringLiteral("pairing message is empty"));
  }
  if (bytes.size() > kMaximumPairingMessageBytes) {
    return failure(PairingMessageError::TooLarge, QStringLiteral("pairing message exceeds the local limit"));
  }

  QCborParserError parserError;
  const auto root = QCborValue::fromCbor(bytes.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != bytes.size() || !root.isMap()) {
    return failure(PairingMessageError::MalformedCbor, QStringLiteral("pairing message is not one CBOR map"));
  }
  const auto envelope = root.toMap();
  if (!hasOnlyKeys(envelope, {ProtocolKey, VersionKey, MessageTypeKey, PayloadKey}, 4)) {
    return failure(PairingMessageError::InvalidEnvelope, QStringLiteral("pairing envelope fields are invalid"));
  }
  const auto protocol = valueFor(envelope, ProtocolKey);
  if (!protocol.isString() || protocol.toString() != QString::fromLatin1(kPairingProtocol)) {
    return failure(PairingMessageError::UnsupportedProtocol, QStringLiteral("unsupported pairing protocol"));
  }
  const auto version = valueFor(envelope, VersionKey);
  if (!version.isInteger() || version.toInteger() != kPairingProtocolVersion) {
    return failure(PairingMessageError::UnsupportedVersion, QStringLiteral("unsupported pairing protocol version"));
  }
  const auto typeValue = valueFor(envelope, MessageTypeKey);
  if (!typeValue.isInteger() || typeValue.toInteger() < static_cast<qint64>(PairingMessageType::Request) ||
      typeValue.toInteger() > static_cast<qint64>(PairingMessageType::Result)) {
    return failure(PairingMessageError::UnsupportedMessageType, QStringLiteral("unsupported pairing message type"));
  }
  const auto payloadValue = valueFor(envelope, PayloadKey);
  if (!payloadValue.isMap()) {
    return failure(PairingMessageError::InvalidPayload, QStringLiteral("pairing payload must be a map"));
  }
  const auto payload = payloadValue.toMap();
  const auto sessionId = readSessionId(payload);
  if (!sessionId.has_value()) {
    return failure(PairingMessageError::InvalidSessionId, QStringLiteral("pairing session ID is invalid"));
  }

  const auto type = static_cast<PairingMessageType>(typeValue.toInteger());
  if (type == PairingMessageType::Request) {
    if (!hasOnlyKeys(payload, {SessionIdKey, SenderKey, ValueKey}, 3)) {
      return failure(PairingMessageError::InvalidPayload, QStringLiteral("pairing request fields are invalid"));
    }
    QString diagnostic;
    const auto sender = readSender(payload, &diagnostic);
    if (!sender.has_value()) {
      const auto error = diagnostic.contains(QStringLiteral("fingerprint")) ? PairingMessageError::InvalidFingerprint
                                                                            : PairingMessageError::InvalidDeviceInfo;
      return failure(error, std::move(diagnostic));
    }
    const auto expiry = valueFor(payload, ValueKey);
    if (!expiry.isInteger() || expiry.toInteger() <= 0) {
      return failure(PairingMessageError::InvalidExpiry, QStringLiteral("pairing expiry must be epoch milliseconds"));
    }
    const auto expiresAtUtc = QDateTime::fromMSecsSinceEpoch(expiry.toInteger(), QTimeZone::UTC);
    if (!expiresAtUtc.isValid()) {
      return failure(
          PairingMessageError::InvalidExpiry, QStringLiteral("pairing expiry is outside the supported range")
      );
    }
    return {.message = PairingMessage(PairingRequest{*sessionId, *sender, expiresAtUtc})};
  }

  if (type == PairingMessageType::CodeSubmission) {
    if (!hasOnlyKeys(payload, {SessionIdKey, SenderKey, ValueKey}, 3)) {
      return failure(PairingMessageError::InvalidPayload, QStringLiteral("pairing submission fields are invalid"));
    }
    QString diagnostic;
    const auto sender = readSender(payload, &diagnostic);
    if (!sender.has_value()) {
      const auto error = diagnostic.contains(QStringLiteral("fingerprint")) ? PairingMessageError::InvalidFingerprint
                                                                            : PairingMessageError::InvalidDeviceInfo;
      return failure(error, std::move(diagnostic));
    }
    const auto sas = valueFor(payload, ValueKey);
    if (!sas.isString() || !kSixDigitPattern.match(sas.toString()).hasMatch()) {
      return failure(PairingMessageError::InvalidSas, QStringLiteral("pairing code must contain six digits"));
    }
    return {.message = PairingMessage(PairingCodeSubmission{*sessionId, *sender, sas.toString()})};
  }

  if (payload.size() != 3 && payload.size() != 4) {
    return failure(PairingMessageError::InvalidPayload, QStringLiteral("pairing result fields are invalid"));
  }
  const auto accepted = valueFor(payload, SenderKey);
  if (!accepted.isBool()) {
    return failure(PairingMessageError::InvalidResult, QStringLiteral("pairing result acceptance must be boolean"));
  }
  if (!hasOnlyKeys(payload, {SessionIdKey, SenderKey, ValueKey}, 3) &&
      !hasOnlyKeys(payload, {SessionIdKey, SenderKey, ValueKey, DiagnosticKey}, 4)) {
    return failure(PairingMessageError::InvalidResult, QStringLiteral("pairing result fields are invalid"));
  }
  const auto encodedReason = valueFor(payload, ValueKey);
  if (!encodedReason.isInteger() || encodedReason.toInteger() < 0 ||
      encodedReason.toInteger() > std::numeric_limits<quint32>::max()) {
    return failure(PairingMessageError::InvalidResult, QStringLiteral("pairing result failure reason is invalid"));
  }
  const auto reason = static_cast<PairingFailureReason>(encodedReason.toInteger());
  if (!isKnownPairingFailureReason(reason) ||
      accepted.toBool() != (reason == PairingFailureReason::None)) {
    return failure(
        PairingMessageError::InvalidResult,
        QStringLiteral("pairing result acceptance and failure reason are inconsistent")
    );
  }
  QString diagnostic;
  if (payload.contains(key(DiagnosticKey))) {
    const auto encodedDiagnostic = valueFor(payload, DiagnosticKey);
    if (!encodedDiagnostic.isString() || encodedDiagnostic.toString().isEmpty() ||
        encodedDiagnostic.toString().toUtf8().size() > kMaximumDiagnosticBytes || accepted.toBool()) {
      return failure(PairingMessageError::InvalidResult, QStringLiteral("pairing result diagnostic is invalid"));
    }
    diagnostic = encodedDiagnostic.toString();
  }
  return {
      .message = PairingMessage(PairingResultMessage{*sessionId, accepted.toBool(), reason, diagnostic})
  };
}

} // namespace deskflow::relaydesk
