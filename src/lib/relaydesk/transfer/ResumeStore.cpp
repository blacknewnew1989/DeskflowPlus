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
#include <QScopeGuard>
#include <QSet>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
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
  ResolvedTargetsKey = 8,
};

enum ResolvedTargetKey : qint64
{
  TargetFileIdKey = 1,
  TargetRelativePathKey = 2,
  TargetSizeKey = 3,
  TargetSha256Key = 4,
  TargetDecisionKey = 5,
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

struct NoFollowReadResult
{
  QByteArray bytes;
  ResumeStoreError error = ResumeStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ResumeStoreError::None;
  }
};

NoFollowReadResult readStateNoFollow(const QString &path, quint64 maximumBytes)
{
  if (maximumBytes == 0) {
    return {{}, ResumeStoreError::StateTooLarge, QStringLiteral("resume state size limit is zero")};
  }
#if defined(Q_OS_WIN)
  const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
  HANDLE handle = CreateFileW(
      nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr
  );
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return {
        {}, error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ResumeStoreError::NotFound
                                                                           : ResumeStoreError::OpenFailed,
        QStringLiteral("could not open resume state without following links (Windows error %1)").arg(error)
    };
  }
  const auto closeHandle = qScopeGuard([handle] { CloseHandle(handle); });
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return {{}, ResumeStoreError::InvalidPath, QStringLiteral("resume state is a link or non-regular file")};
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
      static_cast<quint64>(size.QuadPart) > maximumBytes ||
      static_cast<quint64>(size.QuadPart) > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    return {{}, ResumeStoreError::StateTooLarge, QStringLiteral("resume state exceeds the limit")};
  }
  QByteArray bytes(static_cast<qsizetype>(size.QuadPart), Qt::Uninitialized);
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    const DWORD requested = static_cast<DWORD>(
        std::min<quint64>(static_cast<quint64>(bytes.size() - offset), std::numeric_limits<DWORD>::max())
    );
    DWORD read = 0;
    if (!ReadFile(handle, bytes.data() + offset, requested, &read, nullptr) || read == 0) {
      return {{}, ResumeStoreError::ReadFailed, QStringLiteral("could not read resume state")};
    }
    offset += static_cast<qsizetype>(read);
  }
  return {.bytes = std::move(bytes)};
#else
  const QByteArray nativePath = QFile::encodeName(path);
  const int descriptor = ::open(nativePath.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) {
    return {
        {}, errno == ENOENT ? ResumeStoreError::NotFound
                            : errno == ELOOP ? ResumeStoreError::InvalidPath : ResumeStoreError::OpenFailed,
        QString::fromLocal8Bit(std::strerror(errno))
    };
  }
  const auto closeDescriptor = qScopeGuard([descriptor] { ::close(descriptor); });
  struct stat status {};
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
    return {{}, ResumeStoreError::InvalidPath, QStringLiteral("resume state is not a regular file")};
  }
  if (status.st_size < 0 || static_cast<quint64>(status.st_size) > maximumBytes ||
      static_cast<quint64>(status.st_size) > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    return {{}, ResumeStoreError::StateTooLarge, QStringLiteral("resume state exceeds the limit")};
  }
  QByteArray bytes(static_cast<qsizetype>(status.st_size), Qt::Uninitialized);
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    const auto read = ::read(descriptor, bytes.data() + offset, static_cast<size_t>(bytes.size() - offset));
    if (read <= 0) {
      return {{}, ResumeStoreError::ReadFailed, QString::fromLocal8Bit(std::strerror(errno))};
    }
    offset += static_cast<qsizetype>(read);
  }
  return {.bytes = std::move(bytes)};
#endif
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

bool isResolvedDecision(IncomingConflictDecision decision)
{
  return decision == IncomingConflictDecision::AutoRename || decision == IncomingConflictDecision::Overwrite ||
         decision == IncomingConflictDecision::Skip;
}

