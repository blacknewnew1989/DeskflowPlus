// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferOfferStateMachine.h"

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

OfferStateResult failure(OfferStateError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool isTerminal(OfferState state)
{
  return state == OfferState::Accepted || state == OfferState::Rejected || state == OfferState::Failed;
}

bool validRejectReason(RejectReason reason)
{
  switch (reason) {
  case RejectReason::UserDeclined:
  case RejectReason::NotTrusted:
  case RejectReason::PolicyDenied:
  case RejectReason::InsufficientSpace:
  case RejectReason::TooManyFiles:
  case RejectReason::PathInvalid:
  case RejectReason::UnsupportedCapability:
  case RejectReason::Busy:
  case RejectReason::InternalError:
    return true;
  }
  return false;
}

} // namespace

TransferOfferStateMachine::TransferOfferStateMachine(NegotiatedCapabilities capabilities)
    : m_capabilities(std::move(capabilities))
{
}

OfferStateResult TransferOfferStateMachine::beginOutgoing(const TransferOffer &offer)
{
  if (m_snapshot.has_value() && !isTerminal(m_snapshot->state)) {
    return failure(OfferStateError::ActiveOfferExists, QStringLiteral("a transfer offer is already active"));
  }
  const auto valid = validateOffer(offer);
  if (!valid.ok()) {
    return valid;
  }
  m_snapshot = OfferSnapshot{
      .transferId = offer.transferId,
      .direction = OfferDirection::Outgoing,
      .state = OfferState::AwaitingPeerDecision,
      .displayName = offer.displayName,
      .totalBytes = offer.totalBytes,
      .fileCount = offer.fileCount,
      .directoryCount = offer.directoryCount,
      .requestedConflictPolicy = offer.requestedConflictPolicy,
  };
  return {};
}

OfferStateResult TransferOfferStateMachine::receiveIncoming(const TransferOffer &offer)
{
  if (m_snapshot.has_value() && !isTerminal(m_snapshot->state)) {
    return failure(OfferStateError::ActiveOfferExists, QStringLiteral("a transfer offer is already active"));
  }
  const auto valid = validateOffer(offer);
  if (!valid.ok()) {
    return valid;
  }
  m_snapshot = OfferSnapshot{
      .transferId = offer.transferId,
      .direction = OfferDirection::Incoming,
      .state = OfferState::AwaitingLocalDecision,
      .displayName = offer.displayName,
      .totalBytes = offer.totalBytes,
      .fileCount = offer.fileCount,
      .directoryCount = offer.directoryCount,
      .requestedConflictPolicy = offer.requestedConflictPolicy,
  };
  return {};
}

OfferStateResult TransferOfferStateMachine::receiveAccept(const TransferAccept &acceptance)
{
  if (!m_snapshot.has_value() || m_snapshot->direction != OfferDirection::Outgoing ||
      m_snapshot->state != OfferState::AwaitingPeerDecision) {
    return failure(OfferStateError::InvalidState, QStringLiteral("no outgoing offer is waiting for acceptance"));
  }
  if (acceptance.transferId != m_snapshot->transferId) {
    return failure(OfferStateError::TransferIdMismatch, QStringLiteral("acceptance belongs to another transfer"));
  }
  if (!supportsPolicy(acceptance.effectiveConflictPolicy)) {
    return failure(
        OfferStateError::ConflictPolicyUnavailable,
        QStringLiteral("acceptance selected an unnegotiated conflict policy")
    );
  }
  if (acceptance.logicalDestination.trimmed().isEmpty() ||
      acceptance.logicalDestination.toUtf8().size() > kMaxControlStringUtf8Bytes) {
    return failure(OfferStateError::InvalidResponse, QStringLiteral("acceptance destination label is invalid"));
  }
  m_snapshot->acceptance = acceptance;
  m_snapshot->state = OfferState::Accepted;
  return {};
}

OfferStateResult TransferOfferStateMachine::receiveReject(const TransferReject &rejection)
{
  if (!m_snapshot.has_value() || m_snapshot->direction != OfferDirection::Outgoing ||
      m_snapshot->state != OfferState::AwaitingPeerDecision) {
    return failure(OfferStateError::InvalidState, QStringLiteral("no outgoing offer is waiting for rejection"));
  }
  if (rejection.transferId != m_snapshot->transferId) {
    return failure(OfferStateError::TransferIdMismatch, QStringLiteral("rejection belongs to another transfer"));
  }
  if (!validRejectReason(rejection.reason) || rejection.diagnostic.toUtf8().size() > kMaxControlStringUtf8Bytes) {
    return failure(OfferStateError::InvalidResponse, QStringLiteral("rejection reason or diagnostic is invalid"));
  }
  m_snapshot->rejection = rejection;
  m_snapshot->state = OfferState::Rejected;
  return {};
}

