/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingOperation.h"

#include <QObject>

#include <optional>

namespace deskflow::relaydesk {

/**
 * The sole pairing boundary consumed by GUI models and composition code.
 *
 * Device discovery and the runtime resolve transport endpoints and advertised
 * identity evidence. GUI callers can only select a strongly typed device ID.
 */
class IPairingService : public QObject
{
  Q_OBJECT

public:
  explicit IPairingService(QObject *parent = nullptr) : QObject(parent)
  {
  }
  ~IPairingService() override = default;

  Q_DISABLE_COPY_MOVE(IPairingService)

  [[nodiscard]] virtual PairingOperationResult startPairing(const DeviceId &deviceId) = 0;
  [[nodiscard]] virtual PairingOperationResult confirmMatchingSas(const QUuid &sessionId) = 0;
  [[nodiscard]] virtual PairingOperationResult submitDisplayedSas(
      const QUuid &sessionId, const QString &sixDigits
  ) = 0;
  [[nodiscard]] virtual PairingOperationResult cancel(const QUuid &sessionId) = 0;
  [[nodiscard]] virtual PairingOperationResult revoke(const DeviceId &deviceId) = 0;
  [[nodiscard]] virtual std::optional<PairingSnapshot> snapshot() const = 0;
  [[nodiscard]] virtual std::optional<QByteArray> pendingFingerprint(const QUuid &sessionId) const = 0;

Q_SIGNALS:
  void pairingChanged(PairingSnapshot snapshot);
  void operationFailed(PairingOperationResult result);
};

} // namespace deskflow::relaydesk
