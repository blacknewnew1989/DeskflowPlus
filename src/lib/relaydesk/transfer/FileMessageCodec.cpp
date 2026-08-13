// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileMessageCodec.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QSet>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr qsizetype kMaximumDiagnosticUtf8Bytes = 4096;
constexpr quint32 kMaximumChunkBytes = 4U * 1024U * 1024U;

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

FileMessageDecodeResult failure(FileMessageCodecError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool wireInteger(quint64 value)
{
  return value <= static_cast<quint64>(std::numeric_limits<qint64>::max());
}

bool knownResultCode(FileResultCode code)
{
  switch (code) {
  case FileResultCode::Ok:
  case FileResultCode::HashMismatch:
  case FileResultCode::SizeMismatch:
  case FileResultCode::SourceChanged:
  case FileResultCode::TargetExists:
  case FileResultCode::DiskFull:
  case FileResultCode::PermissionDenied:
  case FileResultCode::PathInvalid:
  case FileResultCode::IoError:
  case FileResultCode::Cancelled:
    return true;
  }
  return false;
}

bool validate(const FileBeginMessage &message, QString *error)
{
  if (!wireInteger(message.size) || !wireInteger(message.startOffset) || message.startOffset > message.size) {
    setError(error, QStringLiteral("FILE_BEGIN size and offset are invalid"));
  } else if (message.chunkBytes == 0 || message.chunkBytes > kMaximumChunkBytes) {
    setError(error, QStringLiteral("FILE_BEGIN chunk bytes exceed the RDFT hard limit"));
  } else if (message.expectedSha256.size() != kSha256Bytes) {
    setError(error, QStringLiteral("FILE_BEGIN expected hash must be SHA-256"));
  } else {
    return true;
  }
  return false;
}

bool validate(const FileChunkMessage &message, QString *error)
{
  if (!wireInteger(message.offset) || !wireInteger(message.sequence)) {
    setError(error, QStringLiteral("FILE_CHUNK offset or sequence exceeds the wire integer range"));
  } else {
    return true;
  }
  return false;
}

bool validate(const FileCheckpointMessage &message, QString *error)
{
  if (!wireInteger(message.durableOffset)) {
    setError(error, QStringLiteral("FILE_CHECKPOINT durable offset exceeds the wire integer range"));
  } else {
    return true;
  }
  return false;
}

bool validate(const FileEndMessage &message, QString *error)
{
  if (!wireInteger(message.size)) {
    setError(error, QStringLiteral("FILE_END size exceeds the wire integer range"));
  } else if (message.sha256.size() != kSha256Bytes) {
    setError(error, QStringLiteral("FILE_END hash must be SHA-256"));
  } else {
    return true;
  }
  return false;
}

bool validate(const FileResultMessage &message, QString *error)
{
  if (!knownResultCode(message.code)) {
    setError(error, QStringLiteral("FILE_RESULT code is unknown"));
  } else if (message.code == FileResultCode::Ok && !message.diagnostic.isEmpty()) {
    setError(error, QStringLiteral("successful FILE_RESULT cannot contain a diagnostic"));
  } else if (message.code != FileResultCode::Ok &&
             (message.diagnostic.isEmpty() || message.diagnostic.toUtf8().size() > kMaximumDiagnosticUtf8Bytes)) {
    setError(error, QStringLiteral("failed FILE_RESULT requires a bounded diagnostic"));
  } else {
    return true;
  }
  return false;
}

template <typename Id> void insertUuid(QCborMap &map, qint64 field, const Id &value)
{
  map.insert(key(field), value.toBytes());
}

void insertUnsigned(QCborMap &map, qint64 field, quint64 value)
{
  map.insert(key(field), static_cast<qint64>(value));
}

bool hasExactKeys(const QCborMap &map, const QSet<qint64> &expected)
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

template <typename Id> std::optional<Id> readUuid(const QCborMap &map, qint64 field)
{
  const auto value = valueFor(map, field);
  if (!value.isByteArray() || value.toByteArray().size() != kUuidBytes) {
    return std::nullopt;
  }
  return Id::fromBytes(value.toByteArray());
}

std::optional<quint64> readUnsigned(const QCborMap &map, qint64 field)
{
  const auto value = valueFor(map, field);
  if (!value.isInteger() || value.toInteger() < 0) {
    return std::nullopt;
  }
  return static_cast<quint64>(value.toInteger());
}

FileMessageCodecError idError(const QCborMap &map)
{
  return readUuid<TransferId>(map, 1).has_value() ? FileMessageCodecError::InvalidFileId
                                                  : FileMessageCodecError::InvalidTransferId;
}

} // namespace

