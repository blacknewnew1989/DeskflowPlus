// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ManifestBuilder.h"
#include "relaydesk/transfer/ManifestPageCodec.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
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

TransferManifestBuildResult
failTransfer(ManifestBuildError error, QString diagnostic, PathError pathError = PathError::None)
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

bool sourceSnapshotChanged(const QFileInfo &initial, const QFileInfo &final, quint64 bytesHashed)
{
  if (!final.exists() || final.isSymLink() || !final.isFile() || initial.size() < 0 || final.size() < 0) {
    return true;
  }
  return final.size() != initial.size() || bytesHashed != static_cast<quint64>(initial.size()) ||
         final.lastModified().toMSecsSinceEpoch() != initial.lastModified().toMSecsSinceEpoch();
}

bool optionsAreValid(const ManifestBuildOptions &options)
{
  return options.hashChunkBytes > 0 && options.hashChunkBytes <= kMaxManifestHashChunkBytes &&
         options.hashChunkBytes <= static_cast<quint64>(std::numeric_limits<qsizetype>::max()) &&
         options.maxEntries > 0 && options.maxEntries <= kMaxManifestEntries && options.maxMetadataBytes > 0 &&
         options.maxMetadataBytes <= kMaxManifestMetadataBytes;
}

struct ChildIdentity
{
  QString name;
  ManifestEntryKind kind = ManifestEntryKind::Special;

  [[nodiscard]] bool operator==(const ChildIdentity &) const = default;
};

struct DirectorySnapshot
{
  QString canonicalPath;
  qint64 modifiedAtMs = 0;
  QList<ChildIdentity> children;
};

struct ScannedEntry
{
  PreparedManifestEntry prepared;
  QByteArray pathUtf8;
  qint64 size = 0;
  qint64 modifiedAtMs = 0;
  ManifestEntryKind kind = ManifestEntryKind::Special;
};

struct PendingEntry
{
  QString localPath;
  QString protocolPath;
};

bool childLess(const ChildIdentity &left, const ChildIdentity &right)
{
  const QByteArray leftBytes = left.name.toUtf8();
  const QByteArray rightBytes = right.name.toUtf8();
  if (leftBytes != rightBytes) {
    return leftBytes < rightBytes;
  }
  return static_cast<int>(left.kind) < static_cast<int>(right.kind);
}

enum class DirectoryListResult
{
  Ok,
  Unavailable,
  LimitExceeded,
};

DirectoryListResult listDirectory(
    const QString &canonicalPath, quint64 maxChildren, QList<QFileInfo> &children, QList<ChildIdentity> &identities
)
{
  const QFileInfo directoryInfo(canonicalPath);
  QDir directory(canonicalPath);
  if (!directoryInfo.exists() || !directoryInfo.isDir() || directoryInfo.isSymLink() || !directoryInfo.isReadable() ||
      !directory.exists()) {
    return DirectoryListResult::Unavailable;
  }

  children.clear();
  QDirIterator iterator(
      canonicalPath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
      QDirIterator::NoIteratorFlags
  );
  while (iterator.hasNext()) {
    iterator.next();
    if (static_cast<quint64>(children.size()) >= maxChildren) {
      return DirectoryListResult::LimitExceeded;
    }
    children.append(iterator.fileInfo());
  }
  std::sort(children.begin(), children.end(), [](const QFileInfo &left, const QFileInfo &right) {
    return left.fileName().toUtf8() < right.fileName().toUtf8();
  });
  identities.clear();
  identities.reserve(children.size());
  for (const QFileInfo &child : std::as_const(children)) {
    identities.append({.name = child.fileName(), .kind = classifyEntry(child)});
  }
  std::sort(identities.begin(), identities.end(), childLess);
  return DirectoryListResult::Ok;
}

std::optional<FileId> stableEntryId(const QString &normalizedProtocolPath, ManifestEntryType type)
{
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArrayView("RDFT/1 manifest entry id\0", 25));
  const char typeByte = static_cast<char>(type);
  hash.addData(QByteArrayView(&typeByte, 1));
  const QByteArray pathBytes = normalizedProtocolPath.toUtf8();
  hash.addData(QByteArrayView(pathBytes));
  QByteArray uuidBytes = hash.result().left(16);
  // RFC 9562 UUIDv8: deterministic application-defined ID, scoped by the
  // canonical path and entry type rather than a local absolute path.
  uuidBytes[6] = static_cast<char>((static_cast<unsigned char>(uuidBytes.at(6)) & 0x0fU) | 0x80U);
  uuidBytes[8] = static_cast<char>((static_cast<unsigned char>(uuidBytes.at(8)) & 0x3fU) | 0x80U);
  return FileId::fromBytes(uuidBytes);
}

