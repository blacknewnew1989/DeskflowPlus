/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/TransferCenterModel.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;
using namespace ::relaydesk::transfer;

inline constexpr qint64 kMinimumPublishIntervalMs = 200;
inline constexpr qint64 kMinimumNotificationIntervalMs = 2000;
inline constexpr auto kMaximumEta = std::chrono::hours(24 * 99);

qint64 systemClockMs()
{
  return static_cast<qint64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
  );
}

qint64 elapsedMs(qint64 now, qint64 previous)
{
  if (now <= previous)
    return 0;
  const auto elapsed = static_cast<quint64>(now) - static_cast<quint64>(previous);
  return elapsed > static_cast<quint64>(std::numeric_limits<qint64>::max())
             ? std::numeric_limits<qint64>::max()
             : static_cast<qint64>(elapsed);
}

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

QString speedText(const TransferSnapshot &snapshot);
QString etaText(const TransferSnapshot &snapshot);

QString accessibleSummary(const TransferSnapshot &snapshot)
{
  auto details = progressText(snapshot);
  const auto speed = speedText(snapshot);
  const auto eta = etaText(snapshot);
  if (!speed.isEmpty())
    details += QStringLiteral(", ") + speed;
  if (!eta.isEmpty())
    details += QStringLiteral(", ") + eta;
  return i18n::translate(Text::TransferAccessibleSummary)
      .arg(directionText(snapshot.direction))
      .arg(snapshot.displayName)
      .arg(snapshot.peerDisplayName)
      .arg(stateText(snapshot.state))
      .arg(details);
}

QString speedText(const TransferSnapshot &snapshot)
{
  const auto active = snapshot.state == TransferState::Transferring || snapshot.state == TransferState::Resuming;
  if (!active)
    return {};
  if (snapshot.progress.bytesPerSecond <= 0.0)
    return i18n::translate(Text::TransferSpeedUnknown);
  const auto rounded = std::min<double>(
      snapshot.progress.bytesPerSecond, static_cast<double>(std::numeric_limits<qint64>::max() / 2)
  );
  return i18n::translate(Text::TransferSpeed).arg(QLocale().formattedDataSize(std::max<qint64>(1, qRound64(rounded))));
}

QString etaText(const TransferSnapshot &snapshot)
{
  const auto active = snapshot.state == TransferState::Transferring || snapshot.state == TransferState::Resuming;
  if (!active)
    return {};
  if (!snapshot.progress.estimatedRemaining.has_value())
    return i18n::translate(Text::TransferEtaUnknown);

  const auto seconds = snapshot.progress.estimatedRemaining->count();
  if (seconds >= std::chrono::duration_cast<std::chrono::seconds>(kMaximumEta).count())
    return i18n::translate(Text::TransferEtaLong);
  if (seconds < 60)
    return i18n::translatePlural(Text::TransferEtaSeconds, static_cast<int>(seconds));
  if (seconds < 3600)
    return i18n::translatePlural(Text::TransferEtaMinutes, static_cast<int>((seconds + 59) / 60));
  if (seconds < 86400)
    return i18n::translatePlural(Text::TransferEtaHours, static_cast<int>((seconds + 3599) / 3600));
  return i18n::translatePlural(Text::TransferEtaDays, static_cast<int>((seconds + 86399) / 86400));
}

QString notificationTitle(TransferState state)
{
  switch (state) {
  case TransferState::Completed:
    return i18n::translate(Text::TransferNotificationCompleted);
  case TransferState::Rejected:
    return i18n::translate(Text::TransferNotificationRejected);
  case TransferState::Cancelled:
    return i18n::translate(Text::TransferNotificationCanceled);
  case TransferState::Failed:
    return i18n::translate(Text::TransferNotificationFailed);
  default:
    return {};
  }
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

TransferCenterModel::TransferCenterModel(QObject *parent) : TransferCenterModel(systemClockMs, parent)
{
}

TransferCenterModel::TransferCenterModel(Clock clock, QObject *parent)
    : QAbstractListModel(parent), m_clock(clock ? std::move(clock) : Clock(systemClockMs))
{
  m_updateTimer.setSingleShot(true);
  connect(&m_updateTimer, &QTimer::timeout, this, &TransferCenterModel::flushDueUpdates);
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
    return entry.history.has_value() ? entry.history->status == HistoryStatus::Failed : snapshot.canRetry;
  case IsTerminalRole:
    return TransferControlStateMachine::isTerminal(snapshot.state);
  case IsHistoricalRole:
    return entry.history.has_value();
  case CreatedUtcRole:
    return snapshot.createdUtc;
  case FinishedUtcRole:
    return snapshot.finishedUtc;
  case SpeedTextRole:
    return speedText(snapshot);
  case EtaTextRole:
    return etaText(snapshot);
  case HasHistoryDetailsRole:
    return entry.history.has_value();
  case CanOpenFolderRole:
    return entry.history.has_value() && entry.history->status == HistoryStatus::Completed;
  case CanOpenFileRole:
    return entry.history.has_value() && entry.history->status == HistoryStatus::Completed &&
           entry.history->fileCount == 1;
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
      {SpeedTextRole, "speedText"},
      {EtaTextRole, "etaText"},
      {HasHistoryDetailsRole, "hasHistoryDetails"},
      {CanOpenFolderRole, "canOpenFolder"},
      {CanOpenFileRole, "canOpenFile"},
      {AccessibleSummaryRole, "accessibleSummary"},
  };
}

