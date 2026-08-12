// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ManifestBuilder.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

ManifestBuildResult fail(ManifestBuildError error, QString diagnostic, PathError pathError = PathError::None)
{
  return {
      .manifest = std::nullopt,
      .error = error,
      .pathError = pathError,
      .diagnostic = std::move(diagnostic),
  };
}

ManifestEntryKind classifyEntry(const QFileInfo &info)
{
  if (info.isSymLink()) {
    return ManifestEntryKind::SymbolicLink;
  }
  if (info.isFile()) {
    return ManifestEntryKind::RegularFile;
  }
  if (info.isDir()) {
    return ManifestEntryKind::Directory;
  }
  return ManifestEntryKind::Special;
}

void insertInteger(QCborMap &map, qint64 key, quint64 value)
{
  map.insert(QCborValue(key), QCborValue(static_cast<qint64>(value)));
}

QByteArray canonicalManifestBytes(const ManifestEntry &entry)
{
  QCborMap entryMap;
  entryMap.insert(QCborValue(1), QCborValue(entry.id.toRfc4122()));
  entryMap.insert(QCborValue(2), QCborValue(entry.relativeProtocolPath));
  insertInteger(entryMap, 3, static_cast<quint8>(entry.type));
  insertInteger(entryMap, 4, entry.size);
  insertInteger(entryMap, 5, static_cast<quint64>(entry.modifiedUtc.toMSecsSinceEpoch()));
  entryMap.insert(QCborValue(6), QCborValue(entry.sha256));
  insertInteger(entryMap, 7, entry.flags);

  QCborArray entries;
  entries.append(entryMap);
  return QCborValue(entries).toCbor(QCborValue::SortKeysInMaps);
}

bool sourceSnapshotChanged(const QFileInfo &initial, const QFileInfo &final, quint64 bytesHashed)
{
  if (!final.exists() || final.isSymLink() || !final.isFile() || initial.size() < 0 || final.size() < 0) {
    return true;
  }
  return final.size() != initial.size() || bytesHashed != static_cast<quint64>(initial.size()) ||
         final.lastModified().toMSecsSinceEpoch() != initial.lastModified().toMSecsSinceEpoch();
}

} // namespace

