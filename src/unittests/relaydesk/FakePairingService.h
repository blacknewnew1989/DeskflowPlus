/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/IPairingService.h"

#include <utility>

namespace deskflow::relaydesk::test {

/** A state-controlled test seam for GUI model/widget tests. */
class FakePairingService final : public IPairingService
{
public:
  explicit FakePairingService(QObject *parent = nullptr) : IPairingService(parent)
  {
  }

  PairingOperationResult startPairing(const DeviceId &deviceId) override
  {
    lastStartedDevice = deviceId;
    ++startCount;
    return startResult;
  }

  PairingOperationResult confirmMatchingSas(const QUuid &sessionId) override
  {
    lastConfirmedSession = sessionId;
    ++confirmCount;
    if (confirmResult.ok()) {
      transition(sessionId, PairingState::Confirming);
    }
    return confirmResult;
  }

  PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits) override
  {
    lastSubmittedSession = sessionId;
    lastSubmittedSas = sixDigits;
    ++submitCount;
    if (submitResult.ok()) {
      transition(sessionId, PairingState::Confirming);
    }
    return submitResult;
  }

  PairingOperationResult cancel(const QUuid &sessionId) override
  {
    lastCancelledSession = sessionId;
    ++cancelCount;
    if (cancelResult.ok()) {
      transition(sessionId, PairingState::Rejected, QStringLiteral("pairing.cancelled"));
    }
    return cancelResult;
  }

  PairingOperationResult revoke(const DeviceId &deviceId) override
  {
    lastRevokedDevice = deviceId;
    ++revokeCount;
    return revokeResult;
  }

  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const override
  {
    return currentSnapshot;
  }

  [[nodiscard]] std::optional<QByteArray> pendingFingerprint(const QUuid &sessionId) const override
  {
    if (!currentSnapshot.has_value() || currentSnapshot->pairingSessionId != sessionId ||
        currentFingerprint.isEmpty()) {
      return std::nullopt;
    }
    return currentFingerprint;
  }

  void publish(PairingSnapshot snapshot, QByteArray fingerprint = {})
  {
    currentSnapshot = std::move(snapshot);
    currentFingerprint = std::move(fingerprint);
    Q_EMIT pairingChanged(*currentSnapshot);
  }

  void publishFailure(PairingOperationResult result)
  {
    Q_EMIT operationFailed(std::move(result));
  }

  PairingOperationResult startResult;
  PairingOperationResult confirmResult;
  PairingOperationResult submitResult;
  PairingOperationResult cancelResult;
  PairingOperationResult revokeResult;
  std::optional<PairingSnapshot> currentSnapshot;
  QByteArray currentFingerprint;
  std::optional<DeviceId> lastStartedDevice;
  std::optional<DeviceId> lastRevokedDevice;
  QUuid lastConfirmedSession;
  QUuid lastSubmittedSession;
  QUuid lastCancelledSession;
  QString lastSubmittedSas;
  int startCount = 0;
  int confirmCount = 0;
  int submitCount = 0;
  int cancelCount = 0;
  int revokeCount = 0;

private:
  void transition(const QUuid &sessionId, PairingState state, QString errorMessageKey = {})
  {
    if (!currentSnapshot.has_value() || currentSnapshot->pairingSessionId != sessionId) {
      return;
    }
    currentSnapshot->state = state;
    currentSnapshot->errorMessageKey = std::move(errorMessageKey);
    Q_EMIT pairingChanged(*currentSnapshot);
  }
};

} // namespace deskflow::relaydesk::test
