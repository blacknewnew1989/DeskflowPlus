/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/pairing/PairingMessageCodec.h"
#include "relaydesk/pairing/PairingStateMachine.h"
#include "relaydesk/pairing/PairingTrustCommitter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QByteArray>
#include <QHostAddress>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

#include <functional>
#include <optional>

namespace deskflow::relaydesk {

struct PairingEndpoint
{
  QHostAddress address;
  quint16 port = 0;

  bool operator==(const PairingEndpoint &) const = default;
};

enum class PairingOperationError
{
  None,
  InvalidLocalDevice,
  InvalidPeer,
  InvalidEndpoint,
  ActiveSessionExists,
  SessionNotFound,
  SessionMismatch,
  PeerMismatch,
  EndpointMismatch,
  Expired,
  InvalidCode,
  UnexpectedMessage,
  DuplicateMessage,
  DecodeFailed,
  InvalidState,
  SendFailed,
  PersistenceFailed,
  RevokeFailed,
};

struct PairingOperationResult
{
  PairingOperationError error = PairingOperationError::None;
  PairingMessageError messageError = PairingMessageError::None;
  PairingError stateError = PairingError::None;
  PairingTrustCommitError trustError = PairingTrustCommitError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PairingOperationError::None;
  }
};

struct PairingTransportResult
{
  bool ok = false;
  QString diagnostic;
};

class PairingManager final : public QObject
{
  Q_OBJECT

public:
  using Clock = PairingStateMachine::Clock;
  using SasGenerator = PairingStateMachine::SasGenerator;
  using DatagramSender = std::function<PairingTransportResult(QByteArray, PairingEndpoint)>;

  PairingManager(
      DeviceInfo localDevice, TrustedDeviceStore &trustedDevices, DatagramSender sender,
      PairingOptions options = {}, Clock clock = {}, SasGenerator sasGenerator = {}, QObject *parent = nullptr
  );

  [[nodiscard]] PairingOperationResult startPairing(
      DeviceSnapshot peer, QByteArray peerFingerprintSha256, PairingEndpoint endpoint
  );
  [[nodiscard]] PairingOperationResult receiveDatagram(QByteArrayView bytes, PairingEndpoint source);
  [[nodiscard]] PairingOperationResult confirmMatchingSas(const QUuid &sessionId);
  [[nodiscard]] PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits);
  [[nodiscard]] PairingOperationResult cancel(const QUuid &sessionId);
  [[nodiscard]] PairingOperationResult revoke(const DeviceId &deviceId);
  [[nodiscard]] bool expireIfNeeded();
  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const;

Q_SIGNALS:
  void pairingChanged(PairingSnapshot snapshot);
  void operationFailed(PairingOperationResult result);

private:
  enum class Role
  {
    Outgoing,
    Incoming,
  };

  struct ActiveSession
  {
    Role role;
    QUuid wireSessionId;
    DeviceId peerId;
    QByteArray peerFingerprintSha256;
    PairingEndpoint endpoint;
    bool localConfirmed = false;
    bool remoteCodeMatched = false;
  };

  [[nodiscard]] PairingOperationResult handleRequest(const PairingRequest &request, const PairingEndpoint &source);
  [[nodiscard]] PairingOperationResult
  handleSubmission(const PairingCodeSubmission &submission, const PairingEndpoint &source);
  [[nodiscard]] PairingOperationResult handleResult(
      const PairingResultMessage &message, const PairingEndpoint &source
  );
  [[nodiscard]] PairingOperationResult validateBoundMessage(
      const QUuid &sessionId, const PairingEndpoint &source, Role requiredRole
  ) const;
  [[nodiscard]] PairingOperationResult sendMessage(const PairingMessage &message);
  [[nodiscard]] PairingOperationResult finalizeOutgoing();
  [[nodiscard]] PairingOperationResult failState(
      PairingOperationError error, QString diagnostic, QString errorMessageKey
  );
  [[nodiscard]] PairingOperationResult resultFromState(const PairingActionResult &result) const;
  [[nodiscard]] PairingOperationResult resultFromTrust(const PairingTrustCommitResult &result) const;
  [[nodiscard]] bool hasNonTerminalSession() const;
  [[nodiscard]] QDateTime nowUtc() const;
  void publishManualSnapshot();

  DeviceInfo m_localDevice;
  TrustedDeviceStore &m_trustedDevices;
  DatagramSender m_sender;
  PairingOptions m_options;
  Clock m_clock;
  PairingStateMachine m_stateMachine;
  std::optional<ActiveSession> m_active;
  std::optional<PairingSnapshot> m_manualSnapshot;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PairingEndpoint)
Q_DECLARE_METATYPE(deskflow::relaydesk::PairingOperationError)
Q_DECLARE_METATYPE(deskflow::relaydesk::PairingOperationResult)
