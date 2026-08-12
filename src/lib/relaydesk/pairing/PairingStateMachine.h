/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceSnapshot.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

#include <chrono>
#include <functional>
#include <optional>

namespace deskflow::relaydesk {

enum class PairingState
{
  Idle,
  Requesting,
  ExchangingTranscript,
  AwaitingUserComparison,
  Confirming,
  Completed,
  Expired,
  Rejected,
  Failed,
};

struct PairingSnapshot
{
  QUuid pairingSessionId;
  DeviceSnapshot peer;
  PairingState state = PairingState::Idle;
  QString sixDigitSas;
  QDateTime expiresAtUtc;
  int attemptsRemaining = 0;
  QString errorMessageKey;

  bool operator==(const PairingSnapshot &) const = default;
};

enum class PairingError
{
  None,
  ActiveSessionExists,
  InvalidFingerprint,
  InvalidSas,
  SessionNotFound,
  InvalidState,
  Expired,
  AttemptsExhausted,
};

struct PairingActionResult
{
  PairingError error = PairingError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PairingError::None;
  }
};

struct PairingOptions
{
  std::chrono::seconds validity{300};
  int attempts = 3;
};

class PairingStateMachine final : public QObject
{
  Q_OBJECT

public:
  using Clock = std::function<QDateTime()>;
  // Returns a value in [0, 999999]. The default uses the system Qt RNG.
  using SasGenerator = std::function<quint32()>;

  explicit PairingStateMachine(
      PairingOptions options = {}, Clock clock = {}, SasGenerator sasGenerator = {}, QObject *parent = nullptr
  );

  [[nodiscard]] PairingActionResult begin(
      DeviceSnapshot peer, QByteArray peerFingerprintSha256, const std::optional<QString> &receivedSas = std::nullopt
  );
  [[nodiscard]] PairingActionResult markTransportReady(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult markTranscriptExchanged(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult confirmMatchingSas(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits);
  [[nodiscard]] PairingActionResult complete(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult cancel(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult fail(const QUuid &sessionId, QString errorMessageKey);
  [[nodiscard]] bool expireIfNeeded();

  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const;
  [[nodiscard]] std::optional<QByteArray> pendingFingerprint(const QUuid &sessionId) const;
  [[nodiscard]] std::optional<QByteArray> confirmedFingerprint(const QUuid &sessionId) const;

Q_SIGNALS:
  void pairingChanged(const deskflow::relaydesk::PairingSnapshot &snapshot);

private:
  [[nodiscard]] PairingActionResult prepareAction(const QUuid &sessionId);
  [[nodiscard]] PairingActionResult transition(const QUuid &sessionId, PairingState required, PairingState next);
  void publish();

  PairingOptions m_options;
  Clock m_clock;
  SasGenerator m_sasGenerator;
  std::optional<PairingSnapshot> m_snapshot;
  QByteArray m_peerFingerprintSha256;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PairingState)
Q_DECLARE_METATYPE(deskflow::relaydesk::PairingSnapshot)
