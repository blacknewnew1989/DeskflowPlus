/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include <QDir>
#include <QFutureWatcher>
#include <QStorageInfo>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <limits>
#include <utility>

namespace deskflow::relaydesk {

struct IncomingTransferRuntime::Session
{
  Session(
      DeviceId peerDeviceId, QString peerName, bool trusted,
      ::relaydesk::transfer::TransferOffer incomingOffer,
      ::relaydesk::transfer::NegotiatedCapabilities capabilities
  )
      : peer(std::move(peerDeviceId)), peerDisplayName(std::move(peerName)), peerTrusted(trusted),
        offer(std::move(incomingOffer)), stateMachine(std::move(capabilities))
  {
  }

  DeviceId peer;
  QString peerDisplayName;
  bool peerTrusted = false;
  ::relaydesk::transfer::TransferOffer offer;
  ::relaydesk::transfer::TransferOfferStateMachine stateMachine;
  bool acceptPreflightPending = false;
};

struct IncomingTransferRuntime::AcceptPreflightResult
{
  FileSafetyResult safety;
  quint64 freeBytes = 0;
};

namespace {

void setDiagnostic(QString *output, QString diagnostic)
{
  if (output != nullptr) {
    *output = std::move(diagnostic);
  }
}

QString logicalDestination(const QString &receiveRoot)
{
  const QString name = QDir::cleanPath(receiveRoot).section(QLatin1Char('/'), -1);
  return name.trimmed().isEmpty() ? QStringLiteral("RelayDesk") : name;
}

} // namespace

IncomingTransferRuntime::IncomingTransferRuntime(
    IPlatformFileSafety &fileSafety, QThreadPool &workerPool, QObject *parent
)
    : QObject(parent), m_fileSafety(fileSafety), m_workerPool(workerPool)
{
}

IncomingTransferRuntime::~IncomingTransferRuntime()
{
  qDeleteAll(m_sessions);
  m_sessions.clear();
}

bool IncomingTransferRuntime::receiveOffer(
    const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted,
    const ::relaydesk::transfer::NegotiatedCapabilities &capabilities,
    const ::relaydesk::transfer::TransferOffer &offer, QString *diagnostic
)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (QThread::currentThread() != thread()) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer runtime must be called on its owning thread"));
    return false;
  }
  if (!capabilities.localCanReceiveFiles) {
    setDiagnostic(diagnostic, QStringLiteral("incoming offer requires negotiated file.receive.v1"));
    return false;
  }
  if (m_sessions.contains(offer.transferId)) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer ID is already known"));
    return false;
  }

  auto *session = new Session(
      peerDeviceId, std::move(peerDisplayName), peerTrusted, offer, capabilities
  );
  const auto received = session->stateMachine.receiveIncoming(offer);
  if (!received.ok()) {
    setDiagnostic(diagnostic, received.diagnostic);
    delete session;
    return false;
  }
  m_sessions.insert(offer.transferId, session);
  Q_EMIT incomingOffer({
      .peerDeviceId = session->peer,
      .peerDisplayName = session->peerDisplayName,
      .offer = session->offer,
      .peerTrusted = session->peerTrusted,
      .mayAutoAccept = false,
  });
  return true;
}

void IncomingTransferRuntime::accept(
    const ::relaydesk::transfer::TransferId &transferId,
    const ::relaydesk::transfer::ReceiveOptions &options
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Incoming transfer ID is unknown")
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  if (snapshot.has_value() && snapshot->state == OfferState::Accepted) {
    publishOperation(transferId, TransferOperation::Accept, TransferOperationOutcome::Idempotent);
    return;
  }
  const bool invalidDestination =
      !options.destinationRoot.isEmpty() && !QDir::isAbsolutePath(options.destinationRoot);
  const bool invalidAutomaticAcceptance =
      options.acceptanceOrigin == AcceptanceOrigin::TrustedDevicePolicy && !session->peerTrusted;
  if (session->acceptPreflightPending || !snapshot.has_value() ||
      snapshot->state != OfferState::AwaitingLocalDecision || invalidDestination ||
      invalidAutomaticAcceptance) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState,
        session->acceptPreflightPending
            ? QStringLiteral("Receive-root preflight is already pending")
            : QStringLiteral("Incoming transfer cannot be accepted with the supplied state and options")
    );
    return;
  }
  if (options.destinationRoot.isEmpty()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Receive root is empty")
    );
    return;
  }

  session->acceptPreflightPending = true;
  auto *watcher = new QFutureWatcher<AcceptPreflightResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, options, watcher]() mutable {
    auto result = watcher->result();
    watcher->deleteLater();
    finishAcceptPreflight(transferId, std::move(options), std::move(result));
  });
  watcher->setFuture(QtConcurrent::run(
      &m_workerPool, [fileSafety = &m_fileSafety, root = options.destinationRoot]() {
    AcceptPreflightResult result;
    result.safety = fileSafety->verifyReceiveRoot({.receiveRoot = root});
    if (!result.safety.ok()) {
      return result;
    }
    const QStorageInfo storage(root);
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0) {
      result.freeBytes = static_cast<quint64>(storage.bytesAvailable());
    } else {
      result.freeBytes = std::numeric_limits<quint64>::max();
    }
    return result;
  }
  ));
}

void IncomingTransferRuntime::reject(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::RejectReason reason
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Incoming transfer ID is unknown")
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  if (snapshot.has_value() && snapshot->state == OfferState::Rejected) {
    publishOperation(transferId, TransferOperation::Reject, TransferOperationOutcome::Idempotent);
    return;
  }
  if (session->acceptPreflightPending || !snapshot.has_value() ||
      snapshot->state != OfferState::AwaitingLocalDecision) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Incoming transfer cannot be rejected now")
    );
    return;
  }
  const auto rejected = session->stateMachine.rejectIncoming(reason);
  if (!rejected.ok()) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, rejected.diagnostic
    );
    return;
  }
  const auto updated = session->stateMachine.snapshot();
  Q_ASSERT(updated.has_value() && updated->rejection.has_value());
  Q_EMIT transferRejected(session->peer, *updated->rejection);
  publishOperation(transferId, TransferOperation::Reject, TransferOperationOutcome::Applied);
}

void IncomingTransferRuntime::finishAcceptPreflight(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::ReceiveOptions options, AcceptPreflightResult result
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr || !session->acceptPreflightPending) {
    return;
  }
  session->acceptPreflightPending = false;
  if (!result.safety.ok()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, result.safety.diagnostic
    );
    return;
  }
  const auto accepted = session->stateMachine.acceptIncoming(
      options.conflictPolicy, logicalDestination(options.destinationRoot), result.freeBytes,
      options.acceptanceOrigin
  );
  if (!accepted.ok()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, accepted.diagnostic
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  Q_ASSERT(snapshot.has_value() && snapshot->acceptance.has_value());
  Q_EMIT transferAccepted(session->peer, *snapshot->acceptance);
  publishOperation(transferId, TransferOperation::Accept, TransferOperationOutcome::Applied);
}

void IncomingTransferRuntime::publishOperation(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::TransferOperation operation,
    ::relaydesk::transfer::TransferOperationOutcome outcome,
    ::relaydesk::transfer::TransferOperationError error, QString diagnostic
)
{
  Q_EMIT transferOperationFinished({
      .transferId = transferId,
      .operation = operation,
      .outcome = outcome,
      .error = error,
      .diagnostic = std::move(diagnostic),
  });
}

} // namespace deskflow::relaydesk
