// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/ResumeStore.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <chrono>
#include <optional>

namespace relaydesk::transfer {

inline constexpr auto kDefaultPartialRetention = std::chrono::days{7};

struct ExpiredPartialFile
{
  FileId fileId;
  QString relativeProtocolPath;
  QString partAbsolutePath;
  quint64 durableOffset = 0;
  quint64 totalBytes = 0;

  [[nodiscard]] bool operator==(const ExpiredPartialFile &) const = default;
};

struct ExpiredPartialTransfer
{
  TransferId transferId;
  QDateTime updatedUtc;
  QList<ExpiredPartialFile> files;

  [[nodiscard]] bool operator==(const ExpiredPartialTransfer &) const = default;
};

enum class PartialCleanupIssueError
{
  ResumeStoreIssue,
  UnsafePartPath,
  MissingPart,
  PartSizeMismatch,
};

struct PartialCleanupIssue
{
  // Store-level corruption can be discovered before a transfer ID is parsed.
  std::optional<TransferId> transferId;
  QString path;
  PartialCleanupIssueError error = PartialCleanupIssueError::ResumeStoreIssue;
  QString diagnostic;

  [[nodiscard]] bool operator==(const PartialCleanupIssue &) const = default;
};

enum class PartialCleanupError
{
  None,
  InvalidConfiguration,
  ScanFailed,
};

enum class PartialCleanupChoice
{
  Keep,
  Delete,
};

enum class PartialCleanupApplyError
{
  None,
  NotListed,
  ChangedSinceListing,
  PartRemoveFailed,
  StateRemoveFailed,
};

struct PartialCleanupApplyResult
{
  PartialCleanupApplyError error = PartialCleanupApplyError::None;
  QString diagnostic;
  qsizetype removedPartFiles = 0;
  bool stateRemoved = false;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PartialCleanupApplyError::None;
  }
};

struct PartialCleanupListing
{
  QList<ExpiredPartialTransfer> expired;
  QList<PartialCleanupIssue> issues;
  PartialCleanupError error = PartialCleanupError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PartialCleanupError::None;
  }
};

struct PartialCleanupSettings
{
  QString stagingRoot;
  std::chrono::seconds retention = std::chrono::duration_cast<std::chrono::seconds>(kDefaultPartialRetention);
  PathLimits pathLimits;
};

class PartialCleanupPolicy final
{
public:
  PartialCleanupPolicy(const ResumeStore &store, PartialCleanupSettings settings);

  // Read-only worker API. Callers notify users from this listing, then make a
  // separate explicit keep/delete choice. No partial is removed here.
  [[nodiscard]] PartialCleanupListing listExpired(QDateTime nowUtc = QDateTime::currentDateTimeUtc()) const;
  // Applies only a previously listed transfer snapshot. Keep is a no-op.
  // Delete revalidates state and every part before any removal.
  [[nodiscard]] PartialCleanupApplyResult
  apply(const ExpiredPartialTransfer &listed, PartialCleanupChoice choice) const;

private:
  const ResumeStore &m_store;
  PartialCleanupSettings m_settings;
};

} // namespace relaydesk::transfer
