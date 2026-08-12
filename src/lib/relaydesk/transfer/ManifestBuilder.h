// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace relaydesk::transfer {

inline constexpr quint32 kDefaultManifestHashChunkBytes = 1U * 1024U * 1024U;
inline constexpr quint32 kMaxManifestHashChunkBytes = 4U * 1024U * 1024U;
inline constexpr quint64 kDefaultMaxManifestEntries = 100'000;
inline constexpr quint64 kMaxManifestEntries = 100'000;
inline constexpr quint64 kDefaultMaxManifestMetadataBytes = 64U * 1024U * 1024U;
inline constexpr quint64 kMaxManifestMetadataBytes = 64U * 1024U * 1024U;

struct SingleFileManifestRequest
{
  QString sourcePath;
  // Empty derives the logical name from QFileInfo::fileName(). Any supplied
  // value is still normalized and validated by the shared PathPolicy.
  QString relativeProtocolPath;
  TransferId transferId;
  FileId fileId;
};

struct ManifestSourceRequest
{
  QString sourcePath;
  // Empty derives the top-level logical name from QFileInfo::fileName().
  // Supplying a value permits callers to place separate selections beneath a
  // shared logical root without exposing their local absolute paths.
  QString relativeProtocolPath;
};

struct TransferManifestRequest
{
  QList<ManifestSourceRequest> sources;
  TransferId transferId;
  // Required for multiple top-level selections. A single selection derives
  // its display name from its normalized top-level protocol path when empty.
  QString displayName;
};

struct ManifestBuildProgress
{
  quint64 bytesHashed = 0;
  quint64 totalBytes = 0;
};

struct ManifestBuildOptions
{
  quint32 hashChunkBytes = kDefaultManifestHashChunkBytes;
  PathLimits pathLimits;
  quint64 maxEntries = kDefaultMaxManifestEntries;
  quint64 maxMetadataBytes = kDefaultMaxManifestMetadataBytes;
  std::function<void(const ManifestBuildProgress &)> progress;
};

enum class ManifestBuildError
{
  None,
  InvalidTransferId,
  InvalidFileId,
  InvalidOptions,
  EmptySources,
  InvalidDisplayName,
  SourceNotFound,
  EntrySkipped,
  DirectoryNotSupported,
  UnsafeProtocolPath,
  ProtocolPathCollision,
  TooManyEntries,
  ManifestMetadataTooLarge,
  TotalBytesOverflow,
  SourceEnumerationFailed,
  SourceOpenFailed,
  SourceReadFailed,
  InvalidSourceTimestamp,
  SourceChanged,
};

struct TransferManifestBuildResult
{
  std::optional<TransferManifest> manifest;
  ManifestBuildError error = ManifestBuildError::None;
  PathError pathError = PathError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return manifest.has_value() && error == ManifestBuildError::None;
  }
};

struct ManifestBuildResult
{
  std::optional<SingleFileManifest> manifest;
  ManifestBuildError error = ManifestBuildError::None;
  PathError pathError = PathError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return manifest.has_value() && error == ManifestBuildError::None;
  }
};

class ManifestBuilder final
{
public:
  // Pure synchronous core. Callers run this on their bounded worker pool;
  // no GUI, socket, or event-loop dependency is introduced here.
  [[nodiscard]] static ManifestBuildResult
  buildSingleFile(const SingleFileManifestRequest &request, const ManifestBuildOptions &options = {});

  // Iterative, bounded scanner and streaming hasher. Callers run this pure
  // core on a bounded worker pool, never from GUI or network callbacks.
  [[nodiscard]] static TransferManifestBuildResult
  buildTransfer(const TransferManifestRequest &request, const ManifestBuildOptions &options = {});
};

} // namespace relaydesk::transfer
