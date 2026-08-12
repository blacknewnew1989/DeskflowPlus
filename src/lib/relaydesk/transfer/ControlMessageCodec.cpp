// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ControlMessageCodec.h"

#include <QCborMap>
#include <QCborStreamReader>
#include <QCborValue>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

ControlMessageDecodeResult decodeError(ControlMessageError error, QString diagnostic)
{
  return {
      .message = std::nullopt,
      .error = error,
      .diagnostic = std::move(diagnostic),
  };
}

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

bool isValidString(const QString &value, bool allowEmpty = false)
{
  return (allowEmpty || !value.isEmpty()) && value.toUtf8().size() <= kMaxControlStringUtf8Bytes;
}

bool isValidUuid(const QUuid &value)
{
  return !value.isNull() && value.toRfc4122().size() == kUuidBytes;
}

bool isWireInteger(quint64 value)
{
  return value <= static_cast<quint64>(std::numeric_limits<qint64>::max());
}

QString conflictPolicyName(ConflictPolicy policy)
{
  switch (policy) {
  case ConflictPolicy::AutoRename:
    return QStringLiteral("auto-rename");
  case ConflictPolicy::Overwrite:
    return QStringLiteral("overwrite");
  case ConflictPolicy::Skip:
    return QStringLiteral("skip");
  case ConflictPolicy::Ask:
    return QStringLiteral("ask");
  }
  return {};
}

std::optional<ConflictPolicy> parseConflictPolicy(const QString &value)
{
  if (value == QStringLiteral("auto-rename")) {
    return ConflictPolicy::AutoRename;
  }
  if (value == QStringLiteral("overwrite")) {
    return ConflictPolicy::Overwrite;
  }
  if (value == QStringLiteral("skip")) {
    return ConflictPolicy::Skip;
  }
  if (value == QStringLiteral("ask")) {
    return ConflictPolicy::Ask;
  }
  return std::nullopt;
}

bool isKnownRejectReason(RejectReason reason)
{
  switch (reason) {
  case RejectReason::UserDeclined:
  case RejectReason::NotTrusted:
  case RejectReason::PolicyDenied:
  case RejectReason::InsufficientSpace:
  case RejectReason::TooManyFiles:
  case RejectReason::PathInvalid:
  case RejectReason::UnsupportedCapability:
  case RejectReason::Busy:
  case RejectReason::InternalError:
    return true;
  }
  return false;
}

bool validate(const TransferOffer &message, QString &error)
{
  if (!isValidUuid(message.transferId)) {
    error = QStringLiteral("transferId must be a non-null UUID");
  } else if (!isValidString(message.displayName)) {
    error = QStringLiteral("displayName must be non-empty and at most 4096 UTF-8 bytes");
  } else if (!isWireInteger(message.totalBytes) || !isWireInteger(message.fileCount) ||
             !isWireInteger(message.directoryCount) || !isWireInteger(message.manifestPageCount) ||
             !isWireInteger(message.createdAtMs)) {
    error = QStringLiteral("unsigned integer field exceeds the Qt CBOR integer range");
  } else if (message.manifestSha256.size() != kSha256Bytes) {
    error = QStringLiteral("manifestSha256 must contain 32 bytes");
  } else if (message.manifestPageCount == 0) {
    error = QStringLiteral("manifestPageCount must be greater than zero");
  } else {
    return true;
  }
  return false;
}

bool validate(const TransferAccept &message, QString &error)
{
  if (!isValidUuid(message.transferId)) {
    error = QStringLiteral("transferId must be a non-null UUID");
  } else if (!isValidString(message.logicalDestination)) {
    error = QStringLiteral("logicalDestination must be non-empty and at most 4096 UTF-8 bytes");
  } else if (!isWireInteger(message.freeBytes)) {
    error = QStringLiteral("freeBytes exceeds the Qt CBOR integer range");
  } else {
    return true;
  }
  return false;
}

bool validate(const TransferReject &message, QString &error)
{
  if (!isValidUuid(message.transferId)) {
    error = QStringLiteral("transferId must be a non-null UUID");
  } else if (!isKnownRejectReason(message.reason)) {
    error = QStringLiteral("reject reason is unsupported");
  } else if (!isValidString(message.diagnostic, true)) {
    error = QStringLiteral("diagnostic must be at most 4096 UTF-8 bytes");
  } else {
    return true;
  }
  return false;
}

