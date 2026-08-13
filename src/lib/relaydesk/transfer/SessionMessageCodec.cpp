// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/SessionMessageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QSet>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

enum HelloKey : qint64
{
  DeviceIdKey = 1,
  SessionIdKey = 2,
  AppVersionKey = 3,
  SupportedVersionsKey = 4,
  CertificateFingerprintKey = 5,
  TimestampKey = 6,
};

enum AuthResultKey : qint64
{
  AcceptedKey = 1,
  ErrorCodeKey = 2,
  DiagnosticKey = 3,
};

enum HeartbeatKey : qint64
{
  HeartbeatSequenceKey = 1,
  HeartbeatTimestampKey = 2,
};

enum GoodbyeKey : qint64
{
  GoodbyeReasonKey = 1,
  GoodbyeDiagnosticKey = 2,
};

constexpr qsizetype kMaximumAppVersionUtf8Bytes = 64;
constexpr qsizetype kMaximumSupportedVersions = 16;
constexpr qsizetype kMaximumDiagnosticUtf8Bytes = 512;
constexpr quint64 kMaximumSessionMetadataBytes = 1U * 1024U * 1024U;

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

void setError(QString *error, const QString &diagnostic)
{
  if (error != nullptr) {
    *error = diagnostic;
  }
}

template <typename Message> SessionMessageDecodeResult<Message> failure(SessionMessageError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool hasExactIntegerKeys(const QCborMap &map, const QSet<qint64> &expected)
{
  if (map.size() != expected.size()) {
    return false;
  }
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || !expected.contains(iterator.key().toInteger())) {
      return false;
    }
  }
  return true;
}

std::optional<QCborMap> parseMap(QByteArrayView metadata)
{
  if (metadata.isEmpty()) {
    return std::nullopt;
  }
  QCborParserError parserError;
  const auto value = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != metadata.size() || !value.isMap()) {
    return std::nullopt;
  }
  return value.toMap();
}

std::optional<QList<quint16>> readVersions(const QCborValue &value)
{
  if (!value.isArray()) {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (array.isEmpty() || array.size() > kMaximumSupportedVersions) {
    return std::nullopt;
  }
  QList<quint16> versions;
  QSet<qint64> unique;
  for (const auto &entry : array) {
    if (!entry.isInteger() || entry.toInteger() <= 0 || entry.toInteger() > std::numeric_limits<quint16>::max() ||
        unique.contains(entry.toInteger())) {
      return std::nullopt;
    }
    unique.insert(entry.toInteger());
    versions.append(static_cast<quint16>(entry.toInteger()));
  }
  return versions;
}

bool validateVersions(const QList<quint16> &versions, QString *error)
{
  if (versions.isEmpty() || versions.size() > kMaximumSupportedVersions) {
    setError(error, QStringLiteral("HELLO supported versions must contain between 1 and 16 entries"));
    return false;
  }
  QSet<quint16> unique;
  for (const auto version : versions) {
    if (version == 0 || unique.contains(version)) {
      setError(error, QStringLiteral("HELLO supported versions must be non-zero and unique"));
      return false;
    }
    unique.insert(version);
  }
  return true;
}

bool knownGoodbyeReason(GoodbyeReason reason)
{
  switch (reason) {
  case GoodbyeReason::Normal:
  case GoodbyeReason::ApplicationShutdown:
  case GoodbyeReason::ProtocolError:
  case GoodbyeReason::IdleTimeout:
    return true;
  }
  return false;
}

} // namespace

