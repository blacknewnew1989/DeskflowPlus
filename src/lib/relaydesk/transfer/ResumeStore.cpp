// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ResumeStore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

enum StateKey : qint64
{
  SchemaVersionKey = 1,
  TransferIdKey = 2,
  PeerDeviceIdKey = 3,
  ManifestHashKey = 4,
  DirectionKey = 5,
  FilesKey = 6,
  UpdatedAtKey = 7,
};

enum FileKey : qint64
{
  FileIdKey = 1,
  ProtocolPathKey = 2,
  DurableOffsetKey = 3,
  TotalBytesKey = 4,
  PartRelativePathKey = 5,
};

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
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

ResumeStoreOperationResult operationFailure(ResumeStoreError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

ResumeStoreLoadResult loadFailure(ResumeStoreError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

QString directionName(ResumeDirection direction)
{
  switch (direction) {
  case ResumeDirection::Sending:
    return QStringLiteral("sending");
  case ResumeDirection::Receiving:
    return QStringLiteral("receiving");
  }
  return {};
}

std::optional<ResumeDirection> directionFromName(const QString &name)
{
  if (name == QStringLiteral("sending")) {
    return ResumeDirection::Sending;
  }
  if (name == QStringLiteral("receiving")) {
    return ResumeDirection::Receiving;
  }
  return std::nullopt;
}

bool isSupportedUnsignedInteger(quint64 value)
{
  return value <= static_cast<quint64>(std::numeric_limits<qint64>::max());
}

ResumeStoreOperationResult validateState(const ResumeState &state, const ResumeStoreLimits &limits)
{
  if (state.transferId.isNull()) {
    return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume transfer ID is null"));
  }
  if (state.peerDeviceId.value().isNull()) {
    return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume peer device ID is null"));
  }
  if (state.manifestSha256.size() != kSha256Bytes) {
    return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume manifest hash must be SHA-256"));
  }
  if (directionName(state.direction).isEmpty()) {
    return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume direction is invalid"));
  }
  if (!state.updatedUtc.isValid() || state.updatedUtc.toMSecsSinceEpoch() <= 0) {
    return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume update timestamp is invalid"));
  }
  if (limits.maximumFiles == 0 || static_cast<quint64>(state.files.size()) > limits.maximumFiles) {
    return operationFailure(ResumeStoreError::TooManyFiles, QStringLiteral("resume file count exceeds the limit"));
  }

  QSet<QByteArray> fileIds;
  QSet<QString> protocolPaths;
  QSet<QString> partPaths;
  for (const auto &file : state.files) {
    if (file.fileId.isNull()) {
      return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume file ID is null"));
    }
    const QByteArray fileId = file.fileId.toRfc4122();
    if (fileIds.contains(fileId)) {
      return operationFailure(ResumeStoreError::InvalidState, QStringLiteral("resume contains a duplicate file ID"));
    }
    fileIds.insert(fileId);

    const auto protocolPath = PathPolicy::validateRelative(file.relativeProtocolPath, limits.pathLimits);
    if (!protocolPath.ok || protocolPath.normalized != file.relativeProtocolPath) {
      return operationFailure(
          ResumeStoreError::InvalidPath,
          protocolPath.ok ? QStringLiteral("resume protocol path is not NFC-normalized") : protocolPath.diagnostic
      );
    }
    if (protocolPaths.contains(protocolPath.collisionKey)) {
      return operationFailure(
          ResumeStoreError::InvalidPath, QStringLiteral("resume contains colliding protocol paths")
      );
    }
    protocolPaths.insert(protocolPath.collisionKey);

    const auto partPath = PathPolicy::validateRelative(file.partRelativePath, limits.pathLimits);
    if (!partPath.ok || partPath.normalized != file.partRelativePath) {
      return operationFailure(
          ResumeStoreError::InvalidPath,
          partPath.ok ? QStringLiteral("resume partial path is not NFC-normalized") : partPath.diagnostic
      );
    }
    if (partPaths.contains(partPath.collisionKey)) {
      return operationFailure(ResumeStoreError::InvalidPath, QStringLiteral("resume contains colliding partial paths"));
    }
    partPaths.insert(partPath.collisionKey);

    if (file.durableOffset > file.totalBytes || !isSupportedUnsignedInteger(file.durableOffset) ||
        !isSupportedUnsignedInteger(file.totalBytes)) {
      return operationFailure(
          ResumeStoreError::InvalidState, QStringLiteral("resume durable offset or file size is invalid")
      );
    }
  }
  return {};
}

QByteArray encodeState(const ResumeState &state)
{
  QCborArray files;
  for (const auto &file : state.files) {
    files.append(QCborMap{
        {key(FileIdKey), file.fileId.toRfc4122()},
        {key(ProtocolPathKey), file.relativeProtocolPath},
        {key(DurableOffsetKey), static_cast<qint64>(file.durableOffset)},
        {key(TotalBytesKey), static_cast<qint64>(file.totalBytes)},
        {key(PartRelativePathKey), file.partRelativePath},
    });
  }

  const QCborMap map = {
      {key(SchemaVersionKey), static_cast<qint64>(kResumeStateSchemaVersion)},
      {key(TransferIdKey), state.transferId.toRfc4122()},
      {key(PeerDeviceIdKey), state.peerDeviceId.toBytes()},
      {key(ManifestHashKey), state.manifestSha256},
      {key(DirectionKey), directionName(state.direction)},
      {key(FilesKey), files},
      {key(UpdatedAtKey), state.updatedUtc.toMSecsSinceEpoch()},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

ResumeStoreLoadResult decodeState(QByteArrayView encoded, const ResumeStoreLimits &limits)
{
  if (encoded.isEmpty() || static_cast<quint64>(encoded.size()) > limits.maximumEncodedBytes) {
    return loadFailure(ResumeStoreError::StateTooLarge, QStringLiteral("resume state is empty or exceeds the limit"));
  }

  QCborParserError parserError;
  const auto value = QCborValue::fromCbor(encoded.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != encoded.size() || !value.isMap()) {
    return loadFailure(ResumeStoreError::MalformedCbor, QStringLiteral("resume state is not exactly one CBOR map"));
  }
  const auto map = value.toMap();
  if (!hasExactIntegerKeys(
          map, {SchemaVersionKey, TransferIdKey, PeerDeviceIdKey, ManifestHashKey, DirectionKey, FilesKey, UpdatedAtKey}
      )) {
    return loadFailure(
        ResumeStoreError::InvalidFields, QStringLiteral("resume state contains missing, duplicate, or unknown fields")
    );
  }

  const auto schemaVersion = valueFor(map, SchemaVersionKey);
  if (!schemaVersion.isInteger() || schemaVersion.toInteger() != static_cast<qint64>(kResumeStateSchemaVersion)) {
    return loadFailure(ResumeStoreError::UnsupportedSchema, QStringLiteral("resume state schema is unsupported"));
  }

  const auto transferBytes = valueFor(map, TransferIdKey);
  const auto peerBytes = valueFor(map, PeerDeviceIdKey);
  const auto manifestHash = valueFor(map, ManifestHashKey);
  const auto directionValue = valueFor(map, DirectionKey);
  const auto filesValue = valueFor(map, FilesKey);
  const auto updatedAtValue = valueFor(map, UpdatedAtKey);
  if (!transferBytes.isByteArray() || transferBytes.toByteArray().size() != kUuidBytes || !peerBytes.isByteArray() ||
      !manifestHash.isByteArray() || manifestHash.toByteArray().size() != kSha256Bytes || !directionValue.isString() ||
      !filesValue.isArray() || !updatedAtValue.isInteger() || updatedAtValue.toInteger() <= 0) {
    return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume state contains invalid field types"));
  }

  const auto transferId = QUuid::fromRfc4122(transferBytes.toByteArray());
  const auto peerDeviceId = deskflow::relaydesk::DeviceId::fromBytes(peerBytes.toByteArray());
  const auto direction = directionFromName(directionValue.toString());
  if (transferId.isNull() || !peerDeviceId.has_value() || !direction.has_value()) {
    return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume identity or direction is invalid"));
  }

  const auto encodedFiles = filesValue.toArray();
  if (limits.maximumFiles == 0 || static_cast<quint64>(encodedFiles.size()) > limits.maximumFiles) {
    return loadFailure(ResumeStoreError::TooManyFiles, QStringLiteral("resume file count exceeds the limit"));
  }
  QList<ResumeFileState> files;
  files.reserve(encodedFiles.size());
  for (const auto &encodedFile : encodedFiles) {
    if (!encodedFile.isMap()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume file entry is not a map"));
    }
    const auto fileMap = encodedFile.toMap();
    if (!hasExactIntegerKeys(
            fileMap, {FileIdKey, ProtocolPathKey, DurableOffsetKey, TotalBytesKey, PartRelativePathKey}
        )) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume file entry fields are invalid"));
    }
    const auto fileBytes = valueFor(fileMap, FileIdKey);
    const auto protocolPath = valueFor(fileMap, ProtocolPathKey);
    const auto durableOffset = valueFor(fileMap, DurableOffsetKey);
    const auto totalBytes = valueFor(fileMap, TotalBytesKey);
    const auto partPath = valueFor(fileMap, PartRelativePathKey);
    if (!fileBytes.isByteArray() || fileBytes.toByteArray().size() != kUuidBytes || !protocolPath.isString() ||
        !durableOffset.isInteger() || durableOffset.toInteger() < 0 || !totalBytes.isInteger() ||
        totalBytes.toInteger() < 0 || !partPath.isString()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume file entry types are invalid"));
    }
    const auto fileId = QUuid::fromRfc4122(fileBytes.toByteArray());
    if (fileId.isNull()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume file ID is invalid"));
    }
    files.append({
        .fileId = fileId,
        .relativeProtocolPath = protocolPath.toString(),
        .durableOffset = static_cast<quint64>(durableOffset.toInteger()),
        .totalBytes = static_cast<quint64>(totalBytes.toInteger()),
        .partRelativePath = partPath.toString(),
    });
  }

  ResumeState state{
      .transferId = transferId,
      .peerDeviceId = *peerDeviceId,
      .manifestSha256 = manifestHash.toByteArray(),
      .direction = *direction,
      .files = std::move(files),
      .updatedUtc = QDateTime::fromMSecsSinceEpoch(updatedAtValue.toInteger(), Qt::UTC),
  };
  const auto validation = validateState(state, limits);
  if (!validation.ok()) {
    return loadFailure(validation.error, validation.diagnostic);
  }
  return {.state = std::move(state)};
}

} // namespace

ResumeStore::ResumeStore(QString activeDirectory, ResumeStoreLimits limits)
    : m_activeDirectory(QDir::cleanPath(std::move(activeDirectory))),
      m_limits(std::move(limits))
{
}

ResumeStoreOperationResult ResumeStore::save(const ResumeState &state) const
{
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory)) {
    return operationFailure(
        ResumeStoreError::InvalidStoreDirectory, QStringLiteral("resume store directory must be absolute")
    );
  }
  const auto validation = validateState(state, m_limits);
  if (!validation.ok()) {
    return validation;
  }
  const QByteArray encoded = encodeState(state);
  if (m_limits.maximumEncodedBytes == 0 || static_cast<quint64>(encoded.size()) > m_limits.maximumEncodedBytes) {
    return operationFailure(ResumeStoreError::StateTooLarge, QStringLiteral("encoded resume state exceeds the limit"));
  }
  if (!QDir().mkpath(m_activeDirectory)) {
    return operationFailure(
        ResumeStoreError::DirectoryCreateFailed, QStringLiteral("could not create resume store directory")
    );
  }

  QSaveFile output(statePath(state.transferId));
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    return operationFailure(ResumeStoreError::OpenFailed, output.errorString());
  }
  if (output.write(encoded) != encoded.size()) {
    output.cancelWriting();
    return operationFailure(ResumeStoreError::WriteFailed, output.errorString());
  }
  if (!output.commit()) {
    return operationFailure(ResumeStoreError::CommitFailed, output.errorString());
  }
  return {};
}