bool validate(const ErrorMessage &message, QString &error)
{
  if (message.code == 0 || !isWireInteger(message.code)) {
    error = QStringLiteral("error code must be a positive CBOR integer");
  } else if (!isValidString(message.diagnostic)) {
    error = QStringLiteral("diagnostic must be non-empty and at most 4096 UTF-8 bytes");
  } else if (message.transferId.has_value() && !isValidUuid(*message.transferId)) {
    error = QStringLiteral("optional transferId must be a non-null UUID");
  } else if (message.fileId.has_value() && !isValidUuid(*message.fileId)) {
    error = QStringLiteral("optional fileId must be a non-null UUID");
  } else {
    return true;
  }
  return false;
}

void insertUuid(QCborMap &map, qint64 field, const QUuid &value)
{
  map.insert(key(field), QCborValue(value.toRfc4122()));
}

void insertUnsigned(QCborMap &map, qint64 field, quint64 value)
{
  map.insert(key(field), QCborValue(static_cast<qint64>(value)));
}

QByteArray encode(const TransferOffer &message)
{
  QCborMap map;
  insertUuid(map, 1, message.transferId);
  map.insert(key(2), QCborValue(message.displayName));
  insertUnsigned(map, 3, message.totalBytes);
  insertUnsigned(map, 4, message.fileCount);
  insertUnsigned(map, 5, message.directoryCount);
  map.insert(key(6), QCborValue(message.manifestSha256));
  insertUnsigned(map, 7, message.manifestPageCount);
  map.insert(key(8), QCborValue(conflictPolicyName(message.requestedConflictPolicy)));
  insertUnsigned(map, 9, message.createdAtMs);
  return QCborValue(map).toCbor();
}

QByteArray encode(const TransferAccept &message)
{
  QCborMap map;
  insertUuid(map, 1, message.transferId);
  map.insert(key(2), QCborValue(conflictPolicyName(message.effectiveConflictPolicy)));
  map.insert(key(3), QCborValue(message.logicalDestination));
  insertUnsigned(map, 4, message.freeBytes);
  map.insert(key(5), QCborValue(message.autoAccepted));
  return QCborValue(map).toCbor();
}

QByteArray encode(const TransferReject &message)
{
  QCborMap map;
  insertUuid(map, 1, message.transferId);
  insertUnsigned(map, 2, static_cast<quint32>(message.reason));
  if (!message.diagnostic.isEmpty()) {
    map.insert(key(3), QCborValue(message.diagnostic));
  }
  return QCborValue(map).toCbor();
}

QByteArray encode(const ErrorMessage &message)
{
  QCborMap map;
  insertUnsigned(map, 1, message.code);
  map.insert(key(2), QCborValue(message.diagnostic));
  map.insert(key(3), QCborValue(message.retryable));
  if (message.transferId.has_value()) {
    insertUuid(map, 4, *message.transferId);
  }
  if (message.fileId.has_value()) {
    insertUuid(map, 5, *message.fileId);
  }
  return QCborValue(map).toCbor();
}

ControlMessageDecodeResult validateMapKeys(const QCborMap &map)
{
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || iterator.key().toInteger() < 0) {
      return decodeError(
          ControlMessageError::NonIntegerKey, QStringLiteral("control metadata keys must be non-negative integers")
      );
    }
  }
  return {};
}

ControlMessageDecodeResult missingField(qint64 field)
{
  return decodeError(ControlMessageError::MissingField, QStringLiteral("required field %1 is missing").arg(field));
}

ControlMessageDecodeResult invalidType(qint64 field, const QString &expected)
{
  return decodeError(
      ControlMessageError::InvalidFieldType, QStringLiteral("field %1 must be %2").arg(field).arg(expected)
  );
}

ControlMessageDecodeResult invalidValue(qint64 field, const QString &reason)
{
  return decodeError(
      ControlMessageError::InvalidFieldValue, QStringLiteral("field %1 is invalid: %2").arg(field).arg(reason)
  );
}

std::optional<QCborValue> requiredValue(const QCborMap &map, qint64 field)
{
  const QCborValue fieldKey = key(field);
  if (!map.contains(fieldKey)) {
    return std::nullopt;
  }
  return map.value(fieldKey);
}

bool readUuid(const QCborMap &map, qint64 field, QUuid &output, ControlMessageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = missingField(field);
    return false;
  }
  if (!value->isByteArray()) {
    failure = invalidType(field, QStringLiteral("a 16-byte UUID byte string"));
    return false;
  }
  const QByteArray bytes = value->toByteArray();
  if (bytes.size() != kUuidBytes) {
    failure = invalidValue(field, QStringLiteral("UUID byte string must contain 16 bytes"));
    return false;
  }
  output = QUuid::fromRfc4122(bytes);
  if (output.isNull()) {
    failure = invalidValue(field, QStringLiteral("UUID must not be null"));
    return false;
  }
  return true;
}