MessageType fileMessageType(const FileControlMessage &message) noexcept
{
  if (std::holds_alternative<FileBeginMessage>(message)) {
    return MessageType::FileBegin;
  }
  if (std::holds_alternative<FileChunkMessage>(message)) {
    return MessageType::FileChunk;
  }
  if (std::holds_alternative<FileCheckpointMessage>(message)) {
    return MessageType::FileCheckpoint;
  }
  if (std::holds_alternative<FileEndMessage>(message)) {
    return MessageType::FileEnd;
  }
  return MessageType::FileResult;
}

QByteArray FileMessageCodec::encode(const FileControlMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  QCborMap map;
  const bool valid = std::visit(
      [&](const auto &typed) {
        if (!validate(typed, error)) {
          return false;
        }
        insertUuid(map, 1, typed.transferId);
        insertUuid(map, 2, typed.fileId);
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, FileBeginMessage>) {
          insertUnsigned(map, 3, typed.size);
          insertUnsigned(map, 4, typed.startOffset);
          insertUnsigned(map, 5, typed.chunkBytes);
          map.insert(key(6), typed.expectedSha256);
        } else if constexpr (std::is_same_v<T, FileChunkMessage>) {
          insertUnsigned(map, 3, typed.offset);
          insertUnsigned(map, 4, typed.sequence);
        } else if constexpr (std::is_same_v<T, FileCheckpointMessage>) {
          insertUnsigned(map, 3, typed.durableOffset);
        } else if constexpr (std::is_same_v<T, FileEndMessage>) {
          insertUnsigned(map, 3, typed.size);
          map.insert(key(4), typed.sha256);
        } else {
          insertUnsigned(map, 3, static_cast<quint32>(typed.code));
          if (!typed.diagnostic.isEmpty()) {
            map.insert(key(4), typed.diagnostic);
          }
        }
        return true;
      },
      message
  );
  return valid ? QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps) : QByteArray{};
}