bool TransferCenterModel::upsertTransfer(const TransferSnapshot &snapshot)
{
  if (!validSnapshot(snapshot))
    return false;

  const auto &presented = snapshot;
  const auto row = indexOf(snapshot.id);
  const auto now = m_clock();
  if (row < 0) {
    const auto inserted = upsertEntry({presented, std::nullopt});
    m_lastPublishedMs.insert(snapshot.id, now);
    if (TransferControlStateMachine::isTerminal(presented.state))
      enqueueTerminalNotification(presented);
    return inserted;
  }

  const auto &current = m_entries.at(row).snapshot;
  const auto lastPublished = m_lastPublishedMs.constFind(snapshot.id);
  if (isProgressOnlyChange(current, presented) && lastPublished != m_lastPublishedMs.cend() &&
      elapsedMs(now, lastPublished.value()) < kMinimumPublishIntervalMs) {
    m_pendingEntries.insert(snapshot.id, Entry{presented, std::nullopt});
    scheduleUpdateTimer();
    return true;
  }

  m_pendingEntries.remove(snapshot.id);
  const auto updated = upsertEntry({presented, std::nullopt});
  m_lastPublishedMs.insert(snapshot.id, now);
  if (TransferControlStateMachine::isTerminal(presented.state))
    enqueueTerminalNotification(presented);
  scheduleUpdateTimer();
  return updated;
}

bool TransferCenterModel::removeTransfer(const TransferId &transferId)
{
  const auto row = indexOf(transferId);
  if (row < 0)
    return false;
  beginRemoveRows(QModelIndex(), row, row);
  m_entries.removeAt(row);
  endRemoveRows();
  m_pendingEntries.remove(transferId);
  m_lastPublishedMs.remove(transferId);
  m_notifiedTerminalIds.remove(transferId);
  m_pendingNotifications.removeIf([&transferId](const auto &notification) {
    return notification.snapshot.id == transferId;
  });
  scheduleUpdateTimer();
  return true;
}

