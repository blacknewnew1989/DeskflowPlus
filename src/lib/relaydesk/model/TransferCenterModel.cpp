/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/TransferCenterModel.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;
using namespace ::relaydesk::transfer;

QList<int> allDataRoles()
{
  QList<int> roles{Qt::DisplayRole, Qt::ToolTipRole, Qt::AccessibleTextRole};
  for (int role = TransferCenterModel::TransferIdRole; role <= TransferCenterModel::AccessibleSummaryRole; ++role)
    roles.append(role);
  return roles;
}

QString directionText(TransferDirection direction)
{
  return i18n::translate(
      direction == TransferDirection::Sending ? Text::TransferDirectionSending : Text::TransferDirectionReceiving
  );
}

QString stateText(TransferState state)
{
  switch (state) {
  case TransferState::Preparing:
    return i18n::translate(Text::TransferStatePreparing);
  case TransferState::Offered:
  case TransferState::WaitingForAcceptance:
    return i18n::translate(Text::TransferStateAwaitingConfirmation);
  case TransferState::Queued:
    return i18n::translate(Text::TransferStateQueued);
  case TransferState::Transferring:
    return i18n::translate(Text::TransferStateTransferring);
  case TransferState::Paused:
    return i18n::translate(Text::TransferStatePaused);
  case TransferState::Interrupted:
    return i18n::translate(Text::TransferStateInterrupted);
  case TransferState::Resuming:
    return i18n::translate(Text::TransferStateResuming);
  case TransferState::Verifying:
    return i18n::translate(Text::TransferStateVerifying);
  case TransferState::Committing:
    return i18n::translate(Text::TransferStateSaving);
  case TransferState::Completed:
    return i18n::translate(Text::TransferStateCompleted);
  case TransferState::Rejected:
    return i18n::translate(Text::TransferStateRejected);
  case TransferState::Cancelling:
    return i18n::translate(Text::TransferStateCanceling);
  case TransferState::Cancelled:
    return i18n::translate(Text::TransferStateCanceled);
  case TransferState::Failed:
    return i18n::translate(Text::TransferStateFailed);
  }
  return i18n::translate(Text::TransferStateFailed);
}

QString safeErrorText(const QString &errorMessageKey)
{
  if (errorMessageKey.isEmpty())
    return {};
  if (errorMessageKey.endsWith(QStringLiteral("disk_full")))
    return i18n::translate(Text::TransferErrorDiskFull);
  if (errorMessageKey.endsWith(QStringLiteral("unsafe_path")) ||
      errorMessageKey.endsWith(QStringLiteral("path_invalid"))) {
    return i18n::translate(Text::TransferErrorUnsafePath);
  }
  if (errorMessageKey.endsWith(QStringLiteral("unreadable")) ||
      errorMessageKey.endsWith(QStringLiteral("source_changed"))) {
    return i18n::translate(Text::TransferErrorUnreadable);
  }
  if (errorMessageKey.endsWith(QStringLiteral("connection_lost")))
    return i18n::translate(Text::TransferErrorConnectionLost);
  if (errorMessageKey.endsWith(QStringLiteral("checksum_mismatch")) ||
      errorMessageKey.endsWith(QStringLiteral("hash_mismatch"))) {
    return i18n::translate(Text::TransferErrorChecksumMismatch);
  }
  return i18n::translate(Text::TransferErrorUnknown);
}

double progressValue(const TransferSnapshot &snapshot)
{
  const auto &progress = snapshot.progress;
  if (progress.totalBytes > 0)
    return static_cast<double>(progress.completedBytes) / static_cast<double>(progress.totalBytes);
  if (progress.totalFiles > 0)
    return static_cast<double>(progress.completedFiles) / static_cast<double>(progress.totalFiles);
  return TransferControlStateMachine::isTerminal(snapshot.state) ? 1.0 : 0.0;
}

QString progressText(const TransferSnapshot &snapshot)
{
  const auto formattedSize = [](quint64 bytes) {
    return bytes <= static_cast<quint64>(std::numeric_limits<qint64>::max())
               ? QLocale().formattedDataSize(static_cast<qint64>(bytes))
               : QLocale().toString(bytes) + QStringLiteral(" B");
  };
  const auto bytes = i18n::translate(Text::TransferProgressBytes)
                         .arg(formattedSize(snapshot.progress.completedBytes))
                         .arg(formattedSize(snapshot.progress.totalBytes));
  const auto files = i18n::translatePlural(Text::TransferProgressItems, static_cast<int>(snapshot.progress.totalFiles))
                         .arg(QLocale().toString(snapshot.progress.completedFiles));
  return bytes + QStringLiteral(" · ") + files;
}