bool readUnsigned(const QCborMap &map, qint64 field, quint64 &output, ControlMessageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = missingField(field);
    return false;
  }
  if (!value->isInteger()) {
    failure = invalidType(field, QStringLiteral("an unsigned integer"));
    return false;
  }
  const qint64 integer = value->toInteger();
  if (integer < 0) {
    failure = invalidValue(field, QStringLiteral("integer must not be negative"));
    return false;
  }
  output = static_cast<quint64>(integer);
  return true;
}

bool readString(const QCborMap &map, qint64 field, QString &output, ControlMessageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = missingField(field);
    return false;
  }
  if (!value->isString()) {
    failure = invalidType(field, QStringLiteral("a UTF-8 text string"));
    return false;
  }
  output = value->toString();
  if (!isValidString(output)) {
    failure = invalidValue(field, QStringLiteral("string must be non-empty and at most 4096 UTF-8 bytes"));
    return false;
  }
  return true;
}

bool readOptionalString(const QCborMap &map, qint64 field, QString &output, ControlMessageDecodeResult &failure)
{
  if (!map.contains(key(field))) {
    output.clear();
    return true;
  }
  const auto value = map.value(key(field));
  if (!value.isString()) {
    failure = invalidType(field, QStringLiteral("a UTF-8 text string"));
    return false;
  }
  output = value.toString();
  if (!isValidString(output)) {
    failure = invalidValue(field, QStringLiteral("string must be non-empty and at most 4096 UTF-8 bytes"));
    return false;
  }
  return true;
}

bool readBytes(
    const QCborMap &map, qint64 field, qsizetype expectedSize, QByteArray &output, ControlMessageDecodeResult &failure
)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = missingField(field);
    return false;
  }
  if (!value->isByteArray()) {
    failure = invalidType(field, QStringLiteral("a byte string"));
    return false;
  }
  output = value->toByteArray();
  if (output.size() != expectedSize) {
    failure = invalidValue(field, QStringLiteral("byte string has the wrong length"));
    return false;
  }
  return true;
}

bool readBool(const QCborMap &map, qint64 field, bool &output, ControlMessageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = missingField(field);
    return false;
  }
  if (!value->isBool()) {
    failure = invalidType(field, QStringLiteral("a boolean"));
    return false;
  }
  output = value->toBool();
  return true;
}

bool readConflictPolicy(const QCborMap &map, qint64 field, ConflictPolicy &output, ControlMessageDecodeResult &failure)
{
  QString name;
  if (!readString(map, field, name, failure)) {
    return false;
  }
  const auto policy = parseConflictPolicy(name);
  if (!policy.has_value()) {
    failure = invalidValue(field, QStringLiteral("unsupported conflict policy"));
    return false;
  }
  output = *policy;
  return true;
}

ControlMessageDecodeResult decodeTransferOffer(const QCborMap &map)
{
  TransferOffer message;
  ControlMessageDecodeResult failure;
  if (!readUuid(map, 1, message.transferId, failure) || !readString(map, 2, message.displayName, failure) ||
      !readUnsigned(map, 3, message.totalBytes, failure) || !readUnsigned(map, 4, message.fileCount, failure) ||
      !readUnsigned(map, 5, message.directoryCount, failure) ||
      !readBytes(map, 6, kSha256Bytes, message.manifestSha256, failure) ||
      !readUnsigned(map, 7, message.manifestPageCount, failure) ||
      !readConflictPolicy(map, 8, message.requestedConflictPolicy, failure) ||
      !readUnsigned(map, 9, message.createdAtMs, failure)) {
    return failure;
  }
  if (message.manifestPageCount == 0) {
    return invalidValue(7, QStringLiteral("manifest page count must be greater than zero"));
  }
  return {.message = ControlMessage(std::move(message))};
}

ControlMessageDecodeResult decodeTransferAccept(const QCborMap &map)
{
  TransferAccept message;
  ControlMessageDecodeResult failure;
  if (!readUuid(map, 1, message.transferId, failure) ||
      !readConflictPolicy(map, 2, message.effectiveConflictPolicy, failure) ||
      !readString(map, 3, message.logicalDestination, failure) || !readUnsigned(map, 4, message.freeBytes, failure) ||
      !readBool(map, 5, message.autoAccepted, failure)) {
    return failure;
  }
  return {.message = ControlMessage(std::move(message))};
}

