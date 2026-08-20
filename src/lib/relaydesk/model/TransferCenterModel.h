/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferHistoryStore.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>

namespace deskflow::relaydesk::model {

class TransferCenterModel final : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role
  {
    TransferIdRole = Qt::UserRole + 1,
    DirectionRole,
    DirectionTextRole,
    PeerDeviceIdRole,
    PeerDisplayNameRole,
    DisplayNameRole,
    StateRole,
    StateTextRole,
    CompletedBytesRole,
    TotalBytesRole,
    CompletedFilesRole,
    TotalFilesRole,
    ProgressValueRole,
    ProgressPercentRole,
    ProgressTextRole,
    CurrentPathRole,
    ErrorTextRole,
    CanPauseRole,
    CanResumeRole,
    CanCancelRole,
    CanRetryRole,
    IsTerminalRole,
    IsHistoricalRole,
    CreatedUtcRole,
    FinishedUtcRole,
    SpeedTextRole,
    EtaTextRole,
    HasHistoryDetailsRole,
    CanOpenFolderRole,
    CanOpenFileRole,
    AccessibleSummaryRole,
  };
  Q_ENUM(Role)

  using Clock = std::function<qint64()>;

  explicit TransferCenterModel(QObject *parent = nullptr);
  explicit TransferCenterModel(Clock clock, QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  bool upsertTransfer(const ::relaydesk::transfer::TransferSnapshot &snapshot);
  bool removeTransfer(const ::relaydesk::transfer::TransferId &transferId);
  void setTransfers(const QList<::relaydesk::transfer::TransferSnapshot> &snapshots);
  void setHistoryRecords(const QList<::relaydesk::transfer::TransferHistoryRecord> &records);

  [[nodiscard]] int indexOf(const ::relaydesk::transfer::TransferId &transferId) const;
  [[nodiscard]] std::optional<::relaydesk::transfer::TransferSnapshot>
  snapshot(const ::relaydesk::transfer::TransferId &transferId) const;
  [[nodiscard]] std::optional<::relaydesk::transfer::TransferHistoryRecord>
  historyRecord(const ::relaydesk::transfer::TransferId &transferId) const;

  bool requestPause(const ::relaydesk::transfer::TransferId &transferId);
  bool requestResume(const ::relaydesk::transfer::TransferId &transferId);
  bool requestCancel(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::TransferCancelOptions &options = {}
  );
  bool requestRetry(const ::relaydesk::transfer::TransferId &transferId);
  bool requestOpenFolder(const ::relaydesk::transfer::TransferId &transferId);
  bool requestOpenFile(const ::relaydesk::transfer::TransferId &transferId);
  // Durable history intentionally has no source recipe. This flag is scoped
  // to an outgoing session that still exists in the current process.
  void setHistoryRetryAvailable(const ::relaydesk::transfer::TransferId &transferId, bool available);
  void flushDueUpdates();

Q_SIGNALS:
  void pauseRequested(::relaydesk::transfer::TransferId transferId);
  void resumeRequested(::relaydesk::transfer::TransferId transferId);
  void cancelRequested(
      ::relaydesk::transfer::TransferId transferId,
      ::relaydesk::transfer::TransferCancelOptions options
  );
  void retryRequested(::relaydesk::transfer::TransferId transferId);
  void historyRetryRequested(::relaydesk::transfer::TransferId transferId);
  void openFolderRequested(::relaydesk::transfer::TransferHistoryRecord record);
  void openFileRequested(::relaydesk::transfer::TransferHistoryRecord record);
  void notificationRequested(
      ::relaydesk::transfer::TransferSnapshot snapshot, QString title, QString message
  );

private:
  struct Entry
  {
    ::relaydesk::transfer::TransferSnapshot snapshot;
    std::optional<::relaydesk::transfer::TransferHistoryRecord> history;

    [[nodiscard]] bool operator==(const Entry &) const = default;
  };

  struct Notification
  {
    ::relaydesk::transfer::TransferSnapshot snapshot;
    QString title;
    QString message;
  };

  [[nodiscard]] static bool validSnapshot(const ::relaydesk::transfer::TransferSnapshot &snapshot);
  [[nodiscard]] static bool validHistory(const ::relaydesk::transfer::TransferHistoryRecord &record);
  [[nodiscard]] static Entry fromHistory(const ::relaydesk::transfer::TransferHistoryRecord &record);
  [[nodiscard]] bool isProgressOnlyChange(
      const ::relaydesk::transfer::TransferSnapshot &current,
      const ::relaydesk::transfer::TransferSnapshot &next
  ) const;
  [[nodiscard]] int insertionIndex(const Entry &entry, int ignoredIndex = -1) const;
  [[nodiscard]] int compare(const Entry &left, const Entry &right) const;
  bool upsertEntry(Entry entry);
  void scheduleUpdateTimer();
  void enqueueTerminalNotification(const ::relaydesk::transfer::TransferSnapshot &snapshot);
  void flushNotification();
  [[nodiscard]] bool requestControl(
      const ::relaydesk::transfer::TransferId &transferId, Role allowedRole,
      void (TransferCenterModel::*signal)(::relaydesk::transfer::TransferId)
  );
  [[nodiscard]] bool requestHistoryAction(
      const ::relaydesk::transfer::TransferId &transferId, Role allowedRole,
      void (TransferCenterModel::*signal)(::relaydesk::transfer::TransferHistoryRecord)
  );

  QList<Entry> m_entries;
  Clock m_clock;
  QTimer m_updateTimer;
  QHash<::relaydesk::transfer::TransferId, Entry> m_pendingEntries;
  QHash<::relaydesk::transfer::TransferId, qint64> m_lastPublishedMs;
  QSet<::relaydesk::transfer::TransferId> m_notifiedTerminalIds;
  QSet<::relaydesk::transfer::TransferId> m_historyRetryAvailable;
  QList<Notification> m_pendingNotifications;
  std::optional<qint64> m_lastNotificationMs;
};

} // namespace deskflow::relaydesk::model