QString accessibleSummary(const TransferSnapshot &snapshot)
{
  return i18n::translate(Text::TransferAccessibleSummary)
      .arg(directionText(snapshot.direction))
      .arg(snapshot.displayName)
      .arg(snapshot.peerDisplayName)
      .arg(stateText(snapshot.state))
      .arg(progressText(snapshot));
}

TransferState historyState(HistoryStatus status)
{
  switch (status) {
  case HistoryStatus::Completed:
    return TransferState::Completed;
  case HistoryStatus::Rejected:
    return TransferState::Rejected;
  case HistoryStatus::Cancelled:
    return TransferState::Cancelled;
  case HistoryStatus::Failed:
    return TransferState::Failed;
  }
  return TransferState::Failed;
}

TransferDirection historyDirection(HistoryDirection direction)
{
  return direction == HistoryDirection::Sending ? TransferDirection::Sending : TransferDirection::Receiving;
}

} // namespace

TransferCenterModel::TransferCenterModel(QObject *parent) : QAbstractListModel(parent)
{
}

int TransferCenterModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_entries.size();
}

QVariant TransferCenterModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.parent().isValid() || index.column() != 0 || index.row() < 0 ||
      index.row() >= m_entries.size()) {
    return {};
  }

  const auto &entry = m_entries.at(index.row());
  const auto &snapshot = entry.snapshot;
  switch (role) {
  case Qt::DisplayRole:
  case DisplayNameRole:
    return snapshot.displayName;
  case Qt::ToolTipRole: {
    const auto error = safeErrorText(snapshot.errorMessageKey);
    return error.isEmpty() ? snapshot.currentRelativeDisplayPath : error;
  }
  case Qt::AccessibleTextRole:
  case AccessibleSummaryRole:
    return accessibleSummary(snapshot);
  case TransferIdRole:
    return snapshot.id.toString(QUuid::WithoutBraces);
  case DirectionRole:
    return static_cast<int>(snapshot.direction);
  case DirectionTextRole:
    return directionText(snapshot.direction);
  case PeerDeviceIdRole:
    return snapshot.peerId.toString();
  case PeerDisplayNameRole:
    return snapshot.peerDisplayName;
  case StateRole:
    return static_cast<int>(snapshot.state);
  case StateTextRole:
    return stateText(snapshot.state);
  case CompletedBytesRole:
    return QVariant::fromValue(snapshot.progress.completedBytes);
  case TotalBytesRole:
    return QVariant::fromValue(snapshot.progress.totalBytes);
  case CompletedFilesRole:
    return QVariant::fromValue(snapshot.progress.completedFiles);
  case TotalFilesRole:
    return QVariant::fromValue(snapshot.progress.totalFiles);
  case ProgressValueRole:
    return progressValue(snapshot);
  case ProgressPercentRole:
    return qRound(progressValue(snapshot) * 100.0);
  case ProgressTextRole:
    return progressText(snapshot);
  case CurrentPathRole:
    return snapshot.currentRelativeDisplayPath;
  case ErrorTextRole:
    return safeErrorText(snapshot.errorMessageKey);
  case CanPauseRole:
    return !entry.history.has_value() && snapshot.canPause;
  case CanResumeRole:
    return !entry.history.has_value() && snapshot.canResume;
  case CanCancelRole:
    return !entry.history.has_value() && snapshot.canCancel;
  case CanRetryRole:
    return !entry.history.has_value() && snapshot.canRetry;
  case IsTerminalRole:
    return TransferControlStateMachine::isTerminal(snapshot.state);
  case IsHistoricalRole:
    return entry.history.has_value();
  case CreatedUtcRole:
    return snapshot.createdUtc;
  case FinishedUtcRole:
    return snapshot.finishedUtc;
  default:
    return {};
  }
}

