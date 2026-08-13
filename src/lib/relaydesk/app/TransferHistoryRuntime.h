/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferHistoryStore.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"

#include <QObject>
#include <QSet>

#include <memory>

class QMutex;

namespace deskflow::relaydesk::model {
class IncomingOfferModel;
class TransferCenterModel;
} // namespace deskflow::relaydesk::model

namespace deskflow::relaydesk {

class IFileTransferService;

// Keeps history and free-space probes off the GUI thread. It consumes typed
// service snapshots only; no wire or transport details cross this boundary.
class TransferHistoryRuntime final : public QObject
{
  Q_OBJECT

public:
  TransferHistoryRuntime(
      IFileTransferService &service, model::TransferCenterModel &transfers,
      model::IncomingOfferModel &incomingOffers, QString receiveRoot, QString historyPath,
      QObject *parent = nullptr
  );

  Q_DISABLE_COPY_MOVE(TransferHistoryRuntime)

  void start();
  [[nodiscard]] static std::optional<::relaydesk::transfer::TransferHistoryRecord> recordForSnapshot(
      const ::relaydesk::transfer::TransferSnapshot &snapshot
  );

Q_SIGNALS:
  void historyError(::relaydesk::transfer::TransferHistoryError error, QString diagnostic);

private:
  struct StartupSnapshot;

  void loadSnapshotAsync();
  void persistTerminal(const ::relaydesk::transfer::TransferSnapshot &snapshot);
  model::TransferCenterModel &m_transfers;
  model::IncomingOfferModel &m_incomingOffers;
  QString m_receiveRoot;
  ::relaydesk::transfer::TransferHistoryStore m_store;
  std::shared_ptr<QMutex> m_storeMutex;
  QSet<::relaydesk::transfer::TransferId> m_persistedOrPending;
  QList<::relaydesk::transfer::TransferHistoryRecord> m_records;
  bool m_started = false;
};

} // namespace deskflow::relaydesk
