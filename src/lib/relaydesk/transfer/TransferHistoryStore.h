// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/Protocol.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <functional>

namespace relaydesk::transfer {

inline constexpr quint64 kTransferHistorySchemaVersion = 1;
inline constexpr qsizetype kDefaultMaximumHistoryEntries = 1'000;
inline constexpr quint64 kDefaultMaximumHistoryBytes = 16U * 1024U * 1024U;
inline constexpr qsizetype kDefaultMaximumHistoryLineBytes = 64U * 1024U;

enum class HistoryDirection
{
  Sending,
  Receiving,
};

enum class HistoryStatus
{
  Completed,
  Rejected,
  Cancelled,
  Failed,
};

struct TransferHistoryRecord
{
  TransferId transferId;
  deskflow::relaydesk::DeviceId peerDeviceId;
  QString peerDisplayName;
  QString displayName;
  HistoryDirection direction = HistoryDirection::Sending;
  quint64 fileCount = 0;
  quint64 totalBytes = 0;
  QDateTime startedUtc;
  QDateTime finishedUtc;
  HistoryStatus status = HistoryStatus::Completed;
  int errorCode = 0;
  QString errorMessageKey;

  [[nodiscard]] bool operator==(const TransferHistoryRecord &) const = default;
};

struct TransferHistoryLimits
{
  qsizetype maximumEntries = kDefaultMaximumHistoryEntries;
  std::chrono::days maximumAge{90};
  quint64 maximumFileBytes = kDefaultMaximumHistoryBytes;
  qsizetype maximumLineBytes = kDefaultMaximumHistoryLineBytes;
};

enum class TransferHistoryError
{
  None,
  InvalidStorePath,
  InvalidLimits,
  InvalidRecord,
  DirectoryCreateFailed,
  OpenFailed,
  ReadFailed,
  FileTooLarge,
  WriteFailed,
  CommitFailed,
  RemoveFailed,
};

struct TransferHistoryIssue
{
  qsizetype line = 0;
  QString diagnostic;
};

struct TransferHistoryOperationResult
{
  TransferHistoryError error = TransferHistoryError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TransferHistoryError::None;
  }
};

struct TransferHistoryPage
{
  // Newest first. Offset/limit are applied after invalid and expired rows are
  // removed.
  QList<TransferHistoryRecord> records;
  QList<TransferHistoryIssue> issues;
  qsizetype totalValidEntries = 0;
};

struct TransferHistoryPageResult
{
  TransferHistoryPage page;
  TransferHistoryError error = TransferHistoryError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TransferHistoryError::None;
  }
};

class TransferHistoryStore final
{
public:
  using Clock = std::function<QDateTime()>;

  explicit TransferHistoryStore(QString historyPath, TransferHistoryLimits limits = {}, Clock clock = {});

  // Replaces an existing record with the same transferId, prunes entries older
  // than maximumAge and keeps only the newest maximumEntries rows. The whole
  // JSONL file is committed atomically.
  [[nodiscard]] TransferHistoryOperationResult append(const TransferHistoryRecord &record) const;
  [[nodiscard]] TransferHistoryPageResult page(qsizetype offset = 0, qsizetype limit = 100) const;
  [[nodiscard]] TransferHistoryOperationResult clear() const;

  [[nodiscard]] QString historyPath() const;

private:
  struct LoadAllResult;

  [[nodiscard]] LoadAllResult loadAll() const;
  [[nodiscard]] TransferHistoryOperationResult writeAll(const QList<TransferHistoryRecord> &records) const;

  QString m_historyPath;
  TransferHistoryLimits m_limits;
  Clock m_clock;
};

} // namespace relaydesk::transfer
