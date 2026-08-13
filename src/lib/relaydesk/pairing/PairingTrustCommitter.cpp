/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingTrustCommitter.h"

#include <QHostAddress>

#include <utility>

namespace deskflow::relaydesk {
namespace {

PairingTrustCommitResult failure(PairingTrustCommitError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

QStringList addressStrings(const QList<QHostAddress> &addresses)
{
  QStringList result;
  for (const auto &address : addresses) {
    if (!address.isNull()) {
      result.append(address.toString());
    }
  }
  return result;
}

} // namespace

PairingTrustCommitResult PairingTrustCommitter::commit(
    PairingStateMachine &stateMachine, TrustedDeviceStore &store, const QUuid &sessionId,
    PairingTrustSelection selection
)
{
  const auto snapshot = stateMachine.snapshot();
  const auto fingerprint = stateMachine.pendingFingerprint(sessionId);
  if (!snapshot.has_value() || snapshot->pairingSessionId != sessionId || snapshot->state != PairingState::Confirming ||
      !fingerprint.has_value()) {
    return failure(
        PairingTrustCommitError::SessionNotConfirming, QStringLiteral("pairing session is not ready to persist trust")
    );
  }
  if (snapshot->peer.id.value().isNull() || snapshot->peer.platform.trimmed().isEmpty()) {
    return failure(PairingTrustCommitError::InvalidPeer, QStringLiteral("pairing peer identity is incomplete"));
  }

  const auto existing = store.find(snapshot->peer.id);
  selection.alias = selection.alias.trimmed();
  if (selection.alias.isEmpty()) {
    if (existing.has_value() && !existing->alias.isEmpty()) {
      selection.alias = existing->alias;
    } else if (!snapshot->peer.alias.trimmed().isEmpty()) {
      selection.alias = snapshot->peer.alias.trimmed();
    } else {
      selection.alias = snapshot->peer.displayName.trimmed();
    }
  }

  TrustedDevice record{
      .deviceId = snapshot->peer.id,
      .alias = selection.alias,
      .platform = snapshot->peer.platform,
      .fingerprintSha256 = *fingerprint,
      .lastAddresses = addressStrings(snapshot->peer.addresses),
      .autoAcceptFiles = selection.autoAcceptFiles,
      .revoked = false,
  };

  TrustedDeviceStore previous = store;
  TrustedDeviceStore staged = store;
  QString diagnostic;
  if (!staged.upsert(std::move(record), &diagnostic)) {
    return failure(PairingTrustCommitError::InvalidTrustRecord, std::move(diagnostic));
  }

  const auto saveResult = staged.save();
  if (!saveResult.ok) {
    // save() can update the primary before a backup write fails. Restore the
    // previous in-memory snapshot so a failed confirmation never leaves a new
    // trusted fingerprint active on the next launch.
    const auto rollbackResult = store.save();
    QString combined = saveResult.diagnostic;
    if (!rollbackResult.ok) {
      combined += QStringLiteral("; rollback failed: %1").arg(rollbackResult.diagnostic);
    }
    const auto failed = stateMachine.fail(sessionId, QStringLiteral("pairing.trust_store_write_failed"));
    if (!failed.ok()) {
      combined += QStringLiteral("; state transition failed: %1").arg(failed.diagnostic);
    }
    return failure(PairingTrustCommitError::PersistenceFailed, std::move(combined));
  }

  // Make the persisted trust visible before Completed is published. Runtime
  // observers synchronously consume that transition to update device cards and
  // TLS pinning state.
  store = std::move(staged);
  const auto completed = stateMachine.complete(sessionId);
  if (!completed.ok()) {
    store = std::move(previous);
    const auto rollbackResult = store.save();
    QString combined = completed.diagnostic;
    if (!rollbackResult.ok) {
      combined += QStringLiteral("; rollback failed: %1").arg(rollbackResult.diagnostic);
    }
    return failure(PairingTrustCommitError::TransitionFailed, std::move(combined));
  }

  return {};
}

} // namespace deskflow::relaydesk
