// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/Protocol.h"

#include <QString>

#include <optional>

namespace relaydesk::transfer {

enum class OfferDirection
{
  Outgoing,
  Incoming,
};

enum class OfferState
{
  Idle,
  AwaitingPeerDecision,
  AwaitingLocalDecision,
  Accepted,
  Rejected,
  Failed,
};

enum class OfferStateError
{
  None,
  InvalidOffer,
  ActiveOfferExists,
  InvalidState,
  TransferIdMismatch,
  CapabilityUnavailable,
  ConflictPolicyUnavailable,
  InvalidResponse,
};

struct OfferStateResult
{
  OfferStateError error = OfferStateError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == OfferStateError::None;
  }
};

struct OfferSnapshot
{
  TransferId transferId;
  OfferDirection direction = OfferDirection::Outgoing;
  OfferState state = OfferState::Idle;
  QString displayName;
  quint64 totalBytes = 0;
  quint64 fileCount = 0;
  quint64 directoryCount = 0;
  ConflictPolicy requestedConflictPolicy = ConflictPolicy::Ask;
  std::optional<TransferAccept> acceptance;
  std::optional<TransferReject> rejection;
  QString errorMessageKey;

  [[nodiscard]] bool operator==(const OfferSnapshot &) const = default;
};

class TransferOfferStateMachine final
{
public:
  explicit TransferOfferStateMachine(NegotiatedCapabilities capabilities);

  [[nodiscard]] OfferStateResult beginOutgoing(const TransferOffer &offer);
  [[nodiscard]] OfferStateResult receiveIncoming(const TransferOffer &offer);
  [[nodiscard]] OfferStateResult receiveAccept(const TransferAccept &acceptance);
  [[nodiscard]] OfferStateResult receiveReject(const TransferReject &rejection);
  [[nodiscard]] OfferStateResult
  acceptIncoming(ConflictPolicy effectivePolicy, QString logicalDestination, quint64 freeBytes, bool autoAccepted);
  [[nodiscard]] OfferStateResult rejectIncoming(RejectReason reason, QString diagnostic = {});
  [[nodiscard]] OfferStateResult fail(QString errorMessageKey);

  [[nodiscard]] std::optional<OfferSnapshot> snapshot() const;
  void reset();

private:
  [[nodiscard]] OfferStateResult validateOffer(const TransferOffer &offer) const;
  [[nodiscard]] bool supportsPolicy(ConflictPolicy policy) const;

  NegotiatedCapabilities m_capabilities;
  std::optional<OfferSnapshot> m_snapshot;
};

} // namespace relaydesk::transfer