qsizetype cborArrayHeaderBytes(quint64 count)
{
  if (count < 24) {
    return 1;
  }
  if (count <= std::numeric_limits<quint8>::max()) {
    return 2;
  }
  if (count <= std::numeric_limits<quint16>::max()) {
    return 3;
  }
  if (count <= std::numeric_limits<quint32>::max()) {
    return 5;
  }
  return 9;
}

bool isValidDisplayName(const QString &displayName)
{
  const QByteArray utf8 = displayName.toUtf8();
  return !displayName.isEmpty() && utf8.size() <= 4096;
}

struct HashFileResult
{
  QByteArray sha256;
  ManifestBuildError error = ManifestBuildError::None;
  QString diagnostic;
};

HashFileResult hashScannedFile(
    const ScannedEntry &scanned, const ManifestBuildOptions &options, quint64 completedBytes, quint64 totalBytes
)
{
  QFileInfo initialInfo(scanned.prepared.canonicalSourcePath);
  initialInfo.refresh();
  if (!initialInfo.exists() || initialInfo.isSymLink() || !initialInfo.isFile() || initialInfo.size() < 0 ||
      initialInfo.size() != scanned.size || initialInfo.lastModified().toMSecsSinceEpoch() != scanned.modifiedAtMs) {
    return {
        .error = ManifestBuildError::SourceChanged,
        .diagnostic = QStringLiteral("source changed after directory scanning"),
    };
  }

  QFile source(scanned.prepared.canonicalSourcePath);
  if (!source.open(QIODevice::ReadOnly)) {
    return {
        .error = ManifestBuildError::SourceOpenFailed,
        .diagnostic = QStringLiteral("source file could not be opened: %1").arg(source.errorString()),
    };
  }

  QByteArray chunk(static_cast<qsizetype>(options.hashChunkBytes), Qt::Uninitialized);
  QCryptographicHash fileHash(QCryptographicHash::Sha256);
  quint64 bytesHashed = 0;
  while (true) {
    const qint64 bytesRead = source.read(chunk.data(), chunk.size());
    if (bytesRead < 0) {
      return {
          .error = ManifestBuildError::SourceReadFailed,
          .diagnostic = QStringLiteral("source file could not be read: %1").arg(source.errorString()),
      };
    }
    if (bytesRead == 0) {
      break;
    }
    if (static_cast<quint64>(bytesRead) > std::numeric_limits<quint64>::max() - bytesHashed) {
      return {
          .error = ManifestBuildError::SourceReadFailed,
          .diagnostic = QStringLiteral("hashed byte count overflow"),
      };
    }
    fileHash.addData(QByteArrayView(chunk.constData(), bytesRead));
    bytesHashed += static_cast<quint64>(bytesRead);
    if (options.progress) {
      options.progress({.bytesHashed = completedBytes + bytesHashed, .totalBytes = totalBytes});
    }
  }
  source.close();

  QFileInfo finalInfo(scanned.prepared.canonicalSourcePath);
  finalInfo.refresh();
  if (sourceSnapshotChanged(initialInfo, finalInfo, bytesHashed)) {
    return {
        .error = ManifestBuildError::SourceChanged,
        .diagnostic = QStringLiteral("source size or modification time changed while hashing"),
    };
  }
  return {.sha256 = fileHash.result()};
}

} // namespace