QHash<int, QByteArray> TransferCenterModel::roleNames() const
{
  return {
      {TransferIdRole, "transferId"},
      {DirectionRole, "direction"},
      {DirectionTextRole, "directionText"},
      {PeerDeviceIdRole, "peerDeviceId"},
      {PeerDisplayNameRole, "peerDisplayName"},
      {DisplayNameRole, "displayName"},
      {StateRole, "state"},
      {StateTextRole, "stateText"},
      {CompletedBytesRole, "completedBytes"},
      {TotalBytesRole, "totalBytes"},
      {CompletedFilesRole, "completedFiles"},
      {TotalFilesRole, "totalFiles"},
      {ProgressValueRole, "progressValue"},
      {ProgressPercentRole, "progressPercent"},
      {ProgressTextRole, "progressText"},
      {CurrentPathRole, "currentPath"},
      {ErrorTextRole, "errorText"},
      {CanPauseRole, "canPause"},
      {CanResumeRole, "canResume"},
      {CanCancelRole, "canCancel"},
      {CanRetryRole, "canRetry"},
      {IsTerminalRole, "isTerminal"},
      {IsHistoricalRole, "isHistorical"},
      {CreatedUtcRole, "createdUtc"},
      {FinishedUtcRole, "finishedUtc"},
      {AccessibleSummaryRole, "accessibleSummary"},
  };
}

bool TransferCenterModel::upsertTransfer(const TransferSnapshot &snapshot)
{
  return validSnapshot(snapshot) && upsertEntry({snapshot, std::nullopt});
}

bool TransferCenterModel::removeTransfer(const TransferId &transferId)
{
  const auto row = indexOf(transferId);
  if (row < 0)
    return false;
  beginRemoveRows(QModelIndex(), row, row);
  m_entries.removeAt(row);
  endRemoveRows();
  return true;
}

void TransferCenterModel::setTransfers(const QList<TransferSnapshot> &snapshots)
{
  QList<Entry> entries;
  for (const auto &entry : std::as_const(m_entries)) {
    if (entry.history.has_value())
      entries.append(entry);
  }
  for (const auto &snapshot : snapshots) {
    if (!validSnapshot(snapshot))
      continue;
    const auto duplicate = std::find_if(entries.cbegin(), entries.cend(), [&snapshot](const auto &entry) {
      return entry.snapshot.id == snapshot.id;
    });
    if (duplicate == entries.cend())
      entries.append({snapshot, std::nullopt});
  }
  beginResetModel();
  m_entries = std::move(entries);
  std::sort(m_entries.begin(), m_entries.end(), [this](const auto &left, const auto &right) {
    return compare(left, right) < 0;
  });
  endResetModel();
}

void TransferCenterModel::setHistoryRecords(const QList<TransferHistoryRecord> &records)
{
  QList<Entry> entries;
  for (const auto &entry : std::as_const(m_entries)) {
    if (!entry.history.has_value())
      entries.append(entry);
  }
  for (const auto &record : records) {
    if (!validHistory(record))
      continue;
    const auto duplicate = std::find_if(entries.cbegin(), entries.cend(), [&record](const auto &entry) {
      return entry.snapshot.id == record.transferId;
    });
    if (duplicate == entries.cend())
      entries.append(fromHistory(record));
  }
  beginResetModel();
  m_entries = std::move(entries);
  std::sort(m_entries.begin(), m_entries.end(), [this](const auto &left, const auto &right) {
    return compare(left, right) < 0;
  });
  endResetModel();
}

int TransferCenterModel::indexOf(const TransferId &transferId) const
{
  for (int row = 0; row < m_entries.size(); ++row) {
    if (m_entries.at(row).snapshot.id == transferId)
      return row;
  }
  return -1;
}

std::optional<TransferSnapshot> TransferCenterModel::snapshot(const TransferId &transferId) const
{
  const auto row = indexOf(transferId);
  return row < 0 ? std::nullopt : std::optional<TransferSnapshot>{m_entries.at(row).snapshot};
}

std::optional<TransferHistoryRecord> TransferCenterModel::historyRecord(const TransferId &transferId) const
{
  const auto row = indexOf(transferId);
  return row < 0 ? std::nullopt : m_entries.at(row).history;
}

bool TransferCenterModel::requestPause(const TransferId &transferId)
{
  return requestControl(transferId, CanPauseRole, &TransferCenterModel::pauseRequested);
}

bool TransferCenterModel::requestResume(const TransferId &transferId)
{
  return requestControl(transferId, CanResumeRole, &TransferCenterModel::resumeRequested);
}

bool TransferCenterModel::requestCancel(const TransferId &transferId)
{
  return requestControl(transferId, CanCancelRole, &TransferCenterModel::cancelRequested);
}