ResumeStoreOperationResult validateState(const ResumeState &state, const ResumeStoreLimits &limits)
{
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
  if (limits.maximumFiles == 0 ||
      static_cast<quint64>(state.files.size() + state.resolvedTargets.size()) > limits.maximumFiles) {
    return operationFailure(ResumeStoreError::TooManyFiles, QStringLiteral("resume file count exceeds the limit"));
  }

  QSet<QByteArray> fileIds;
  QSet<QString> protocolPaths;
  QSet<QString> partPaths;
  QSet<QString> resolvedPaths;
  for (const auto &file : state.files) {
    const QByteArray fileId = file.fileId.toBytes();
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
  for (const auto &target : state.resolvedTargets) {
    const QByteArray fileId = target.fileId.toBytes();
    if (fileIds.contains(fileId) || !isSupportedUnsignedInteger(target.size) || target.sha256.size() != kSha256Bytes ||
        !isResolvedDecision(target.decision)) {
      return operationFailure(
          ResumeStoreError::InvalidState, QStringLiteral("resolved target identity or content binding is invalid")
      );
    }
    fileIds.insert(fileId);
    if (target.decision == IncomingConflictDecision::Skip) {
      if (!target.relativeTargetPath.isEmpty()) {
        return operationFailure(
            ResumeStoreError::InvalidPath, QStringLiteral("skipped target must not contain a path")
        );
      }
      continue;
    }
    const auto targetPath = PathPolicy::validateRelative(target.relativeTargetPath, limits.pathLimits);
    if (!targetPath.ok || targetPath.normalized != target.relativeTargetPath) {
      return operationFailure(
          ResumeStoreError::InvalidPath,
          targetPath.ok ? QStringLiteral("resolved target path is not NFC-normalized") : targetPath.diagnostic
      );
    }
    if (resolvedPaths.contains(targetPath.collisionKey)) {
      return operationFailure(
          ResumeStoreError::InvalidPath, QStringLiteral("resume contains colliding resolved target paths")
      );
    }
    resolvedPaths.insert(targetPath.collisionKey);
  }
  return {};
}

QByteArray encodeState(const ResumeState &state)
{
  QCborArray files;
  for (const auto &file : state.files) {
    files.append(QCborMap{
        {key(FileIdKey), file.fileId.toBytes()},
        {key(ProtocolPathKey), file.relativeProtocolPath},
        {key(DurableOffsetKey), static_cast<qint64>(file.durableOffset)},
        {key(TotalBytesKey), static_cast<qint64>(file.totalBytes)},
        {key(PartRelativePathKey), file.partRelativePath},
    });
  }
  QCborArray resolvedTargets;
  for (const auto &target : state.resolvedTargets) {
    resolvedTargets.append(QCborMap{
        {key(TargetFileIdKey), target.fileId.toBytes()},
        {key(TargetRelativePathKey), target.relativeTargetPath},
        {key(TargetSizeKey), static_cast<qint64>(target.size)},
        {key(TargetSha256Key), target.sha256},
        {key(TargetDecisionKey), static_cast<qint64>(target.decision)},
    });
  }

  const QCborMap map = {
      {key(SchemaVersionKey), static_cast<qint64>(kResumeStateSchemaVersion)},
      {key(TransferIdKey), state.transferId.toBytes()},
      {key(PeerDeviceIdKey), state.peerDeviceId.toBytes()},
      {key(ManifestHashKey), state.manifestSha256},
      {key(DirectionKey), directionName(state.direction)},
      {key(FilesKey), files},
      {key(UpdatedAtKey), state.updatedUtc.toMSecsSinceEpoch()},
      {key(ResolvedTargetsKey), resolvedTargets},
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
  const auto schemaVersion = valueFor(map, SchemaVersionKey);
  if (!schemaVersion.isInteger()) {
    return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume state schema field is invalid"));
  }
  const qint64 schema = schemaVersion.toInteger();
  if (schema != static_cast<qint64>(kLegacyResumeStateSchemaVersion) &&
      schema != static_cast<qint64>(kResumeStateSchemaVersion)) {
    return loadFailure(ResumeStoreError::UnsupportedSchema, QStringLiteral("resume state schema is unsupported"));
  }
  const bool legacy = schema == static_cast<qint64>(kLegacyResumeStateSchemaVersion);
  const bool exactFields = legacy
                               ? hasExactIntegerKeys(
                                     map,
                                     {SchemaVersionKey, TransferIdKey, PeerDeviceIdKey, ManifestHashKey, DirectionKey,
                                      FilesKey, UpdatedAtKey}
                                 )
                               : hasExactIntegerKeys(
                                     map,
                                     {SchemaVersionKey, TransferIdKey, PeerDeviceIdKey, ManifestHashKey, DirectionKey,
                                      FilesKey, UpdatedAtKey, ResolvedTargetsKey}
                                 );
  if (!exactFields) {
    return loadFailure(
        ResumeStoreError::InvalidFields, QStringLiteral("resume state contains missing, duplicate, or unknown fields")
    );
  }

  const auto transferBytes = valueFor(map, TransferIdKey);
  const auto peerBytes = valueFor(map, PeerDeviceIdKey);
  const auto manifestHash = valueFor(map, ManifestHashKey);
  const auto directionValue = valueFor(map, DirectionKey);
  const auto filesValue = valueFor(map, FilesKey);
  const auto updatedAtValue = valueFor(map, UpdatedAtKey);
  const auto targetsValue = legacy ? QCborValue(QCborArray{}) : valueFor(map, ResolvedTargetsKey);
  if (!transferBytes.isByteArray() || transferBytes.toByteArray().size() != kUuidBytes || !peerBytes.isByteArray() ||
      !manifestHash.isByteArray() || manifestHash.toByteArray().size() != kSha256Bytes || !directionValue.isString() ||
      !filesValue.isArray() || !updatedAtValue.isInteger() || updatedAtValue.toInteger() <= 0 ||
      !targetsValue.isArray()) {
    return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume state contains invalid field types"));
  }

  const auto transferId = TransferId::fromBytes(transferBytes.toByteArray());
  const auto peerDeviceId = deskflow::relaydesk::DeviceId::fromBytes(peerBytes.toByteArray());
  const auto direction = directionFromName(directionValue.toString());
  if (!transferId.has_value() || !peerDeviceId.has_value() || !direction.has_value()) {
    return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume identity or direction is invalid"));
  }

  const auto encodedFiles = filesValue.toArray();
  const auto encodedTargets = targetsValue.toArray();
  if (limits.maximumFiles == 0 ||
      static_cast<quint64>(encodedFiles.size() + encodedTargets.size()) > limits.maximumFiles) {
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
    const auto fileId = FileId::fromBytes(fileBytes.toByteArray());
    if (!fileId.has_value()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resume file ID is invalid"));
    }
    files.append({
        .fileId = *fileId,
        .relativeProtocolPath = protocolPath.toString(),
        .durableOffset = static_cast<quint64>(durableOffset.toInteger()),
        .totalBytes = static_cast<quint64>(totalBytes.toInteger()),
        .partRelativePath = partPath.toString(),
    });
  }

  QList<ResolvedTargetState> resolvedTargets;
  resolvedTargets.reserve(encodedTargets.size());
  for (const auto &encodedTarget : encodedTargets) {
    if (!encodedTarget.isMap()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resolved target entry is not a map"));
    }
    const auto targetMap = encodedTarget.toMap();
    if (!hasExactIntegerKeys(
            targetMap,
            {TargetFileIdKey, TargetRelativePathKey, TargetSizeKey, TargetSha256Key, TargetDecisionKey}
        )) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resolved target fields are invalid"));
    }
    const auto fileBytes = valueFor(targetMap, TargetFileIdKey);
    const auto relativePath = valueFor(targetMap, TargetRelativePathKey);
    const auto size = valueFor(targetMap, TargetSizeKey);
    const auto sha256 = valueFor(targetMap, TargetSha256Key);
    const auto decision = valueFor(targetMap, TargetDecisionKey);
    if (!fileBytes.isByteArray() || fileBytes.toByteArray().size() != kUuidBytes || !relativePath.isString() ||
        !size.isInteger() || size.toInteger() < 0 || !sha256.isByteArray() ||
        sha256.toByteArray().size() != kSha256Bytes || !decision.isInteger() ||
        decision.toInteger() < static_cast<qint64>(IncomingConflictDecision::Overwrite) ||
        decision.toInteger() > static_cast<qint64>(IncomingConflictDecision::CancelTransfer)) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resolved target types are invalid"));
    }
    const auto fileId = FileId::fromBytes(fileBytes.toByteArray());
    if (!fileId.has_value()) {
      return loadFailure(ResumeStoreError::InvalidFields, QStringLiteral("resolved target file ID is invalid"));
    }
    resolvedTargets.append({
        .fileId = *fileId,
        .relativeTargetPath = relativePath.toString(),
        .size = static_cast<quint64>(size.toInteger()),
        .sha256 = sha256.toByteArray(),
        .decision = static_cast<IncomingConflictDecision>(decision.toInteger()),
    });
  }

  ResumeState state{
      .transferId = *transferId,
      .peerDeviceId = *peerDeviceId,
      .manifestSha256 = manifestHash.toByteArray(),
      .direction = *direction,
      .files = std::move(files),
      .resolvedTargets = std::move(resolvedTargets),
      .updatedUtc = QDateTime::fromMSecsSinceEpoch(updatedAtValue.toInteger(), Qt::UTC),
  };
  const auto validation = validateState(state, limits);
  if (!validation.ok()) {
    return loadFailure(validation.error, validation.diagnostic);
  }
  return {.state = std::move(state), .schemaVersion = static_cast<quint64>(schema)};
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
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory)) {
    return loadFailure(
        ResumeStoreError::InvalidStoreDirectory, QStringLiteral("resume store path or transfer ID is invalid")
    );
  }
  auto read = readStateNoFollow(statePath(transferId), m_limits.maximumEncodedBytes);
  if (!read.ok()) {
    return loadFailure(read.error, std::move(read.diagnostic));
  }
  auto decoded = decodeState(read.bytes, m_limits);
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
    const auto transferId = TransferId::fromString(stem);
    if (!transferId.has_value() || transferId->toString().compare(stem, Qt::CaseInsensitive) != 0) {
      result.issues.append({
          .path = entry.absoluteFilePath(),
          .error = ResumeStoreError::InvalidFields,
          .diagnostic = QStringLiteral("resume filename is not a canonical transfer ID"),
      });
      continue;
    }
    auto loaded = load(*transferId);
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
  if (m_activeDirectory.isEmpty() || !QDir::isAbsolutePath(m_activeDirectory)) {
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
  const QString fileName = transferId.toString() + QStringLiteral(".resume.cbor");
  return QDir(m_activeDirectory).absoluteFilePath(fileName);
}

} // namespace relaydesk::transfer
