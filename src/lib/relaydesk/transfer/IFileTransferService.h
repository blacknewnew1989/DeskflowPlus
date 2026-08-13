/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QList>
#include <QObject>
#include <QUrl>

namespace deskflow::relaydesk {

class IFileTransferService : public QObject
{
  Q_OBJECT

public:
  explicit IFileTransferService(QObject *parent = nullptr) : QObject(parent)
  {
  }
  ~IFileTransferService() override = default;

  Q_DISABLE_COPY_MOVE(IFileTransferService)

  [[nodiscard]] virtual ::relaydesk::transfer::TransferStartResult send(
      const DeviceId &target, const QList<QUrl> &localItems,
      const ::relaydesk::transfer::SendOptions &options
  ) = 0;
  virtual void accept(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::ReceiveOptions &options
  ) = 0;
  virtual void reject(
      const ::relaydesk::transfer::TransferId &transferId, ::relaydesk::transfer::RejectReason reason
  ) = 0;
  virtual void pause(const ::relaydesk::transfer::TransferId &transferId) = 0;
  virtual void resume(const ::relaydesk::transfer::TransferId &transferId) = 0;
  virtual void cancel(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::TransferCancelOptions &options
  ) = 0;
  virtual void retry(const ::relaydesk::transfer::TransferId &transferId) = 0;

  [[nodiscard]] virtual QList<::relaydesk::transfer::TransferSnapshot> activeTransfers() const = 0;

Q_SIGNALS:
  void incomingOffer(::relaydesk::transfer::IncomingOffer offer);
  void transferAdded(::relaydesk::transfer::TransferSnapshot transfer);
  void transferChanged(::relaydesk::transfer::TransferSnapshot transfer);
  void transferRemoved(::relaydesk::transfer::TransferId transferId);
};

} // namespace deskflow::relaydesk
