// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QString>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace relaydesk::transfer {

inline constexpr quint32 kDefaultManifestHashChunkBytes = 1U * 1024U * 1024U;
inline constexpr quint32 kMaxManifestHashChunkBytes = 4U * 1024U * 1024U;

struct SingleFileManifestRequest
{
  QString sourcePath;
  // Empty derives the logical name from QFileInfo::fileName(). Any supplied
  // value is still normalized and validated by the shared PathPolicy.
  QString relativeProtocolPath;
  TransferId transferId;
  FileId fileId;
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
  std::function<void(const ManifestBuildProgress &)> progress;
};

enum class ManifestBuildError
{
  None,
  InvalidTransferId,
  InvalidFileId,
  InvalidOptions,
  SourceNotFound,
  EntrySkipped,
  DirectoryNotSupported,
  UnsafeProtocolPath,
  SourceOpenFailed,
  SourceReadFailed,
  InvalidSourceTimestamp,
  SourceChanged,
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
};

} // namespace relaydesk::transfer