ResumeStoreLoadResult ResumeStore::load(const TransferId &transferId) const
{
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory) || transferId.isNull()) {
    return loadFailure(
        ResumeStoreError::InvalidStoreDirectory, QStringLiteral("resume store path or transfer ID is invalid")
    );
  }
  QFile input(statePath(transferId));
  if (!input.exists()) {
    return loadFailure(ResumeStoreError::NotFound, QStringLiteral("resume state does not exist"));
  }
  if (!input.open(QIODevice::ReadOnly)) {
    return loadFailure(ResumeStoreError::OpenFailed, input.errorString());
  }
  if (m_limits.maximumEncodedBytes == 0 || input.size() < 0 ||
      static_cast<quint64>(input.size()) > m_limits.maximumEncodedBytes) {
    return loadFailure(ResumeStoreError::StateTooLarge, QStringLiteral("resume state exceeds the limit"));
  }
  const QByteArray encoded = input.readAll();
  if (input.error() != QFileDevice::NoError) {
    return loadFailure(ResumeStoreError::ReadFailed, input.errorString());
  }
  auto decoded = decodeState(encoded, m_limits);
  if (decoded.ok() && decoded.state->transferId != transferId) {
    return loadFailure(
        ResumeStoreError::TransferIdMismatch, QStringLiteral("resume state does not match its requested transfer ID")
    );
  }
  return decoded;
}