ManifestBuildResult
ManifestBuilder::buildSingleFile(const SingleFileManifestRequest &request, const ManifestBuildOptions &options)
{
  if (request.transferId.isNull()) {
    return fail(ManifestBuildError::InvalidTransferId, QStringLiteral("transferId must not be null"));
  }
  if (request.fileId.isNull()) {
    return fail(ManifestBuildError::InvalidFileId, QStringLiteral("fileId must not be null"));
  }
  if (options.hashChunkBytes == 0 || options.hashChunkBytes > kMaxManifestHashChunkBytes ||
      options.hashChunkBytes > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    return fail(ManifestBuildError::InvalidOptions, QStringLiteral("hash chunk size must be between 1 byte and 4 MiB"));
  }

  QFileInfo sourceInfo(request.sourcePath);
  sourceInfo.refresh();
  if (!sourceInfo.exists()) {
    return fail(ManifestBuildError::SourceNotFound, QStringLiteral("source file does not exist"));
  }

  const ManifestEntryKind kind = classifyEntry(sourceInfo);
  const EntryPolicyResult policy = PathPolicy::entryPolicy(kind);
  if (policy.disposition == EntryDisposition::Skip) {
    return fail(ManifestBuildError::EntrySkipped, policy.diagnostic);
  }
  if (kind == ManifestEntryKind::Directory) {
    return fail(
        ManifestBuildError::DirectoryNotSupported,
        QStringLiteral("directory manifests are implemented by the later folder slice")
    );
  }

  const QString logicalName =
      request.relativeProtocolPath.isEmpty() ? sourceInfo.fileName() : request.relativeProtocolPath;
  const PathValidationResult path = PathPolicy::validateRelative(logicalName, options.pathLimits);
  if (!path.ok) {
    return fail(ManifestBuildError::UnsafeProtocolPath, path.diagnostic, path.error);
  }

  const QString canonicalSourcePath = sourceInfo.canonicalFilePath();
  if (canonicalSourcePath.isEmpty()) {
    return fail(ManifestBuildError::SourceNotFound, QStringLiteral("source file cannot be canonicalized"));
  }

  QFile source(canonicalSourcePath);
  if (!source.open(QIODevice::ReadOnly)) {
    return fail(
        ManifestBuildError::SourceOpenFailed,
        QStringLiteral("source file could not be opened: %1").arg(source.errorString())
    );
  }

  QFileInfo initialInfo(canonicalSourcePath);
  initialInfo.refresh();
  if (!initialInfo.exists() || initialInfo.isSymLink() || !initialInfo.isFile() || initialInfo.size() < 0) {
    return fail(ManifestBuildError::SourceChanged, QStringLiteral("source changed before hashing began"));
  }
  const qint64 modifiedAtMs = initialInfo.lastModified().toMSecsSinceEpoch();
  if (modifiedAtMs < 0) {
    return fail(
        ManifestBuildError::InvalidSourceTimestamp,
        QStringLiteral("source modification time cannot be represented by RDFT/1")
    );
  }

  const quint64 expectedBytes = static_cast<quint64>(initialInfo.size());
  QByteArray chunk(static_cast<qsizetype>(options.hashChunkBytes), Qt::Uninitialized);
  QCryptographicHash fileHash(QCryptographicHash::Sha256);
  quint64 bytesHashed = 0;
  while (true) {
    const qint64 bytesRead = source.read(chunk.data(), chunk.size());
    if (bytesRead < 0) {
      return fail(
          ManifestBuildError::SourceReadFailed,
          QStringLiteral("source file could not be read: %1").arg(source.errorString())
      );
    }
    if (bytesRead == 0) {
      break;
    }
    if (static_cast<quint64>(bytesRead) > std::numeric_limits<quint64>::max() - bytesHashed) {
      return fail(ManifestBuildError::SourceReadFailed, QStringLiteral("hashed byte count overflow"));
    }
    fileHash.addData(QByteArrayView(chunk.constData(), bytesRead));
    bytesHashed += static_cast<quint64>(bytesRead);
    if (options.progress) {
      options.progress({.bytesHashed = bytesHashed, .totalBytes = expectedBytes});
    }
  }
  source.close();

  QFileInfo finalInfo(canonicalSourcePath);
  finalInfo.refresh();
  if (sourceSnapshotChanged(initialInfo, finalInfo, bytesHashed)) {
    return fail(
        ManifestBuildError::SourceChanged, QStringLiteral("source size or modification time changed while hashing")
    );
  }

  ManifestEntry entry;
  entry.id = request.fileId;
  entry.relativeProtocolPath = path.normalized;
  entry.type = ManifestEntryType::File;
  entry.size = expectedBytes;
  entry.modifiedUtc = QDateTime::fromMSecsSinceEpoch(modifiedAtMs, QTimeZone::UTC);
  entry.sha256 = fileHash.result();
  entry.flags = 0;

  TransferManifestSummary summary;
  summary.id = request.transferId;
  summary.displayName = path.normalized;
  summary.totalBytes = expectedBytes;
  summary.fileCount = 1;
  summary.directoryCount = 0;
  const QByteArray canonicalBytes = canonicalManifestBytes(entry);
  summary.canonicalSha256 = QCryptographicHash::hash(QByteArrayView(canonicalBytes), QCryptographicHash::Sha256);

  SingleFileManifest manifest;
  manifest.canonicalSourcePath = canonicalSourcePath;
  manifest.protocolCollisionKey = path.collisionKey;
  manifest.entry = std::move(entry);
  manifest.summary = std::move(summary);
  return {.manifest = std::move(manifest)};
}

} // namespace relaydesk::transfer
