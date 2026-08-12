// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ResumeMessageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace relaydesk::transfer {
namespace {

enum MessageKey : qint64
{
  TransferIdKey = 1,
  ManifestHashKey = 2,
  FilesKey = 3,
};

enum FileKey : qint64
{
  FileIdKey = 1,
  DurableOffsetKey = 2,
};

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

bool hasRequiredIntegerKeys(const QCborMap &map, const QSet<qint64> &required)
{
  for (const qint64 requiredKey : required) {
    if (!map.contains(key(requiredKey))) {
      return false;
    }
  }
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || iterator.key().toInteger() < 0) {
      return false;
    }
  }
  return true;
}

void setError(QString *error, const QString &diagnostic)
{
  if (error != nullptr) {
    *error = diagnostic;
  }
}

ResumeMessageDecodeResult decodeFailure(ResumeMessageCodecError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

ResumeResponseBuildResult responseFailure(ResumeNegotiationError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

ResumePlanResult planFailure(ResumeNegotiationError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool validQuery(const ResumeQueryMessage &query)
{
  return !query.transferId.isNull() && query.manifestSha256.size() == kSha256Bytes;
}

bool validWireOffset(quint64 offset)
{
  return offset <= static_cast<quint64>(std::numeric_limits<qint64>::max());
}

bool strictlyOrdered(const QList<ResumeFileOffset> &files, QString *error)
{
  QByteArray previous;
  QSet<QByteArray> seen;
  for (const auto &file : files) {
    if (file.fileId.isNull()) {
      setError(error, QStringLiteral("resume response contains a null file ID"));
      return false;
    }
    if (!validWireOffset(file.durableOffset)) {
      setError(error, QStringLiteral("resume response offset exceeds the wire integer range"));
      return false;
    }
    const QByteArray current = file.fileId.toRfc4122();
    if (seen.contains(current)) {
      setError(error, QStringLiteral("resume response contains a duplicate file ID"));
      return false;
    }
    if (!previous.isEmpty() && previous >= current) {
      setError(error, QStringLiteral("resume response file IDs are not strictly ordered"));
      return false;
    }
    seen.insert(current);
    previous = current;
  }
  return true;
}

QByteArray encodeQuery(const ResumeQueryMessage &query, QString *error)
{
  if (!validQuery(query)) {
    setError(error, QStringLiteral("resume query requires a transfer ID and SHA-256 manifest hash"));
    return {};
  }
  const QCborMap map = {
      {key(TransferIdKey), query.transferId.toRfc4122()},
      {key(ManifestHashKey), query.manifestSha256},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

QByteArray encodeResponse(const ResumeResponseMessage &response, QString *error)
{
  if (response.transferId.isNull() || response.manifestSha256.size() != kSha256Bytes) {
    setError(error, QStringLiteral("resume response requires a transfer ID and SHA-256 manifest hash"));
    return {};
  }
  if (static_cast<quint64>(response.files.size()) > kMaximumResumeResponseFiles) {
    setError(error, QStringLiteral("resume response file count exceeds the limit"));
    return {};
  }
  if (!strictlyOrdered(response.files, error)) {
    return {};
  }

  QCborArray files;
  for (const auto &file : response.files) {
    files.append(QCborMap{
        {key(FileIdKey), file.fileId.toRfc4122()},
        {key(DurableOffsetKey), static_cast<qint64>(file.durableOffset)},
    });
  }
  const QCborMap map = {
      {key(TransferIdKey), response.transferId.toRfc4122()},
      {key(ManifestHashKey), response.manifestSha256},
      {key(FilesKey), files},
  };
  QByteArray encoded = QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
  if (static_cast<quint64>(encoded.size()) > kMaximumResumeMetadataBytes) {
    setError(error, QStringLiteral("resume response metadata exceeds the limit"));
    return {};
  }
  return encoded;
}

std::optional<QUuid> readUuid(const QCborValue &value)
{
  if (!value.isByteArray() || value.toByteArray().size() != kUuidBytes) {
    return std::nullopt;
  }
  const auto id = QUuid::fromRfc4122(value.toByteArray());
  return id.isNull() ? std::nullopt : std::optional<QUuid>{id};
}

} // namespace

QByteArray ResumeMessageCodec::encode(const ResumeControlMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  return std::visit(
      [error](const auto &typed) -> QByteArray {
        using Message = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Message, ResumeQueryMessage>) {
          return encodeQuery(typed, error);
        } else {
          return encodeResponse(typed, error);
        }
      },
      message
  );
}

ResumeMessageDecodeResult ResumeMessageCodec::decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return decodeFailure(ResumeMessageCodecError::UnsupportedVersion, QStringLiteral("resume version is unsupported"));
  }
  if (type != MessageType::ResumeQuery && type != MessageType::ResumeResponse) {
    return decodeFailure(
        ResumeMessageCodecError::UnsupportedMessageType, QStringLiteral("frame is not a resume message")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaximumResumeMetadataBytes) {
    return decodeFailure(ResumeMessageCodecError::TooLarge, QStringLiteral("resume metadata is empty or too large"));
  }

  QCborParserError parserError;
  const auto value = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != metadata.size() || !value.isMap()) {
    return decodeFailure(ResumeMessageCodecError::MalformedCbor, QStringLiteral("resume metadata is not one CBOR map"));
  }
  const auto map = value.toMap();
  const QSet<qint64> expected = type == MessageType::ResumeQuery
                                    ? QSet<qint64>{TransferIdKey, ManifestHashKey}
                                    : QSet<qint64>{TransferIdKey, ManifestHashKey, FilesKey};
  if (!hasRequiredIntegerKeys(map, expected)) {
    return decodeFailure(
        ResumeMessageCodecError::InvalidFields, QStringLiteral("resume metadata keys or required fields are invalid")
    );
  }

  const auto transferId = readUuid(valueFor(map, TransferIdKey));
  if (!transferId.has_value()) {
    return decodeFailure(ResumeMessageCodecError::InvalidTransferId, QStringLiteral("resume transfer ID is invalid"));
  }
  const auto manifestHash = valueFor(map, ManifestHashKey);
  if (!manifestHash.isByteArray() || manifestHash.toByteArray().size() != kSha256Bytes) {
    return decodeFailure(
        ResumeMessageCodecError::InvalidManifestHash, QStringLiteral("resume manifest hash is invalid")
    );
  }
  if (type == MessageType::ResumeQuery) {
    return {
        .message =
            ResumeQueryMessage{
                .transferId = *transferId,
                .manifestSha256 = manifestHash.toByteArray(),
            },
    };
  }

  const auto filesValue = valueFor(map, FilesKey);
  if (!filesValue.isArray()) {
    return decodeFailure(ResumeMessageCodecError::InvalidFields, QStringLiteral("resume offsets are not an array"));
  }
  const auto encodedFiles = filesValue.toArray();
  if (static_cast<quint64>(encodedFiles.size()) > kMaximumResumeResponseFiles) {
    return decodeFailure(ResumeMessageCodecError::TooManyFiles, QStringLiteral("resume file count exceeds the limit"));
  }

  QList<ResumeFileOffset> files;
  files.reserve(encodedFiles.size());
  QByteArray previous;
  QSet<QByteArray> seen;
  for (const auto &encodedFile : encodedFiles) {
    if (!encodedFile.isMap() || !hasRequiredIntegerKeys(encodedFile.toMap(), {FileIdKey, DurableOffsetKey})) {
      return decodeFailure(ResumeMessageCodecError::InvalidFields, QStringLiteral("resume offset entry is invalid"));
    }
    const auto fileMap = encodedFile.toMap();
    const auto fileId = readUuid(valueFor(fileMap, FileIdKey));
    if (!fileId.has_value()) {
      return decodeFailure(ResumeMessageCodecError::InvalidFileId, QStringLiteral("resume file ID is invalid"));
    }
    const auto offset = valueFor(fileMap, DurableOffsetKey);
    if (!offset.isInteger() || offset.toInteger() < 0) {
      return decodeFailure(ResumeMessageCodecError::InvalidOffset, QStringLiteral("resume offset is invalid"));
    }
    const QByteArray current = fileId->toRfc4122();
    if (seen.contains(current)) {
      return decodeFailure(ResumeMessageCodecError::DuplicateFileId, QStringLiteral("resume file ID is duplicated"));
    }
    if (!previous.isEmpty() && previous >= current) {
      return decodeFailure(
          ResumeMessageCodecError::InvalidFileOrder, QStringLiteral("resume file IDs are not strictly ordered")
      );
    }
    seen.insert(current);
    previous = current;
    files.append({.fileId = *fileId, .durableOffset = static_cast<quint64>(offset.toInteger())});
  }
  return {
      .message =
          ResumeResponseMessage{
              .transferId = *transferId,
              .manifestSha256 = manifestHash.toByteArray(),
              .files = std::move(files),
          },
  };
}

ResumeResponseBuildResult ResumeNegotiator::buildResponse(const ResumeStore &store, const ResumeQueryMessage &query)
{
  if (!validQuery(query)) {
    return responseFailure(ResumeNegotiationError::InvalidQuery, QStringLiteral("resume query is invalid"));
  }
  const auto loaded = store.load(query.transferId);
  if (!loaded.ok()) {
    if (loaded.error == ResumeStoreError::NotFound) {
      return responseFailure(ResumeNegotiationError::StateNotFound, QStringLiteral("resume state does not exist"));
    }
    return responseFailure(
        ResumeNegotiationError::StoredStateInvalid,
        loaded.diagnostic.isEmpty() ? QStringLiteral("resume state is invalid") : loaded.diagnostic
    );
  }
  if (loaded.state->direction != ResumeDirection::Receiving) {
    return responseFailure(
        ResumeNegotiationError::DirectionMismatch, QStringLiteral("resume state is not receiver-owned")
    );
  }
  if (loaded.state->manifestSha256 != query.manifestSha256) {
    return responseFailure(
        ResumeNegotiationError::ManifestMismatch, QStringLiteral("resume manifest hash does not match stored state")
    );
  }

  QList<ResumeFileOffset> files;
  files.reserve(loaded.state->files.size());
  for (const auto &file : loaded.state->files) {
    files.append({.fileId = file.fileId, .durableOffset = file.durableOffset});
  }
  std::sort(files.begin(), files.end(), [](const ResumeFileOffset &left, const ResumeFileOffset &right) {
    return left.fileId.toRfc4122() < right.fileId.toRfc4122();
  });
  ResumeResponseMessage response{
      .transferId = query.transferId,
      .manifestSha256 = query.manifestSha256,
      .files = std::move(files),
  };
  QString encodingError;
  if (ResumeMessageCodec::encode(ResumeControlMessage{response}, &encodingError).isEmpty()) {
    return responseFailure(
        ResumeNegotiationError::ResponseTooLarge,
        encodingError.isEmpty() ? QStringLiteral("resume response exceeds protocol limits") : encodingError
    );
  }
  return {.response = std::move(response)};
}

ResumePlanResult ResumeNegotiator::validateResponse(
    const ResumeQueryMessage &query, const ResumeResponseMessage &response, const QList<ManifestEntry> &manifestEntries
)
{
  if (!validQuery(query) || response.transferId != query.transferId ||
      response.manifestSha256 != query.manifestSha256) {
    return planFailure(
        ResumeNegotiationError::ResponseMismatch, QStringLiteral("resume response is not bound to the query")
    );
  }
  QString orderingError;
  if (static_cast<quint64>(response.files.size()) > kMaximumResumeResponseFiles ||
      !strictlyOrdered(response.files, &orderingError)) {
    return planFailure(
        ResumeNegotiationError::DuplicateFile,
        orderingError.isEmpty() ? QStringLiteral("resume response file list is invalid") : orderingError
    );
  }

  QHash<QByteArray, quint64> sizes;
  for (const auto &entry : manifestEntries) {
    if (entry.type == ManifestEntryType::File && !entry.id.isNull()) {
      const QByteArray fileId = entry.id.toRfc4122();
      if (sizes.contains(fileId)) {
        return planFailure(
            ResumeNegotiationError::DuplicateFile, QStringLiteral("sender manifest contains duplicate file IDs")
        );
      }
      sizes.insert(fileId, entry.size);
    }
  }
  for (const auto &file : response.files) {
    const auto size = sizes.constFind(file.fileId.toRfc4122());
    if (size == sizes.cend()) {
      return planFailure(
          ResumeNegotiationError::UnknownFile, QStringLiteral("resume response references an unknown file")
      );
    }
    if (file.durableOffset > *size) {
      return planFailure(
          ResumeNegotiationError::OffsetOutOfRange, QStringLiteral("resume offset exceeds the manifest file size")
      );
    }
  }
  return {
      .plan =
          ResumePlan{
              .transferId = query.transferId,
              .files = response.files,
          },
  };
}

} // namespace relaydesk::transfer
