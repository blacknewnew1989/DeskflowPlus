/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/platform/IPlatformFileSafety.h"
#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/TransferCommandCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferOfferStateMachine.h"

#include <QByteArray>
#include <QHash>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class QThreadPool;

namespace relaydesk::transfer {
class TransferRecoveryStore;
}

namespace deskflow::relaydesk {

// Internal receive-side component consumed by FileTransferRuntime in a later
// composition slice. It deliberately owns no socket and advertises no
// capability; callers must only deliver offers from a channel whose frozen
// file.receive.v1 negotiation has already succeeded.
class IncomingTransferRuntime final : public QObject
{
  Q_OBJECT

public:
  using TrustChecker = std::function<bool(const DeviceId &)>;
  using PeerFingerprintProvider = std::function<std::optional<QByteArray>(const DeviceId &)>;

  IncomingTransferRuntime(
      IPlatformFileSafety &fileSafety, QThreadPool &workerPool, TrustChecker trustChecker = {},
      QObject *parent = nullptr
  );
  ~IncomingTransferRuntime() override;

  Q_DISABLE_COPY_MOVE(IncomingTransferRuntime)

  void configureRecovery(
      DeviceId localDeviceId, PeerFingerprintProvider peerFingerprintProvider,
      ::relaydesk::transfer::TransferRecoveryStore *recoveryStore
  );
  void startRecoveryScan();
  void stopRecoveryScan();

  [[nodiscard]] bool receiveOffer(
      const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted,
      const ::relaydesk::transfer::NegotiatedCapabilities &capabilities,
      const ::relaydesk::transfer::TransferOffer &offer, QString *diagnostic = nullptr
  );
  [[nodiscard]] bool receiveOffer(
      const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted, bool peerAllowsAutoAccept,
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
  void resolveIncomingConflict(
      const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId,
      ::relaydesk::transfer::IncomingConflictDecision decision
  );
  [[nodiscard]] bool hasPendingIncomingConflict(
      const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId
  ) const;
  [[nodiscard]] bool receiveCommand(
      const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
      QString *diagnostic = nullptr
  );
  [[nodiscard]] bool applyLocalCommand(
      const ::relaydesk::transfer::TransferCommandMessage &command, DeviceId *peerDeviceId,
      QString *diagnostic = nullptr,
      ::relaydesk::transfer::TransferOperationOutcome *outcome = nullptr
  );
  [[nodiscard]] bool validateLocalCommand(
      const ::relaydesk::transfer::TransferCommandMessage &command, DeviceId *peerDeviceId,
      QString *diagnostic = nullptr
  ) const;
  [[nodiscard]] bool enqueueFrame(
      const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
      QString *diagnostic = nullptr
  );
  [[nodiscard]] bool receiveResumeQuery(
      const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
      QString *diagnostic = nullptr
  );
  void peerDisconnected(const DeviceId &peerDeviceId);
  [[nodiscard]] bool contains(const ::relaydesk::transfer::TransferId &transferId) const;
  [[nodiscard]] QList<::relaydesk::transfer::TransferSnapshot> activeTransfers() const;

Q_SIGNALS:
  void incomingOffer(::relaydesk::transfer::IncomingOffer offer);
  void incomingConflictDecisionRequired(::relaydesk::transfer::IncomingConflictPrompt prompt);
  void incomingConflictCancelRequested(::relaydesk::transfer::TransferId transferId);
  void transferAccepted(
      deskflow::relaydesk::DeviceId peerDeviceId,
      ::relaydesk::transfer::TransferAccept acceptance
  );
  void transferRejected(
      deskflow::relaydesk::DeviceId peerDeviceId,
      ::relaydesk::transfer::TransferReject rejection
  );
  void transferOperationFinished(::relaydesk::transfer::TransferOperationResult result);
  void responseReady(
      deskflow::relaydesk::DeviceId peerDeviceId, ::relaydesk::transfer::Frame frame
  );
  void transferAdded(::relaydesk::transfer::TransferSnapshot transfer);
  void transferChanged(::relaydesk::transfer::TransferSnapshot transfer);
  void pipelineFailed(
      ::relaydesk::transfer::TransferId transferId,
      ::relaydesk::transfer::TransferErrorCode errorCode, QString diagnostic
  );
  void recoveryIssue(QString diagnostic);

private:
  struct Session;
  struct AcceptPreflightResult;
  struct RecoveryHydrationState;
  class ReceivePipeline;

  void finishAcceptPreflight(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::ReceiveOptions options, AcceptPreflightResult result
  );
  void hydrateNextRecoveryState();
  void publishOperation(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::TransferOperation operation,
      ::relaydesk::transfer::TransferOperationOutcome outcome,
      ::relaydesk::transfer::TransferOperationError error =
          ::relaydesk::transfer::TransferOperationError::None,
      QString diagnostic = {}
  );
  [[nodiscard]] bool isCurrentlyTrusted(const Session &session) const;

  IPlatformFileSafety &m_fileSafety;
  QThreadPool &m_workerPool;
  TrustChecker m_trustChecker;
  std::optional<DeviceId> m_localDeviceId;
  PeerFingerprintProvider m_peerFingerprintProvider;
  ::relaydesk::transfer::TransferRecoveryStore *m_recoveryStore = nullptr;
  std::unique_ptr<RecoveryHydrationState> m_recoveryHydration;
  QHash<::relaydesk::transfer::TransferId, Session *> m_sessions;
};

} // namespace deskflow::relaydesk