QByteArray SessionMessageCodec::encodeHello(const HelloMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (message.deviceId.value().isNull()) {
    setError(error, QStringLiteral("HELLO device ID is null"));
    return {};
  }
  if (message.sessionId.isNull()) {
    setError(error, QStringLiteral("HELLO session ID is null"));
    return {};
  }
  if (message.appVersion.isEmpty() || message.appVersion.toUtf8().size() > kMaximumAppVersionUtf8Bytes) {
    setError(error, QStringLiteral("HELLO app version is empty or exceeds 64 UTF-8 bytes"));
    return {};
  }
  if (!validateVersions(message.supportedMajorVersions, error)) {
    return {};
  }
  if (message.certificateFingerprintSha256.size() != kSha256Bytes) {
    setError(error, QStringLiteral("HELLO certificate fingerprint must be SHA-256"));
    return {};
  }
  if (message.timestampMs == 0 || message.timestampMs > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    setError(error, QStringLiteral("HELLO timestamp is outside the supported range"));
    return {};
  }

  QCborArray versions;
  for (const auto version : message.supportedMajorVersions) {
    versions.append(version);
  }
  const QCborMap map = {
      {key(DeviceIdKey), message.deviceId.toBytes()},
      {key(SessionIdKey), message.sessionId.toRfc4122()},
      {key(AppVersionKey), message.appVersion},
      {key(SupportedVersionsKey), versions},
      {key(CertificateFingerprintKey), message.certificateFingerprintSha256},
      {key(TimestampKey), static_cast<qint64>(message.timestampMs)},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

HelloDecodeResult SessionMessageCodec::decodeHello(MessageType type, QByteArrayView metadata)
{
  if (type != MessageType::Hello) {
    return failure<HelloMessage>(
        SessionMessageError::UnsupportedMessageType, QStringLiteral("session metadata is not a HELLO message")
    );
  }
  const auto parsed = parseMap(metadata);
  if (!parsed.has_value()) {
    return failure<HelloMessage>(SessionMessageError::MalformedCbor, QStringLiteral("HELLO is not one CBOR map"));
  }
  const auto &map = *parsed;
  if (!hasExactIntegerKeys(
          map, {DeviceIdKey, SessionIdKey, AppVersionKey, SupportedVersionsKey, CertificateFingerprintKey, TimestampKey}
      )) {
    return failure<HelloMessage>(
        SessionMessageError::InvalidFields, QStringLiteral("HELLO contains missing, duplicate, or unknown fields")
    );
  }

  const auto deviceBytes = valueFor(map, DeviceIdKey);
  if (!deviceBytes.isByteArray()) {
    return failure<HelloMessage>(SessionMessageError::InvalidDeviceId, QStringLiteral("HELLO device ID is invalid"));
  }
  const auto deviceId = deskflow::relaydesk::DeviceId::fromBytes(deviceBytes.toByteArray());
  if (!deviceId.has_value()) {
    return failure<HelloMessage>(SessionMessageError::InvalidDeviceId, QStringLiteral("HELLO device ID is invalid"));
  }

  const auto sessionBytes = valueFor(map, SessionIdKey);
  if (!sessionBytes.isByteArray() || sessionBytes.toByteArray().size() != kUuidBytes) {
    return failure<HelloMessage>(SessionMessageError::InvalidSessionId, QStringLiteral("HELLO session ID is invalid"));
  }
  const auto sessionId = QUuid::fromRfc4122(sessionBytes.toByteArray());
  if (sessionId.isNull()) {
    return failure<HelloMessage>(SessionMessageError::InvalidSessionId, QStringLiteral("HELLO session ID is invalid"));
  }

  const auto appVersion = valueFor(map, AppVersionKey);
  if (!appVersion.isString() || appVersion.toString().isEmpty() ||
      appVersion.toString().toUtf8().size() > kMaximumAppVersionUtf8Bytes) {
    return failure<HelloMessage>(
        SessionMessageError::InvalidAppVersion, QStringLiteral("HELLO app version is empty or too long")
    );
  }
  const auto versions = readVersions(valueFor(map, SupportedVersionsKey));
  if (!versions.has_value()) {
    return failure<HelloMessage>(
        SessionMessageError::InvalidVersions, QStringLiteral("HELLO supported versions are invalid")
    );
  }
  const auto fingerprint = valueFor(map, CertificateFingerprintKey);
  if (!fingerprint.isByteArray() || fingerprint.toByteArray().size() != kSha256Bytes) {
    return failure<HelloMessage>(
        SessionMessageError::InvalidFingerprint, QStringLiteral("HELLO certificate fingerprint is invalid")
    );
  }
  const auto timestamp = valueFor(map, TimestampKey);
  if (!timestamp.isInteger() || timestamp.toInteger() <= 0) {
    return failure<HelloMessage>(
        SessionMessageError::InvalidTimestamp, QStringLiteral("HELLO timestamp is outside the supported range")
    );
  }

  return {
      .message =
          HelloMessage{
              .deviceId = *deviceId,
              .sessionId = sessionId,
              .appVersion = appVersion.toString(),
              .supportedMajorVersions = *versions,
              .certificateFingerprintSha256 = fingerprint.toByteArray(),
              .timestampMs = static_cast<quint64>(timestamp.toInteger()),
          }
  };
}

QByteArray SessionMessageCodec::encodeAuthResult(const AuthResultMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  QCborMap map{{key(AcceptedKey), message.accepted}};
  if (message.accepted) {
    if (message.errorCode != AuthResultErrorCode::None || !message.diagnostic.isEmpty()) {
      setError(error, QStringLiteral("accepted AUTH_RESULT cannot contain an error"));
      return {};
    }
  } else {
    if (!isKnownAuthResultErrorCode(message.errorCode) || message.diagnostic.isEmpty() ||
        message.diagnostic.toUtf8().size() > kMaximumDiagnosticUtf8Bytes) {
      setError(error, QStringLiteral("rejected AUTH_RESULT requires a known bounded error"));
      return {};
    }
    map.insert(key(ErrorCodeKey), static_cast<quint32>(message.errorCode));
    map.insert(key(DiagnosticKey), message.diagnostic);
  }
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

AuthResultDecodeResult SessionMessageCodec::decodeAuthResult(MessageType type, QByteArrayView metadata)
{
  if (type != MessageType::AuthResult) {
    return failure<AuthResultMessage>(
        SessionMessageError::UnsupportedMessageType, QStringLiteral("session metadata is not an AUTH_RESULT message")
    );
  }
  const auto parsed = parseMap(metadata);
  if (!parsed.has_value()) {
    return failure<AuthResultMessage>(
        SessionMessageError::MalformedCbor, QStringLiteral("AUTH_RESULT is not one CBOR map")
    );
  }
  const auto &map = *parsed;
  const auto accepted = valueFor(map, AcceptedKey);
  if (!accepted.isBool()) {
    return failure<AuthResultMessage>(
        SessionMessageError::InvalidAuthResult, QStringLiteral("AUTH_RESULT acceptance is not boolean")
    );
  }
  if (accepted.toBool()) {
    if (!hasExactIntegerKeys(map, {AcceptedKey})) {
      return failure<AuthResultMessage>(
          SessionMessageError::InvalidAuthResult, QStringLiteral("accepted AUTH_RESULT contains extra fields")
      );
    }
    return {.message = AuthResultMessage{.accepted = true}};
  }

  if (!hasExactIntegerKeys(map, {AcceptedKey, ErrorCodeKey, DiagnosticKey})) {
    return failure<AuthResultMessage>(
        SessionMessageError::InvalidAuthResult, QStringLiteral("rejected AUTH_RESULT is missing error fields")
    );
  }
  const auto errorCode = valueFor(map, ErrorCodeKey);
  const auto diagnostic = valueFor(map, DiagnosticKey);
  if (!errorCode.isInteger() || errorCode.toInteger() <= 0 ||
      errorCode.toInteger() > std::numeric_limits<quint32>::max()) {
    return failure<AuthResultMessage>(
        SessionMessageError::InvalidAuthResult, QStringLiteral("rejected AUTH_RESULT contains an invalid error")
    );
  }
  const auto typedErrorCode = static_cast<AuthResultErrorCode>(static_cast<quint32>(errorCode.toInteger()));
  if (!isKnownAuthResultErrorCode(typedErrorCode) || !diagnostic.isString() || diagnostic.toString().isEmpty() ||
      diagnostic.toString().toUtf8().size() > kMaximumDiagnosticUtf8Bytes) {
    return failure<AuthResultMessage>(
        SessionMessageError::InvalidAuthResult, QStringLiteral("rejected AUTH_RESULT contains an invalid error")
    );
  }
  return {
      .message =
          AuthResultMessage{
              .accepted = false,
              .errorCode = typedErrorCode,
              .diagnostic = diagnostic.toString(),
          }
  };
}

QByteArray SessionMessageCodec::encodeHeartbeat(MessageType type, const HeartbeatMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (type != MessageType::Heartbeat && type != MessageType::HeartbeatAck) {
    setError(error, QStringLiteral("heartbeat type must be HEARTBEAT or HEARTBEAT_ACK"));
    return {};
  }
  if (message.sequence > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    setError(error, QStringLiteral("heartbeat sequence is outside the supported range"));
    return {};
  }
  if (message.timestampMs == 0 || message.timestampMs > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    setError(error, QStringLiteral("heartbeat timestamp is outside the supported range"));
    return {};
  }

  const QCborMap map = {
      {key(HeartbeatSequenceKey), static_cast<qint64>(message.sequence)},
      {key(HeartbeatTimestampKey), static_cast<qint64>(message.timestampMs)},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

HeartbeatDecodeResult
SessionMessageCodec::decodeHeartbeat(quint16 protocolVersion, MessageType type, QByteArrayView metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return failure<HeartbeatMessage>(
        SessionMessageError::UnsupportedVersion, QStringLiteral("heartbeat version is unsupported")
    );
  }
  if (type != MessageType::Heartbeat && type != MessageType::HeartbeatAck) {
    return failure<HeartbeatMessage>(
        SessionMessageError::UnsupportedMessageType,
        QStringLiteral("session metadata is not a HEARTBEAT or HEARTBEAT_ACK message")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaximumSessionMetadataBytes) {
    return failure<HeartbeatMessage>(
        SessionMessageError::TooLarge, QStringLiteral("heartbeat metadata is empty or too large")
    );
  }
  const auto parsed = parseMap(metadata);
  if (!parsed.has_value()) {
    return failure<HeartbeatMessage>(
        SessionMessageError::MalformedCbor, QStringLiteral("heartbeat metadata is not one CBOR map")
    );
  }
  const auto &map = *parsed;
  if (!hasExactIntegerKeys(map, {HeartbeatSequenceKey, HeartbeatTimestampKey})) {
    return failure<HeartbeatMessage>(
        SessionMessageError::InvalidFields,
        QStringLiteral("heartbeat contains missing, duplicate, or unknown fields")
    );
  }
  const auto sequence = valueFor(map, HeartbeatSequenceKey);
  if (!sequence.isInteger() || sequence.toInteger() < 0) {
    return failure<HeartbeatMessage>(
        SessionMessageError::InvalidSequence, QStringLiteral("heartbeat sequence is outside the supported range")
    );
  }
  const auto timestamp = valueFor(map, HeartbeatTimestampKey);
  if (!timestamp.isInteger() || timestamp.toInteger() <= 0) {
    return failure<HeartbeatMessage>(
        SessionMessageError::InvalidTimestamp, QStringLiteral("heartbeat timestamp is outside the supported range")
    );
  }
  return {
      .message =
          HeartbeatMessage{
              .sequence = static_cast<quint64>(sequence.toInteger()),
              .timestampMs = static_cast<quint64>(timestamp.toInteger()),
          },
  };
}

QByteArray SessionMessageCodec::encodeGoodbye(const GoodbyeMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (!knownGoodbyeReason(message.reason)) {
    setError(error, QStringLiteral("GOODBYE reason is unknown"));
    return {};
  }
  const bool protocolError = message.reason == GoodbyeReason::ProtocolError;
  const qsizetype diagnosticBytes = message.diagnostic.toUtf8().size();
  if ((protocolError &&
       (message.diagnostic.isEmpty() || diagnosticBytes > kMaximumDiagnosticUtf8Bytes)) ||
      (!protocolError && !message.diagnostic.isEmpty())) {
    setError(error, QStringLiteral("GOODBYE reason and diagnostic disagree"));
    return {};
  }

  QCborMap map{{key(GoodbyeReasonKey), static_cast<qint64>(message.reason)}};
  if (protocolError) {
    map.insert(key(GoodbyeDiagnosticKey), message.diagnostic);
  }
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

GoodbyeDecodeResult
SessionMessageCodec::decodeGoodbye(quint16 protocolVersion, MessageType type, QByteArrayView metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return failure<GoodbyeMessage>(
        SessionMessageError::UnsupportedVersion, QStringLiteral("GOODBYE version is unsupported")
    );
  }
  if (type != MessageType::Goodbye) {
    return failure<GoodbyeMessage>(
        SessionMessageError::UnsupportedMessageType, QStringLiteral("session metadata is not a GOODBYE message")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaximumSessionMetadataBytes) {
    return failure<GoodbyeMessage>(
        SessionMessageError::TooLarge, QStringLiteral("GOODBYE metadata is empty or too large")
    );
  }
  const auto parsed = parseMap(metadata);
  if (!parsed.has_value()) {
    return failure<GoodbyeMessage>(
        SessionMessageError::MalformedCbor, QStringLiteral("GOODBYE metadata is not one CBOR map")
    );
  }
  const auto &map = *parsed;
  if (!hasExactIntegerKeys(map, {GoodbyeReasonKey}) &&
      !hasExactIntegerKeys(map, {GoodbyeReasonKey, GoodbyeDiagnosticKey})) {
    return failure<GoodbyeMessage>(
        SessionMessageError::InvalidFields,
        QStringLiteral("GOODBYE contains missing, duplicate, or unknown fields")
    );
  }

  const QCborValue encodedReason = valueFor(map, GoodbyeReasonKey);
  if (!encodedReason.isInteger() || encodedReason.toInteger() < 0 ||
      encodedReason.toInteger() > std::numeric_limits<quint32>::max() ||
      !knownGoodbyeReason(static_cast<GoodbyeReason>(encodedReason.toInteger()))) {
    return failure<GoodbyeMessage>(
        SessionMessageError::InvalidGoodbyeReason, QStringLiteral("GOODBYE reason is unknown")
    );
  }
  const auto reason = static_cast<GoodbyeReason>(encodedReason.toInteger());
  QString diagnostic;
  if (map.contains(key(GoodbyeDiagnosticKey))) {
    const QCborValue encodedDiagnostic = valueFor(map, GoodbyeDiagnosticKey);
    if (!encodedDiagnostic.isString() || encodedDiagnostic.toString().isEmpty() ||
        encodedDiagnostic.toString().toUtf8().size() > kMaximumDiagnosticUtf8Bytes) {
      return failure<GoodbyeMessage>(
          SessionMessageError::InvalidDiagnostic, QStringLiteral("GOODBYE diagnostic is invalid")
      );
    }
    diagnostic = encodedDiagnostic.toString();
  }
  if ((reason == GoodbyeReason::ProtocolError) != !diagnostic.isEmpty()) {
    return failure<GoodbyeMessage>(
        SessionMessageError::InvalidDiagnostic, QStringLiteral("GOODBYE reason and diagnostic disagree")
    );
  }
  return {.message = GoodbyeMessage{.reason = reason, .diagnostic = diagnostic}};
}

} // namespace relaydesk::transfer