ManifestBuildResult
ManifestBuilder::buildSingleFile(const SingleFileManifestRequest &request, const ManifestBuildOptions &options)
{
  if (!optionsAreValid(options)) {
    return fail(ManifestBuildError::InvalidOptions, QStringLiteral("manifest build options exceed local limits"));
  }

  QFileInfo sourceInfo(request.sourcePath);
  sourceInfo.refresh();
  if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
    return fail(ManifestBuildError::SourceNotFound, QStringLiteral("source file does not exist"));
  }

  const ManifestEntryKind kind = classifyEntry(sourceInfo);
  const EntryPolicyResult policy = PathPolicy::entryPolicy(kind);
  if (policy.disposition == EntryDisposition::Skip) {
    return fail(ManifestBuildError::EntrySkipped, policy.diagnostic);
  }
  if (kind == ManifestEntryKind::Directory) {
    return fail(
        ManifestBuildError::DirectoryNotSupported, QStringLiteral("use buildTransfer to construct directory manifests")
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
  ManifestEntry entry{
      .id = request.fileId,
      .relativeProtocolPath = path.normalized,
      .type = ManifestEntryType::File,
      .size = expectedBytes,
      .modifiedUtc = QDateTime::fromMSecsSinceEpoch(modifiedAtMs, QTimeZone::UTC),
      .sha256 = {},
      .flags = 0,
  };
  ScannedEntry scanned{
      .prepared =
          {
              .canonicalSourcePath = canonicalSourcePath,
              .protocolCollisionKey = path.collisionKey,
              .entry = entry,
          },
      .pathUtf8 = path.normalized.toUtf8(),
      .size = initialInfo.size(),
      .modifiedAtMs = modifiedAtMs,
      .kind = ManifestEntryKind::RegularFile,
  };
  const HashFileResult hash = hashScannedFile(scanned, options, 0, expectedBytes);
  if (hash.error != ManifestBuildError::None) {
    return fail(hash.error, hash.diagnostic);
  }

  entry.sha256 = hash.sha256;
  TransferManifestSummary summary{
      .id = request.transferId,
      .displayName = path.normalized,
      .totalBytes = expectedBytes,
      .fileCount = 1,
      .directoryCount = 0,
      .canonicalSha256 = ManifestPageCodec::canonicalSha256({entry}),
  };
  SingleFileManifest manifest{
      .canonicalSourcePath = canonicalSourcePath,
      .protocolCollisionKey = path.collisionKey,
      .entry = std::move(entry),
      .summary = std::move(summary),
  };
  return {.manifest = std::move(manifest)};
}

TransferManifestBuildResult
ManifestBuilder::buildTransfer(const TransferManifestRequest &request, const ManifestBuildOptions &options)
{
  if (!optionsAreValid(options)) {
    return failTransfer(
        ManifestBuildError::InvalidOptions, QStringLiteral("manifest build options exceed local limits")
    );
  }
  if (request.sources.isEmpty()) {
    return failTransfer(ManifestBuildError::EmptySources, QStringLiteral("at least one source is required"));
  }
  if (request.sources.size() > 1 && !isValidDisplayName(request.displayName)) {
    return failTransfer(
        ManifestBuildError::InvalidDisplayName,
        QStringLiteral("multiple selections require a non-empty display name of at most 4096 UTF-8 bytes")
    );
  }
  if (!request.displayName.isEmpty() && !isValidDisplayName(request.displayName)) {
    return failTransfer(
        ManifestBuildError::InvalidDisplayName, QStringLiteral("display name must be at most 4096 UTF-8 bytes")
    );
  }

  QList<PendingEntry> pending;
  pending.reserve(request.sources.size());
  if (static_cast<quint64>(request.sources.size()) > options.maxEntries) {
    return failTransfer(ManifestBuildError::TooManyEntries, QStringLiteral("manifest scan node limit exceeded"));
  }
  for (const ManifestSourceRequest &source : request.sources) {
    QFileInfo sourceInfo(source.sourcePath);
    sourceInfo.refresh();
    if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
      return failTransfer(ManifestBuildError::SourceNotFound, QStringLiteral("a selected source does not exist"));
    }
    const QString logicalName =
        source.relativeProtocolPath.isEmpty() ? sourceInfo.fileName() : source.relativeProtocolPath;
    pending.append({.localPath = source.sourcePath, .protocolPath = logicalName});
  }

  QList<ScannedEntry> scannedEntries;
  QList<DirectorySnapshot> directorySnapshots;
  QList<ManifestBuildWarning> warnings;
  QSet<QString> collisionKeys;
  quint64 totalBytes = 0;
  quint64 encodedEntryBytes = 0;
  quint64 discoveredNodes = static_cast<quint64>(pending.size());

  while (!pending.isEmpty()) {
    PendingEntry current = pending.takeLast();
    QFileInfo info(current.localPath);
    info.refresh();
    if (!info.exists() && !info.isSymLink()) {
      return failTransfer(ManifestBuildError::SourceChanged, QStringLiteral("source disappeared during scanning"));
    }

    const ManifestEntryKind kind = classifyEntry(info);
    const EntryPolicyResult policy = PathPolicy::entryPolicy(kind);
    if (policy.disposition == EntryDisposition::Skip) {
      warnings.append(
          {.relativeProtocolPath = current.protocolPath.normalized(QString::NormalizationForm_C),
           .diagnostic = policy.diagnostic}
      );
      continue;
    }

    const PathValidationResult path = PathPolicy::validateRelative(current.protocolPath, options.pathLimits);
    if (!path.ok) {
      return failTransfer(ManifestBuildError::UnsafeProtocolPath, path.diagnostic, path.error);
    }
    if (collisionKeys.contains(path.collisionKey)) {
      return failTransfer(
          ManifestBuildError::ProtocolPathCollision,
          QStringLiteral("two source entries map to the same portable collision key")
      );
    }
    collisionKeys.insert(path.collisionKey);

    if (static_cast<quint64>(scannedEntries.size()) >= options.maxEntries) {
      return failTransfer(ManifestBuildError::TooManyEntries, QStringLiteral("manifest entry limit exceeded"));
    }

    const QString canonicalPath = info.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
      return failTransfer(ManifestBuildError::SourceChanged, QStringLiteral("source cannot be canonicalized"));
    }
    const qint64 modifiedAtMs = info.lastModified().toMSecsSinceEpoch();
    if (modifiedAtMs < 0) {
      return failTransfer(
          ManifestBuildError::InvalidSourceTimestamp,
          QStringLiteral("source modification time cannot be represented by RDFT/1")
      );
    }

    const ManifestEntryType entryType =
        kind == ManifestEntryKind::Directory ? ManifestEntryType::Directory : ManifestEntryType::File;
    const auto entryId = stableEntryId(path.normalized, entryType);
    if (!entryId.has_value()) {
      return failTransfer(ManifestBuildError::InvalidFileId, QStringLiteral("derived fileId is invalid"));
    }
    ManifestEntry entry{
        .id = *entryId,
        .relativeProtocolPath = path.normalized,
        .type = entryType,
        .size = 0,
        .modifiedUtc = QDateTime::fromMSecsSinceEpoch(modifiedAtMs, QTimeZone::UTC),
        .sha256 = {},
        .flags = 0,
    };
    if (kind == ManifestEntryKind::RegularFile) {
      if (info.size() < 0) {
        return failTransfer(ManifestBuildError::SourceChanged, QStringLiteral("source size is invalid"));
      }
      entry.size = static_cast<quint64>(info.size());
      entry.sha256 = QByteArray(kSha256Bytes, '\0');
      if (entry.size > static_cast<quint64>(std::numeric_limits<qint64>::max()) - totalBytes) {
        return failTransfer(
            ManifestBuildError::TotalBytesOverflow, QStringLiteral("total source bytes cannot be represented by RDFT/1")
        );
      }
      totalBytes += entry.size;
    }

    const quint64 encodedEntrySize = ManifestPageCodec::canonicalEntrySize(entry);
    if (encodedEntrySize == 0 || encodedEntrySize > std::numeric_limits<quint64>::max() - encodedEntryBytes) {
      return failTransfer(
          ManifestBuildError::ManifestMetadataTooLarge, QStringLiteral("manifest metadata size overflow")
      );
    }
    encodedEntryBytes += encodedEntrySize;
    const quint64 encodedManifestBytes =
        encodedEntryBytes + static_cast<quint64>(cborArrayHeaderBytes(scannedEntries.size() + 1));
    if (encodedManifestBytes > options.maxMetadataBytes) {
      return failTransfer(
          ManifestBuildError::ManifestMetadataTooLarge, QStringLiteral("canonical manifest metadata limit exceeded")
      );
    }

    scannedEntries.append({
        .prepared =
            {
                .canonicalSourcePath = canonicalPath,
                .protocolCollisionKey = path.collisionKey,
                .entry = std::move(entry),
            },
        .pathUtf8 = path.normalized.toUtf8(),
        .size = info.size(),
        .modifiedAtMs = modifiedAtMs,
        .kind = kind,
    });

    if (kind == ManifestEntryKind::Directory) {
      QList<QFileInfo> children;
      QList<ChildIdentity> identities;
      const DirectoryListResult listed =
          listDirectory(canonicalPath, options.maxEntries - discoveredNodes, children, identities);
      if (listed == DirectoryListResult::LimitExceeded) {
        return failTransfer(ManifestBuildError::TooManyEntries, QStringLiteral("manifest scan node limit exceeded"));
      }
      if (listed != DirectoryListResult::Ok) {
        return failTransfer(
            ManifestBuildError::SourceEnumerationFailed, QStringLiteral("source directory could not be enumerated")
        );
      }
      discoveredNodes += static_cast<quint64>(children.size());
      directorySnapshots.append({.canonicalPath = canonicalPath, .modifiedAtMs = modifiedAtMs, .children = identities});
      for (auto iterator = children.crbegin(); iterator != children.crend(); ++iterator) {
        pending.append({
            .localPath = iterator->absoluteFilePath(),
            .protocolPath = path.normalized + u'/' + iterator->fileName(),
        });
      }
    }
  }

  if (scannedEntries.isEmpty()) {
    return failTransfer(ManifestBuildError::EntrySkipped, QStringLiteral("all selected entries were skipped"));
  }

  std::sort(scannedEntries.begin(), scannedEntries.end(), [](const ScannedEntry &left, const ScannedEntry &right) {
    if (left.pathUtf8 != right.pathUtf8) {
      return left.pathUtf8 < right.pathUtf8;
    }
    if (left.prepared.entry.type != right.prepared.entry.type) {
      return left.prepared.entry.type < right.prepared.entry.type;
    }
    return left.prepared.entry.id.toBytes() < right.prepared.entry.id.toBytes();
  });

  quint64 completedBytes = 0;
  for (ScannedEntry &scanned : scannedEntries) {
    if (scanned.kind != ManifestEntryKind::RegularFile) {
      continue;
    }
    const HashFileResult hash = hashScannedFile(scanned, options, completedBytes, totalBytes);
    if (hash.error != ManifestBuildError::None) {
      return failTransfer(hash.error, hash.diagnostic);
    }
    scanned.prepared.entry.sha256 = hash.sha256;
    completedBytes += scanned.prepared.entry.size;
  }

  // A file hashed early may change while later files are processed. Recheck
  // every frozen file snapshot after the complete build, not only after its
  // own read loop.
  for (const ScannedEntry &scanned : std::as_const(scannedEntries)) {
    if (scanned.kind != ManifestEntryKind::RegularFile) {
      continue;
    }
    QFileInfo finalInfo(scanned.prepared.canonicalSourcePath);
    finalInfo.refresh();
    if (!finalInfo.exists() || finalInfo.isSymLink() || !finalInfo.isFile() || finalInfo.size() != scanned.size ||
        finalInfo.lastModified().toMSecsSinceEpoch() != scanned.modifiedAtMs) {
      return failTransfer(ManifestBuildError::SourceChanged, QStringLiteral("source changed before manifest commit"));
    }
  }

  for (const DirectorySnapshot &snapshot : std::as_const(directorySnapshots)) {
    QFileInfo finalInfo(snapshot.canonicalPath);
    finalInfo.refresh();
    QList<QFileInfo> children;
    QList<ChildIdentity> identities;
    const DirectoryListResult listed =
        listDirectory(snapshot.canonicalPath, static_cast<quint64>(snapshot.children.size()) + 1, children, identities);
    if (listed != DirectoryListResult::Ok || finalInfo.lastModified().toMSecsSinceEpoch() != snapshot.modifiedAtMs ||
        identities != snapshot.children) {
      return failTransfer(
          ManifestBuildError::SourceChanged, QStringLiteral("source directory changed during manifest construction")
      );
    }
  }

  QList<PreparedManifestEntry> entries;
  entries.reserve(scannedEntries.size());
  quint64 fileCount = 0;
  quint64 directoryCount = 0;
  for (ScannedEntry &scanned : scannedEntries) {
    if (scanned.prepared.entry.type == ManifestEntryType::File) {
      ++fileCount;
    } else {
      ++directoryCount;
    }
    entries.append(std::move(scanned.prepared));
  }
  std::sort(warnings.begin(), warnings.end(), [](const ManifestBuildWarning &left, const ManifestBuildWarning &right) {
    return left.relativeProtocolPath.toUtf8() < right.relativeProtocolPath.toUtf8();
  });
  TransferManifestSummary summary{
      .id = request.transferId,
      .displayName = request.displayName.isEmpty() ? entries.constFirst().entry.relativeProtocolPath
                                                   : request.displayName,
      .totalBytes = totalBytes,
      .fileCount = fileCount,
      .directoryCount = directoryCount,
      .canonicalSha256 = ManifestPageCodec::canonicalSha256(entries),
  };
  TransferManifest manifest{
      .entries = std::move(entries),
      .warnings = std::move(warnings),
      .summary = std::move(summary),
  };
  return {.manifest = std::move(manifest)};
}

} // namespace relaydesk::transfer
