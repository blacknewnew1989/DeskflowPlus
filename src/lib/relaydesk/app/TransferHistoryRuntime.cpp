/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferHistoryRuntime.h"

#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/transfer/IFileTransferService.h"

#include <QFutureWatcher>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStorageInfo>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace deskflow::relaydesk {

using namespace ::relaydesk::transfer;

struct TransferHistoryRuntime::StartupSnapshot
{
  TransferHistoryOperationResult maintenance;
  TransferHistoryPageResult history;
  quint64 availableBytes = 0;
};

TransferHistoryRuntime::TransferHistoryRuntime(
    IFileTransferService &service, model::TransferCenterModel &transfers,
    model::IncomingOfferModel &incomingOffers, QString receiveRoot, QString historyPath, QObject *parent
)
    : QObject(parent), m_transfers(transfers), m_incomingOffers(incomingOffers),
      m_receiveRoot(std::move(receiveRoot)), m_store(std::move(historyPath)),
      m_storeMutex(std::make_shared<QMutex>())
{
  connect(
      &service, &IFileTransferService::transferChanged, this,
      [this](const TransferSnapshot &snapshot) { persistTerminal(snapshot); }
  );
}

void TransferHistoryRuntime::start()
{
  if (m_started) {
    return;
  }
  m_started = true;
  loadSnapshotAsync();
}

void TransferHistoryRuntime::loadSnapshotAsync()
{
  auto *watcher = new QFutureWatcher<StartupSnapshot>(this);
  connect(watcher, &QFutureWatcher<StartupSnapshot>::finished, this, [this, watcher]() {
    const auto snapshot = watcher->result();
    watcher->deleteLater();
    if (!snapshot.maintenance.ok()) {
      Q_EMIT historyError(snapshot.maintenance.error, snapshot.maintenance.diagnostic);
    } else if (!snapshot.history.ok()) {
      Q_EMIT historyError(snapshot.history.error, snapshot.history.diagnostic);
    } else {
      for (const auto &record : snapshot.history.page.records) {
        m_persistedOrPending.insert(record.transferId);
        const auto existing = std::find_if(m_records.cbegin(), m_records.cend(), [&record](const auto &candidate) {
          return candidate.transferId == record.transferId;
        });
        if (existing == m_records.cend()) {
          m_records.append(record);
        }
      }
      std::sort(m_records.begin(), m_records.end(), [](const auto &left, const auto &right) {
        return left.finishedUtc > right.finishedUtc;
      });
      m_transfers.setHistoryRecords(m_records);
    }
    auto settings = m_incomingOffers.settings();
    settings.availableBytes = snapshot.availableBytes;
    m_incomingOffers.setSettings(settings);
  });

  const auto store = m_store;
  const auto mutex = m_storeMutex;
  const auto receiveRoot = m_receiveRoot;
  watcher->setFuture(QtConcurrent::run([store, mutex, receiveRoot]() {
    StartupSnapshot snapshot;
    {
      const QMutexLocker lock(mutex.get());
      snapshot.maintenance = store.compact();
      if (snapshot.maintenance.ok()) {
        snapshot.history = store.page(0, kDefaultMaximumHistoryEntries);
      }
    }
    QStorageInfo storage(receiveRoot);
    if (!storage.isValid() || !storage.isReady()) {
      storage.setPath(QFileInfo(receiveRoot).absolutePath());
    }
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() > 0) {
      snapshot.availableBytes = static_cast<quint64>(storage.bytesAvailable());
    }
    return snapshot;
  }));
}

void TransferHistoryRuntime::persistTerminal(const TransferSnapshot &snapshot)
{
  const auto record = recordForSnapshot(snapshot);
  if (!record.has_value() || m_persistedOrPending.contains(record->transferId)) {
    return;
  }
  m_persistedOrPending.insert(record->transferId);
  m_transfers.setHistoryRetryAvailable(
      record->transferId, snapshot.direction == TransferDirection::Sending && snapshot.state == TransferState::Failed &&
                              snapshot.canRetry
  );
  m_records.removeIf([&record](const auto &existing) { return existing.transferId == record->transferId; });
  m_records.prepend(*record);
  const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-kDefaultMaximumHistoryAge.count());
  m_records.removeIf([&cutoff](const auto &existing) { return existing.finishedUtc < cutoff; });
  std::sort(m_records.begin(), m_records.end(), [](const auto &left, const auto &right) {
    if (left.finishedUtc != right.finishedUtc) {
      return left.finishedUtc > right.finishedUtc;
    }
    return left.transferId.toBytes() < right.transferId.toBytes();
  });
  if (m_records.size() > kDefaultMaximumHistoryEntries) {
    m_records.erase(m_records.begin() + kDefaultMaximumHistoryEntries, m_records.end());
  }
  (void)m_transfers.removeTransfer(record->transferId);
  m_transfers.setHistoryRecords(m_records);

  auto *watcher = new QFutureWatcher<TransferHistoryOperationResult>(this);
  const auto transferId = record->transferId;
  connect(watcher, &QFutureWatcher<TransferHistoryOperationResult>::finished, this, [this, watcher, transferId]() {
    const auto result = watcher->result();
    watcher->deleteLater();
    if (!result.ok()) {
      m_persistedOrPending.remove(transferId);
      Q_EMIT historyError(result.error, result.diagnostic);
      return;
    }
  });

  const auto store = m_store;
  const auto mutex = m_storeMutex;
  watcher->setFuture(QtConcurrent::run([store, mutex, record = *record]() {
    const QMutexLocker lock(mutex.get());
    return store.append(record);
  }));
}

std::optional<TransferHistoryRecord> TransferHistoryRuntime::recordForSnapshot(const TransferSnapshot &snapshot)
{
  HistoryStatus status;
  switch (snapshot.state) {
  case TransferState::Completed:
    status = HistoryStatus::Completed;
    break;
  case TransferState::Rejected:
    status = HistoryStatus::Rejected;
    break;
  case TransferState::Cancelled:
    status = HistoryStatus::Cancelled;
    break;
  case TransferState::Failed:
    status = HistoryStatus::Failed;
    break;
  default:
    return std::nullopt;
  }
  if (!snapshot.createdUtc.isValid() || !snapshot.finishedUtc.isValid()) {
    return std::nullopt;
  }
  return TransferHistoryRecord{
      .transferId = snapshot.id,
      .peerDeviceId = snapshot.peerId,
      .peerDisplayName = snapshot.peerDisplayName,
      .displayName = snapshot.displayName,
      .direction = snapshot.direction == TransferDirection::Sending ? HistoryDirection::Sending
                                                                   : HistoryDirection::Receiving,
      .fileCount = snapshot.progress.totalFiles,
      .totalBytes = snapshot.progress.totalBytes,
      .startedUtc = snapshot.createdUtc,
      .finishedUtc = snapshot.finishedUtc,
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? snapshot.errorCode : TransferErrorCode::None,
      .completedRelativePath = status == HistoryStatus::Completed && snapshot.direction == TransferDirection::Receiving &&
                                       snapshot.currentRelativeDisplayPath != QStringLiteral(".")
                                   ? snapshot.currentRelativeDisplayPath
                                   : QString{},
      .topLevelTargetRelativePath =
          status == HistoryStatus::Completed && snapshot.direction == TransferDirection::Receiving
              ? snapshot.currentRelativeDisplayPath == QStringLiteral(".")
                    ? QStringLiteral(".")
                    : snapshot.currentRelativeDisplayPath.section(u'/', 0, 0)
              : QString{},
  };
}

} // namespace deskflow::relaydesk