FileMessageDecodeResult FileMessageCodec::decode(MessageType type, QByteArrayView metadata)
{
  if (type != MessageType::FileBegin && type != MessageType::FileChunk && type != MessageType::FileCheckpoint &&
      type != MessageType::FileEnd && type != MessageType::FileResult) {
    return failure(
        FileMessageCodecError::UnsupportedMessageType, QStringLiteral("metadata is not a file control message")
    );
  }
  QCborParserError parserError;
  const auto root = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (metadata.isEmpty() || parserError.error != QCborError::NoError || parserError.offset != metadata.size() ||
      !root.isMap()) {
    return failure(FileMessageCodecError::MalformedCbor, QStringLiteral("file control metadata is not one CBOR map"));
  }
  const auto map = root.toMap();

  QSet<qint64> expected{1, 2};
  switch (type) {
  case MessageType::FileBegin:
    expected.unite({3, 4, 5, 6});
    break;
  case MessageType::FileChunk:
  case MessageType::FileEnd:
    expected.unite({3, 4});
    break;
  case MessageType::FileCheckpoint:
    expected.insert(3);
    break;
  case MessageType::FileResult:
    expected.insert(3);
    if (map.contains(key(4))) {
      expected.insert(4);
    }
    break;
  default:
    break;
  }
  if (!hasExactKeys(map, expected)) {
    return failure(FileMessageCodecError::InvalidFields, QStringLiteral("file control fields are missing or unknown"));
  }
  const auto transferId = readUuid<TransferId>(map, 1);
  const auto fileId = readUuid<FileId>(map, 2);
  if (!transferId.has_value() || !fileId.has_value()) {
    return failure(idError(map), QStringLiteral("file control identifier is invalid"));
  }

  if (type == MessageType::FileBegin) {
    const auto size = readUnsigned(map, 3);
    const auto offset = readUnsigned(map, 4);
    const auto chunkBytes = readUnsigned(map, 5);
    const auto hash = valueFor(map, 6);
    if (!size.has_value() || !offset.has_value() || *offset > *size) {
      return failure(FileMessageCodecError::InvalidInteger, QStringLiteral("FILE_BEGIN size or offset is invalid"));
    }
    if (!chunkBytes.has_value() || *chunkBytes == 0 || *chunkBytes > kMaximumChunkBytes) {
      return failure(FileMessageCodecError::InvalidChunkSize, QStringLiteral("FILE_BEGIN chunk size is invalid"));
    }
    if (!hash.isByteArray() || hash.toByteArray().size() != kSha256Bytes) {
      return failure(FileMessageCodecError::InvalidHash, QStringLiteral("FILE_BEGIN hash is invalid"));
    }
    return {
        .message = FileControlMessage(FileBeginMessage{
            *transferId, *fileId, *size, *offset, static_cast<quint32>(*chunkBytes), hash.toByteArray()
        })
    };
  }
  if (type == MessageType::FileChunk) {
    const auto offset = readUnsigned(map, 3);
    const auto sequence = readUnsigned(map, 4);
    if (!offset.has_value() || !sequence.has_value()) {
      return failure(FileMessageCodecError::InvalidInteger, QStringLiteral("FILE_CHUNK position is invalid"));
    }
    return {.message = FileControlMessage(FileChunkMessage{*transferId, *fileId, *offset, *sequence})};
  }
  if (type == MessageType::FileCheckpoint) {
    const auto offset = readUnsigned(map, 3);
    if (!offset.has_value()) {
      return failure(FileMessageCodecError::InvalidInteger, QStringLiteral("FILE_CHECKPOINT offset is invalid"));
    }
    return {.message = FileControlMessage(FileCheckpointMessage{*transferId, *fileId, *offset})};
  }
  if (type == MessageType::FileEnd) {
    const auto size = readUnsigned(map, 3);
    const auto hash = valueFor(map, 4);
    if (!size.has_value()) {
      return failure(FileMessageCodecError::InvalidInteger, QStringLiteral("FILE_END size is invalid"));
    }
    if (!hash.isByteArray() || hash.toByteArray().size() != kSha256Bytes) {
      return failure(FileMessageCodecError::InvalidHash, QStringLiteral("FILE_END hash is invalid"));
    }
    return {.message = FileControlMessage(FileEndMessage{*transferId, *fileId, *size, hash.toByteArray()})};
  }

  const auto code = readUnsigned(map, 3);
  if (!code.has_value() || *code > static_cast<quint32>(FileResultCode::Cancelled) ||
      !knownResultCode(static_cast<FileResultCode>(*code))) {
    return failure(FileMessageCodecError::InvalidResult, QStringLiteral("FILE_RESULT code is invalid"));
  }
  QString diagnostic;
  if (map.contains(key(4))) {
    const auto value = valueFor(map, 4);
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().toUtf8().size() > kMaximumDiagnosticUtf8Bytes) {
      return failure(FileMessageCodecError::InvalidResult, QStringLiteral("FILE_RESULT diagnostic is invalid"));
    }
    diagnostic = value.toString();
  }
  const auto resultCode = static_cast<FileResultCode>(*code);
  if ((resultCode == FileResultCode::Ok) != diagnostic.isEmpty()) {
    return failure(FileMessageCodecError::InvalidResult, QStringLiteral("FILE_RESULT status and diagnostic disagree"));
  }
  return {.message = FileControlMessage(FileResultMessage{*transferId, *fileId, resultCode, diagnostic})};
}

} // namespace relaydesk::transfer