void TransferCenterModel::setTransfers(const QList<TransferSnapshot> &snapshots)
{
  m_updateTimer.stop();
  m_pendingNotifications.clear();
  m_lastNotificationMs.reset();
  m_notifiedTerminalIds.clear();
  QList<Entry> entries;
  for (const auto &entry : std::as_const(m_entries)) {
    if (entry.history.has_value())
      entries.append(entry);
  }
  for (const auto &snapshot : snapshots) {
    if (!validSnapshot(snapshot))
      continue;
    const auto &presented = snapshot;
    const auto duplicate = std::find_if(entries.cbegin(), entries.cend(), [&snapshot](const auto &entry) {
      return entry.snapshot.id == snapshot.id;
    });
    if (duplicate == entries.cend())
      entries.append({presented, std::nullopt});
  }
  beginResetModel();
  m_entries = std::move(entries);
  std::sort(m_entries.begin(), m_entries.end(), [this](const auto &left, const auto &right) {
    return compare(left, right) < 0;
  });
  endResetModel();
  m_pendingEntries.clear();
  m_lastPublishedMs.clear();
  const auto now = m_clock();
  for (const auto &entry : std::as_const(m_entries)) {
    m_lastPublishedMs.insert(entry.snapshot.id, now);
    if (TransferControlStateMachine::isTerminal(entry.snapshot.state))
      m_notifiedTerminalIds.insert(entry.snapshot.id);
  }
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

bool TransferCenterModel::requestRetry(const TransferId &transferId)
{
  const auto row = indexOf(transferId);
  if (row < 0 || !data(index(row, 0), CanRetryRole).toBool())
    return false;

  const auto &entry = m_entries.at(row);
  if (entry.history.has_value())
    Q_EMIT historyRetryRequested(*entry.history);
  else
    Q_EMIT retryRequested(entry.snapshot);
  return true;
}

bool TransferCenterModel::requestOpenFolder(const TransferId &transferId)
{
  return requestHistoryAction(transferId, CanOpenFolderRole, &TransferCenterModel::openFolderRequested);
}

bool TransferCenterModel::requestOpenFile(const TransferId &transferId)
{
  return requestHistoryAction(transferId, CanOpenFileRole, &TransferCenterModel::openFileRequested);
}

void TransferCenterModel::flushDueUpdates()
{
  const auto now = m_clock();
  const auto pendingIds = m_pendingEntries.keys();
  for (const auto &id : pendingIds) {
    const auto lastPublished = m_lastPublishedMs.constFind(id);
    if (lastPublished != m_lastPublishedMs.cend() &&
        elapsedMs(now, lastPublished.value()) < kMinimumPublishIntervalMs) {
      continue;
    }
    const auto pendingIt = m_pendingEntries.find(id);
    if (pendingIt == m_pendingEntries.end())
      continue;
    const auto pending = pendingIt.value();
    m_pendingEntries.erase(pendingIt);
    (void)upsertEntry(pending);
    m_lastPublishedMs.insert(id, now);
    if (TransferControlStateMachine::isTerminal(pending.snapshot.state))
      enqueueTerminalNotification(pending.snapshot);
  }
  flushNotification();
  scheduleUpdateTimer();
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

bool TransferCenterModel::isProgressOnlyChange(const TransferSnapshot &current, const TransferSnapshot &next) const
{
  auto currentWithoutProgress = current;
  auto nextWithoutProgress = next;
  currentWithoutProgress.progress.completedBytes = 0;
  currentWithoutProgress.progress.completedFiles = 0;
  currentWithoutProgress.progress.bytesPerSecond = 0.0;
  currentWithoutProgress.progress.estimatedRemaining.reset();
  nextWithoutProgress.progress.completedBytes = 0;
  nextWithoutProgress.progress.completedFiles = 0;
  nextWithoutProgress.progress.bytesPerSecond = 0.0;
  nextWithoutProgress.progress.estimatedRemaining.reset();
  currentWithoutProgress.currentRelativeDisplayPath.clear();
  nextWithoutProgress.currentRelativeDisplayPath.clear();
  return currentWithoutProgress == nextWithoutProgress;
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

void TransferCenterModel::scheduleUpdateTimer()
{
  qint64 delay = std::numeric_limits<qint64>::max();
  const auto now = m_clock();
  for (auto it = m_pendingEntries.cbegin(); it != m_pendingEntries.cend(); ++it) {
    delay = std::min(
        delay, kMinimumPublishIntervalMs - elapsedMs(now, m_lastPublishedMs.value(it.key(), now))
    );
  }
  if (!m_pendingNotifications.isEmpty() && m_lastNotificationMs.has_value())
    delay = std::min(delay, kMinimumNotificationIntervalMs - elapsedMs(now, *m_lastNotificationMs));
  if (delay == std::numeric_limits<qint64>::max()) {
    m_updateTimer.stop();
    return;
  }
  m_updateTimer.start(static_cast<int>(std::clamp<qint64>(delay, 1, std::numeric_limits<int>::max())));
}

void TransferCenterModel::enqueueTerminalNotification(const TransferSnapshot &snapshot)
{
  if (!TransferControlStateMachine::isTerminal(snapshot.state) || m_notifiedTerminalIds.contains(snapshot.id))
    return;
  m_notifiedTerminalIds.insert(snapshot.id);
  const auto title = notificationTitle(snapshot.state);
  if (title.isEmpty())
    return;
  m_pendingNotifications.append({
      .snapshot = snapshot,
      .title = title,
      .message = i18n::translate(Text::TransferNotificationBody).arg(snapshot.displayName, snapshot.peerDisplayName),
  });
  flushNotification();
  scheduleUpdateTimer();
}

void TransferCenterModel::flushNotification()
{
  if (m_pendingNotifications.isEmpty())
    return;
  const auto now = m_clock();
  if (m_lastNotificationMs.has_value() && elapsedMs(now, *m_lastNotificationMs) < kMinimumNotificationIntervalMs)
    return;
  const auto notification = m_pendingNotifications.takeFirst();
  m_lastNotificationMs = now;
  Q_EMIT notificationRequested(notification.snapshot, notification.title, notification.message);
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

bool TransferCenterModel::requestHistoryAction(
    const TransferId &transferId, Role allowedRole,
    void (TransferCenterModel::*signal)(TransferHistoryRecord)
)
{
  const auto row = indexOf(transferId);
  if (row < 0 || !data(index(row, 0), allowedRole).toBool())
    return false;
  const auto &history = m_entries.at(row).history;
  if (!history.has_value())
    return false;
  Q_EMIT(this->*signal)(*history);
  return true;
}

} // namespace deskflow::relaydesk::model