ControlMessageDecodeResult decodeTransferReject(const QCborMap &map)
{
  TransferReject message;
  quint64 encodedReason = 0;
  ControlMessageDecodeResult failure;
  if (!readUuid(map, 1, message.transferId, failure) || !readUnsigned(map, 2, encodedReason, failure) ||
      !readOptionalString(map, 3, message.diagnostic, failure)) {
    return failure;
  }
  if (encodedReason > std::numeric_limits<quint32>::max()) {
    return invalidValue(2, QStringLiteral("reject reason exceeds uint32"));
  }
  message.reason = static_cast<RejectReason>(encodedReason);
  if (!isKnownRejectReason(message.reason)) {
    return invalidValue(2, QStringLiteral("unsupported reject reason"));
  }
  return {.message = ControlMessage(std::move(message))};
}

ControlMessageDecodeResult decodeProtocolError(const QCborMap &map)
{
  ErrorMessage message;
  ControlMessageDecodeResult failure;
  if (!readUnsigned(map, 1, message.code, failure) || !readString(map, 2, message.diagnostic, failure) ||
      !readBool(map, 3, message.retryable, failure)) {
    return failure;
  }
  if (message.code == 0) {
    return invalidValue(1, QStringLiteral("error code must be positive"));
  }

  if (map.contains(key(4))) {
    QUuid transferId;
    if (!readUuid(map, 4, transferId, failure)) {
      return failure;
    }
    message.transferId = transferId;
  }
  if (map.contains(key(5))) {
    QUuid fileId;
    if (!readUuid(map, 5, fileId, failure)) {
      return failure;
    }
    message.fileId = fileId;
  }
  return {.message = ControlMessage(std::move(message))};
}

} // namespace

QByteArray ControlMessageCodec::encode(quint16 protocolVersion, const ControlMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (protocolVersion != kProtocolMajorVersion) {
    if (error != nullptr) {
      *error = QStringLiteral("unsupported RDFT protocol version");
    }
    return {};
  }

  QString validationError;
  QByteArray encoded;
  std::visit(
      [&validationError, &encoded](const auto &typedMessage) {
        if (validate(typedMessage, validationError)) {
          encoded = ::relaydesk::transfer::encode(typedMessage);
        }
      },
      message
  );

  if (encoded.isEmpty() && error != nullptr) {
    *error = validationError.isEmpty() ? QStringLiteral("failed to encode control metadata") : validationError;
  }
  return encoded;
}

ControlMessageDecodeResult
ControlMessageCodec::decode(quint16 protocolVersion, MessageType type, const QByteArray &metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return decodeError(
        ControlMessageError::UnsupportedVersion,
        QStringLiteral("unsupported RDFT protocol version %1").arg(protocolVersion)
    );
  }
  if (type != MessageType::TransferOffer && type != MessageType::TransferAccept &&
      type != MessageType::TransferReject && type != MessageType::Error) {
    return decodeError(
        ControlMessageError::UnsupportedMessageType,
        QStringLiteral("message type is not implemented by this control metadata codec")
    );
  }

  QCborStreamReader reader(metadata);
  const QCborValue value = QCborValue::fromCbor(reader);
  if (reader.lastError() != QCborError::NoError) {
    return decodeError(
        ControlMessageError::MalformedCbor,
        QStringLiteral("malformed CBOR metadata at byte %1").arg(reader.currentOffset())
    );
  }
  if (reader.currentOffset() != metadata.size()) {
    return decodeError(ControlMessageError::MalformedCbor, QStringLiteral("trailing bytes after CBOR metadata map"));
  }
  if (!value.isMap()) {
    return decodeError(ControlMessageError::MetadataNotMap, QStringLiteral("control metadata must be one CBOR map"));
  }

  const QCborMap map = value.toMap();
  const auto keysResult = validateMapKeys(map);
  if (keysResult.error != ControlMessageError::None) {
    return keysResult;
  }

  switch (type) {
  case MessageType::TransferOffer:
    return decodeTransferOffer(map);
  case MessageType::TransferAccept:
    return decodeTransferAccept(map);
  case MessageType::TransferReject:
    return decodeTransferReject(map);
  case MessageType::Error:
    return decodeProtocolError(map);
  default:
    break;
  }
  return decodeError(ControlMessageError::UnsupportedMessageType, QStringLiteral("unsupported control message type"));
}

} // namespace relaydesk::transfer