OfferStateResult TransferOfferStateMachine::acceptIncoming(
    ConflictPolicy effectivePolicy, QString logicalDestination, quint64 freeBytes,
    AcceptanceOrigin origin
)
{
  if (!m_snapshot.has_value() || m_snapshot->direction != OfferDirection::Incoming ||
      m_snapshot->state != OfferState::AwaitingLocalDecision) {
    return failure(OfferStateError::InvalidState, QStringLiteral("no incoming offer is waiting for a local decision"));
  }
  if (!supportsPolicy(effectivePolicy)) {
    return failure(
        OfferStateError::ConflictPolicyUnavailable, QStringLiteral("selected conflict policy was not negotiated")
    );
  }
  logicalDestination = logicalDestination.trimmed();
  if (logicalDestination.isEmpty() || logicalDestination.toUtf8().size() > kMaxControlStringUtf8Bytes) {
    return failure(OfferStateError::InvalidResponse, QStringLiteral("destination label is empty or too long"));
  }
  if (freeBytes < m_snapshot->totalBytes) {
    return failure(OfferStateError::InvalidResponse, QStringLiteral("reported free space is smaller than the offer"));
  }
  m_snapshot->acceptance = TransferAccept{
      .transferId = m_snapshot->transferId,
      .effectiveConflictPolicy = effectivePolicy,
      .logicalDestination = std::move(logicalDestination),
      .freeBytes = freeBytes,
      .autoAccepted = origin == AcceptanceOrigin::TrustedDevicePolicy,
  };
  m_snapshot->state = OfferState::Accepted;
  return {};
}

OfferStateResult TransferOfferStateMachine::rejectIncoming(RejectReason reason, QString diagnostic)
{
  if (!m_snapshot.has_value() || m_snapshot->direction != OfferDirection::Incoming ||
      m_snapshot->state != OfferState::AwaitingLocalDecision) {
    return failure(OfferStateError::InvalidState, QStringLiteral("no incoming offer is waiting for a local decision"));
  }
  if (!validRejectReason(reason) || diagnostic.toUtf8().size() > kMaxControlStringUtf8Bytes) {
    return failure(OfferStateError::InvalidResponse, QStringLiteral("rejection reason or diagnostic is invalid"));
  }
  m_snapshot->rejection = TransferReject{
      .transferId = m_snapshot->transferId,
      .reason = reason,
      .diagnostic = std::move(diagnostic),
  };
  m_snapshot->state = OfferState::Rejected;
  return {};
}

OfferStateResult TransferOfferStateMachine::fail(TransferErrorCode errorCode)
{
  if (!m_snapshot.has_value() || isTerminal(m_snapshot->state) || !isKnownTransferErrorCode(errorCode)) {
    return failure(OfferStateError::InvalidState, QStringLiteral("offer cannot transition to failed"));
  }
  m_snapshot->state = OfferState::Failed;
  m_snapshot->errorCode = errorCode;
  return {};
}

std::optional<OfferSnapshot> TransferOfferStateMachine::snapshot() const
{
  return m_snapshot;
}

void TransferOfferStateMachine::reset()
{
  m_snapshot.reset();
}

OfferStateResult TransferOfferStateMachine::validateOffer(const TransferOffer &offer) const
{
  const bool entryCountOverflows = offer.fileCount > std::numeric_limits<quint64>::max() - offer.directoryCount;
  if (offer.displayName.isEmpty() ||
      offer.displayName.toUtf8().size() > kMaxControlStringUtf8Bytes || offer.manifestSha256.size() != kSha256Bytes ||
      offer.manifestPageCount == 0 || entryCountOverflows ||
      offer.fileCount + offer.directoryCount > m_capabilities.maxManifestEntries) {
    return failure(OfferStateError::InvalidOffer, QStringLiteral("transfer offer fields exceed negotiated bounds"));
  }
  if (offer.directoryCount > 0 && !m_capabilities.features.contains(QStringLiteral("folder.v1"))) {
    return failure(
        OfferStateError::CapabilityUnavailable, QStringLiteral("folder offer requires negotiated folder.v1")
    );
  }
  if (!supportsPolicy(offer.requestedConflictPolicy)) {
    return failure(
        OfferStateError::ConflictPolicyUnavailable, QStringLiteral("requested conflict policy was not negotiated")
    );
  }
  return {};
}

bool TransferOfferStateMachine::supportsPolicy(ConflictPolicy policy) const
{
  return m_capabilities.conflictPolicies.contains(policy);
}

} // namespace relaydesk::transfer
