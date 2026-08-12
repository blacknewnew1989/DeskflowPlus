/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QAbstractListModel>
#include <QList>

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
    AccessibleSummaryRole,
  };
  Q_ENUM(Role)

  explicit TransferCenterModel(QObject *parent = nullptr);

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
  bool requestCancel(const ::relaydesk::transfer::TransferId &transferId);

Q_SIGNALS:
  void pauseRequested(::relaydesk::transfer::TransferSnapshot snapshot);
  void resumeRequested(::relaydesk::transfer::TransferSnapshot snapshot);
  void cancelRequested(::relaydesk::transfer::TransferSnapshot snapshot);

private:
  struct Entry
  {
    ::relaydesk::transfer::TransferSnapshot snapshot;
    std::optional<::relaydesk::transfer::TransferHistoryRecord> history;

    [[nodiscard]] bool operator==(const Entry &) const = default;
  };

  [[nodiscard]] static bool validSnapshot(const ::relaydesk::transfer::TransferSnapshot &snapshot);
  [[nodiscard]] static bool validHistory(const ::relaydesk::transfer::TransferHistoryRecord &record);
  [[nodiscard]] static Entry fromHistory(const ::relaydesk::transfer::TransferHistoryRecord &record);
  [[nodiscard]] int insertionIndex(const Entry &entry, int ignoredIndex = -1) const;
  [[nodiscard]] int compare(const Entry &left, const Entry &right) const;
  bool upsertEntry(Entry entry);
  [[nodiscard]] bool requestControl(
      const ::relaydesk::transfer::TransferId &transferId, Role allowedRole,
      void (TransferCenterModel::*signal)(::relaydesk::transfer::TransferSnapshot)
  );

  QList<Entry> m_entries;
};

} // namespace deskflow::relaydesk::model
