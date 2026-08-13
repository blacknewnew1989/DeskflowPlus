/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/platform/IPlatformFileSafety.h"
#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/TransferOfferStateMachine.h"

#include <QHash>
#include <QObject>

#include <memory>

class QThreadPool;

namespace deskflow::relaydesk {

// Internal receive-side component consumed by FileTransferRuntime in a later
// composition slice. It deliberately owns no socket and advertises no
// capability; callers must only deliver offers from a channel whose frozen
// file.receive.v1 negotiation has already succeeded.
class IncomingTransferRuntime final : public QObject
{
  Q_OBJECT

public:
  IncomingTransferRuntime(
      IPlatformFileSafety &fileSafety, QThreadPool &workerPool, QObject *parent = nullptr
  );
  ~IncomingTransferRuntime() override;

  Q_DISABLE_COPY_MOVE(IncomingTransferRuntime)

  [[nodiscard]] bool receiveOffer(
      const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted,
      const ::relaydesk::transfer::NegotiatedCapabilities &capabilities,
      const ::relaydesk::transfer::TransferOffer &offer, QString *diagnostic = nullptr
  );
  void accept(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::ReceiveOptions &options
  );
  void reject(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::RejectReason reason
  );

Q_SIGNALS:
  void incomingOffer(::relaydesk::transfer::IncomingOffer offer);
  void transferAccepted(
      deskflow::relaydesk::DeviceId peerDeviceId,
      ::relaydesk::transfer::TransferAccept acceptance
  );
  void transferRejected(
      deskflow::relaydesk::DeviceId peerDeviceId,
      ::relaydesk::transfer::TransferReject rejection
  );
  void transferOperationFinished(::relaydesk::transfer::TransferOperationResult result);

private:
  struct Session;
  struct AcceptPreflightResult;

  void finishAcceptPreflight(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::ReceiveOptions options, AcceptPreflightResult result
  );
  void publishOperation(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::TransferOperation operation,
      ::relaydesk::transfer::TransferOperationOutcome outcome,
      ::relaydesk::transfer::TransferOperationError error =
          ::relaydesk::transfer::TransferOperationError::None,
      QString diagnostic = {}
  );

  IPlatformFileSafety &m_fileSafety;
  QThreadPool &m_workerPool;
  QHash<::relaydesk::transfer::TransferId, Session *> m_sessions;
};

} // namespace deskflow::relaydesk