bool TransferCenterModel::validSnapshot(const TransferSnapshot &snapshot)
{
  return !snapshot.id.isNull() && !snapshot.peerId.value().isNull() && !snapshot.displayName.isEmpty() &&
         snapshot.createdUtc.isValid() && snapshot.progress.completedBytes <= snapshot.progress.totalBytes &&
         snapshot.progress.completedFiles <= snapshot.progress.totalFiles &&
         std::isfinite(snapshot.progress.bytesPerSecond) && snapshot.progress.bytesPerSecond >= 0.0 &&
         (!snapshot.progress.estimatedRemaining.has_value() || snapshot.progress.estimatedRemaining->count() >= 0);
}

bool TransferCenterModel::validHistory(const TransferHistoryRecord &record)
{
  return !record.transferId.isNull() && !record.peerDeviceId.value().isNull() && !record.displayName.isEmpty() &&
         record.startedUtc.isValid() && record.finishedUtc.isValid() && record.finishedUtc >= record.startedUtc;
}

TransferCenterModel::Entry TransferCenterModel::fromHistory(const TransferHistoryRecord &record)
{
  const auto completed = record.status == HistoryStatus::Completed;
  return {
      .snapshot = {
          .id = record.transferId,
          .peerId = record.peerDeviceId,
          .peerDisplayName = record.peerDisplayName,
          .displayName = record.displayName,
          .direction = historyDirection(record.direction),
          .state = historyState(record.status),
          .progress = {
              .completedBytes = completed ? record.totalBytes : 0,
              .totalBytes = record.totalBytes,
              .completedFiles = completed ? record.fileCount : 0,
              .totalFiles = record.fileCount,
          },
          .errorMessageKey = record.errorMessageKey,
          .errorCode = record.errorCode,
          .createdUtc = record.startedUtc,
          .finishedUtc = record.finishedUtc,
      },
      .history = record,
  };
}

int TransferCenterModel::insertionIndex(const Entry &entry, int ignoredIndex) const
{
  int destination = 0;
  for (int row = 0; row < m_entries.size(); ++row) {
    if (row == ignoredIndex)
      continue;
    if (compare(entry, m_entries.at(row)) < 0)
      break;
    ++destination;
  }
  return destination;
}

int TransferCenterModel::compare(const Entry &left, const Entry &right) const
{
  const auto leftTerminal = TransferControlStateMachine::isTerminal(left.snapshot.state);
  const auto rightTerminal = TransferControlStateMachine::isTerminal(right.snapshot.state);
  if (leftTerminal != rightTerminal)
    return leftTerminal ? 1 : -1;

  const auto leftTime = leftTerminal ? left.snapshot.finishedUtc : left.snapshot.createdUtc;
  const auto rightTime = rightTerminal ? right.snapshot.finishedUtc : right.snapshot.createdUtc;
  if (leftTime != rightTime)
    return leftTime > rightTime ? -1 : 1;
  return QString::compare(
      left.snapshot.id.toString(QUuid::WithoutBraces), right.snapshot.id.toString(QUuid::WithoutBraces),
      Qt::CaseSensitive
  );
}

bool TransferCenterModel::upsertEntry(Entry entry)
{
  const auto existingRow = indexOf(entry.snapshot.id);
  if (existingRow < 0) {
    const auto destination = insertionIndex(entry);
    beginInsertRows(QModelIndex(), destination, destination);
    m_entries.insert(destination, std::move(entry));
    endInsertRows();
    return true;
  }
  if (m_entries.at(existingRow) == entry)
    return true;

  const auto destination = insertionIndex(entry, existingRow);
  if (destination == existingRow) {
    m_entries[existingRow] = std::move(entry);
    const auto changedIndex = index(existingRow, 0);
    Q_EMIT dataChanged(changedIndex, changedIndex, allDataRoles());
    return true;
  }

  const auto destinationChild = destination > existingRow ? destination + 1 : destination;
  beginMoveRows(QModelIndex(), existingRow, existingRow, QModelIndex(), destinationChild);
  m_entries.removeAt(existingRow);
  m_entries.insert(destination, std::move(entry));
  endMoveRows();
  const auto changedIndex = index(destination, 0);
  Q_EMIT dataChanged(changedIndex, changedIndex, allDataRoles());
  return true;
}

bool TransferCenterModel::requestControl(
    const TransferId &transferId, Role allowedRole, void (TransferCenterModel::*signal)(TransferSnapshot)
)
{
  const auto row = indexOf(transferId);
  if (row < 0 || !data(index(row, 0), allowedRole).toBool())
    return false;
  Q_EMIT(this->*signal)(m_entries.at(row).snapshot);
  return true;
}

} // namespace deskflow::relaydesk::model