ResumeStoreScanResult ResumeStore::scan() const
{
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory)) {
    return {
        .error = ResumeStoreError::InvalidStoreDirectory,
        .diagnostic = QStringLiteral("resume store directory must be absolute"),
    };
  }
  const QDir directory(m_activeDirectory);
  if (!directory.exists()) {
    return {};
  }

  ResumeStoreScanResult result;
  const QFileInfoList entries = directory.entryInfoList(
      {QStringLiteral("*.resume.cbor")}, QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name
  );
  for (const auto &entry : entries) {
    QString stem = entry.fileName();
    stem.chop(QStringLiteral(".resume.cbor").size());
    const QUuid transferId(stem);
    if (transferId.isNull() || transferId.toString(QUuid::WithoutBraces).compare(stem, Qt::CaseInsensitive) != 0) {
      result.issues.append({
          .path = entry.absoluteFilePath(),
          .error = ResumeStoreError::InvalidFields,
          .diagnostic = QStringLiteral("resume filename is not a canonical transfer ID"),
      });
      continue;
    }
    auto loaded = load(transferId);
    if (loaded.ok()) {
      result.states.append(std::move(*loaded.state));
    } else {
      result.issues.append({
          .path = entry.absoluteFilePath(),
          .error = loaded.error,
          .diagnostic = std::move(loaded.diagnostic),
      });
    }
  }
  return result;
}

ResumeStoreOperationResult ResumeStore::remove(const TransferId &transferId) const
{
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory) || transferId.isNull()) {
    return operationFailure(
        ResumeStoreError::InvalidStoreDirectory, QStringLiteral("resume store path or transfer ID is invalid")
    );
  }
  const QString path = statePath(transferId);
  if (!QFileInfo::exists(path)) {
    return {};
  }
  QFile file(path);
  if (!file.remove()) {
    return operationFailure(ResumeStoreError::RemoveFailed, file.errorString());
  }
  return {};
}

QString ResumeStore::statePath(const TransferId &transferId) const
{
  const QString fileName = transferId.toString(QUuid::WithoutBraces) + QStringLiteral(".resume.cbor");
  return QDir(m_activeDirectory).absoluteFilePath(fileName);
}

} // namespace relaydesk::transfer
