/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingManager.h"

#include <QAbstractSocket>
#include <QRegularExpression>

#include <utility>

namespace deskflow::relaydesk {
namespace {

constexpr qsizetype kFingerprintBytes = 32;
const auto kSixDigitPattern = QRegularExpression(QStringLiteral("^[0-9]{6}$"));

PairingOperationResult failure(PairingOperationError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool isTerminal(PairingState state)
{
  return state == PairingState::Completed || state == PairingState::Expired || state == PairingState::Rejected ||
         state == PairingState::Failed;
}

PairingOperationError operationErrorFor(PairingFailureReason reason)
{
  switch (reason) {
  case PairingFailureReason::None:
  case PairingFailureReason::Cancelled:
    return PairingOperationError::InvalidState;
  case PairingFailureReason::CodeMismatch:
  case PairingFailureReason::TooManyAttempts:
    return PairingOperationError::InvalidCode;
  case PairingFailureReason::Expired:
    return PairingOperationError::Expired;
  case PairingFailureReason::TransportFailed:
    return PairingOperationError::SendFailed;
  case PairingFailureReason::TrustStoreWriteFailed:
    return PairingOperationError::PersistenceFailed;
  case PairingFailureReason::CertificateChanged:
    return PairingOperationError::InvalidPeer;
  case PairingFailureReason::DirectConnectionRequired:
    return PairingOperationError::InvalidEndpoint;
  }
  return PairingOperationError::InvalidState;
}

bool isUsableEndpoint(const PairingEndpoint &endpoint)
{
  return endpoint.port != 0 && !endpoint.address.isNull() && !endpoint.address.isMulticast() &&
         !endpoint.address.isBroadcast() && endpoint.address != QHostAddress::AnyIPv4 &&
         endpoint.address != QHostAddress::AnyIPv6 &&
         endpoint.address.protocol() != QAbstractSocket::UnknownNetworkLayerProtocol;
}

DeviceSnapshot snapshotFromDeviceInfo(const DeviceInfo &device, const PairingEndpoint &source)
{
  return {
      .id = device.deviceId,
      .displayName = device.displayName,
      .platform = device.platform,
      .architecture = device.architecture,
      .presence = DevicePresence::Pairing,
      .addresses = {source.address},
      .capabilities = device.capabilities,
  };
}

} // namespace

PairingManager::PairingManager(
    DeviceInfo localDevice, TrustedDeviceStore &trustedDevices, DatagramSender sender, PairingOptions options,
    Clock clock, SasGenerator sasGenerator, QObject *parent
)
    : QObject(parent), m_localDevice(std::move(localDevice)), m_trustedDevices(trustedDevices),
      m_sender(std::move(sender)), m_options(options), m_clock(std::move(clock)),
      m_stateMachine(options, m_clock, std::move(sasGenerator), this)
{
  if (!m_clock) {
    m_clock = []() { return QDateTime::currentDateTimeUtc(); };
  }
  connect(&m_stateMachine, &PairingStateMachine::pairingChanged, this, [this](const PairingSnapshot &snapshot) {
    m_manualSnapshot.reset();
    Q_EMIT pairingChanged(snapshot);
  });
}

PairingOperationResult PairingManager::startPairing(
    DeviceSnapshot peer, QByteArray peerFingerprintSha256, PairingEndpoint endpoint
)
{
  if (m_localDevice.certificateFingerprintSha256.size() != kFingerprintBytes ||
      m_localDevice.deviceId.value().isNull()) {
    return failure(PairingOperationError::InvalidLocalDevice, QStringLiteral("local pairing identity is incomplete"));
  }
  if (peer.id == m_localDevice.deviceId || peerFingerprintSha256.size() != kFingerprintBytes) {
    return failure(PairingOperationError::InvalidPeer, QStringLiteral("pairing peer identity or fingerprint is invalid"));
  }
  if (!isUsableEndpoint(endpoint)) {
    return failure(PairingOperationError::InvalidEndpoint, QStringLiteral("pairing endpoint is invalid"));
  }
  if (hasNonTerminalSession()) {
    return failure(PairingOperationError::ActiveSessionExists, QStringLiteral("a pairing session is already active"));
  }

  peer.presence = DevicePresence::Pairing;
  if (!peer.addresses.contains(endpoint.address)) {
    peer.addresses.prepend(endpoint.address);
  }
  const auto begun = m_stateMachine.begin(peer, peerFingerprintSha256);
  if (!begun.ok()) {
    return resultFromState(begun);
  }
  const auto current = *m_stateMachine.snapshot();
  m_active = ActiveSession{
      .role = Role::Outgoing,
      .wireSessionId = current.pairingSessionId,
      .peerId = current.peer.id,
      .peerFingerprintSha256 = std::move(peerFingerprintSha256),
      .endpoint = std::move(endpoint),
  };

  auto sent = sendMessage(PairingRequest{
      .pairingSessionId = current.pairingSessionId,
      .sender = m_localDevice,
      .expiresAtUtc = current.expiresAtUtc,
  });
  if (!sent.ok()) {
    return failState(sent.error, sent.diagnostic, PairingFailureReason::TransportFailed);
  }
  const auto transportReady = m_stateMachine.markTransportReady(current.pairingSessionId);
  if (!transportReady.ok()) {
    return resultFromState(transportReady);
  }
  return resultFromState(m_stateMachine.markTranscriptExchanged(current.pairingSessionId));
}

PairingOperationResult PairingManager::receiveDatagram(QByteArrayView bytes, PairingEndpoint source)
{
  if (!isUsableEndpoint(source)) {
    return failure(PairingOperationError::InvalidEndpoint, QStringLiteral("pairing datagram source is invalid"));
  }
  const auto decoded = PairingMessageCodec::decode(bytes);
  if (!decoded.ok()) {
    return {
        .error = PairingOperationError::DecodeFailed,
        .messageError = decoded.error,
        .diagnostic = decoded.diagnostic,
    };
  }
  (void)expireIfNeeded();
  return std::visit(
      [this, &source](const auto &message) -> PairingOperationResult {
        using T = std::decay_t<decltype(message)>;
        if constexpr (std::is_same_v<T, PairingRequest>) {
          return handleRequest(message, source);
        } else if constexpr (std::is_same_v<T, PairingCodeSubmission>) {
          return handleSubmission(message, source);
        } else {
          return handleResult(message, source);
        }
      },
      *decoded.message
  );
}

PairingOperationResult PairingManager::confirmMatchingSas(const QUuid &sessionId)
{
  const auto bound = validateBoundMessage(
      sessionId, m_active.has_value() ? m_active->endpoint : PairingEndpoint{}, Role::Outgoing
  );
  if (!bound.ok()) {
    return bound;
  }
  if (const auto current = snapshot(); current.has_value() && current->state == PairingState::Expired) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  if (expireIfNeeded()) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  const auto current = m_stateMachine.snapshot();
  if (!current.has_value() ||
      (current->state != PairingState::AwaitingUserComparison && current->state != PairingState::Confirming)) {
    return failure(PairingOperationError::InvalidState, QStringLiteral("pairing is not awaiting local confirmation"));
  }
  if (m_active->localConfirmed) {
    return failure(PairingOperationError::DuplicateMessage, QStringLiteral("pairing was already locally confirmed"));
  }
  m_active->localConfirmed = true;
  if (m_active->remoteCodeMatched) {
    return finalizeOutgoing();
  }
  Q_EMIT pairingChanged(*current);
  return {};
}

PairingOperationResult PairingManager::submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits)
{
  const auto bound = validateBoundMessage(
      sessionId, m_active.has_value() ? m_active->endpoint : PairingEndpoint{}, Role::Incoming
  );
  if (!bound.ok()) {
    return bound;
  }
  if (const auto current = snapshot(); current.has_value() && current->state == PairingState::Expired) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  if (expireIfNeeded()) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  if (!m_manualSnapshot.has_value() || m_manualSnapshot->state != PairingState::AwaitingUserComparison) {
    return failure(PairingOperationError::InvalidState, QStringLiteral("pairing is not awaiting a displayed code"));
  }
  if (!kSixDigitPattern.match(sixDigits).hasMatch()) {
    return failure(PairingOperationError::InvalidCode, QStringLiteral("pairing code must contain six digits"));
  }

  const auto peer = m_manualSnapshot->peer;
  const auto expiry = m_manualSnapshot->expiresAtUtc;
  const auto begun = m_stateMachine.beginBoundSession(
      peer, m_active->peerFingerprintSha256, sessionId, expiry, sixDigits
  );
  if (!begun.ok()) {
    return resultFromState(begun);
  }
  const auto ready = m_stateMachine.markTransportReady(sessionId);
  if (!ready.ok()) {
    return resultFromState(ready);
  }
  const auto exchanged = m_stateMachine.markTranscriptExchanged(sessionId);
  if (!exchanged.ok()) {
    return resultFromState(exchanged);
  }
  // The initiator is the authoritative comparison side. This local call
  // records that the receiving user explicitly submitted the displayed code.
  const auto submitted = m_stateMachine.submitDisplayedSas(sessionId, sixDigits);
  if (!submitted.ok()) {
    return resultFromState(submitted);
  }
  const auto sent = sendMessage(PairingCodeSubmission{
      .pairingSessionId = sessionId,
      .sender = m_localDevice,
      .sixDigitSas = sixDigits,
  });
  if (!sent.ok()) {
    return failState(sent.error, sent.diagnostic, PairingFailureReason::TransportFailed);
  }
  return {};
}

PairingOperationResult PairingManager::cancel(const QUuid &sessionId)
{
  if (!m_active.has_value() || m_active->wireSessionId != sessionId) {
    return failure(PairingOperationError::SessionNotFound, QStringLiteral("pairing session was not found"));
  }
  if (expireIfNeeded()) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  const auto sent = sendMessage(PairingResultMessage{
      .pairingSessionId = sessionId,
      .accepted = false,
      .failureReason = PairingFailureReason::Cancelled,
  });
  if (m_manualSnapshot.has_value()) {
    m_manualSnapshot->state = PairingState::Rejected;
    m_manualSnapshot->failureReason = PairingFailureReason::Cancelled;
    publishManualSnapshot();
  } else {
    const auto cancelled = m_stateMachine.cancel(sessionId);
    if (!cancelled.ok()) {
      return resultFromState(cancelled);
    }
  }
  return sent;
}

PairingOperationResult PairingManager::revoke(const DeviceId &deviceId)
{
  TrustedDeviceStore staged = m_trustedDevices;
  if (!staged.revoke(deviceId)) {
    return failure(PairingOperationError::RevokeFailed, QStringLiteral("trusted device was not found"));
  }
  const auto saved = staged.save();
  if (!saved.ok) {
    return failure(PairingOperationError::PersistenceFailed, saved.diagnostic);
  }
  m_trustedDevices = std::move(staged);
  return {};
}

bool PairingManager::expireIfNeeded()
{
  if (m_manualSnapshot.has_value()) {
    if (!isTerminal(m_manualSnapshot->state) && nowUtc() >= m_manualSnapshot->expiresAtUtc) {
      m_manualSnapshot->state = PairingState::Expired;
      m_manualSnapshot->failureReason = PairingFailureReason::Expired;
      publishManualSnapshot();
      return true;
    }
    return false;
  }
  return m_stateMachine.expireIfNeeded();
}

std::optional<PairingSnapshot> PairingManager::snapshot() const
{
  if (m_manualSnapshot.has_value()) {
    return m_manualSnapshot;
  }
  return m_stateMachine.snapshot();
}

std::optional<QByteArray> PairingManager::pendingFingerprint(const QUuid &sessionId) const
{
  if (!m_active.has_value() || m_active->wireSessionId != sessionId) {
    return std::nullopt;
  }
  return m_active->peerFingerprintSha256;
}

PairingOperationResult PairingManager::handleRequest(
    const PairingRequest &request, const PairingEndpoint &source
)
{
  if (request.sender.deviceId == m_localDevice.deviceId ||
      request.sender.certificateFingerprintSha256.size() != kFingerprintBytes) {
    return failure(PairingOperationError::InvalidPeer, QStringLiteral("incoming pairing peer is invalid"));
  }
  if (hasNonTerminalSession()) {
    if (m_active.has_value() && m_active->role == Role::Incoming &&
        m_active->wireSessionId == request.pairingSessionId && m_active->peerId == request.sender.deviceId &&
        m_active->endpoint == source) {
      return failure(PairingOperationError::DuplicateMessage, QStringLiteral("pairing request was already received"));
    }
    return failure(PairingOperationError::ActiveSessionExists, QStringLiteral("a pairing session is already active"));
  }
  const auto now = nowUtc();
  const auto expiry = request.expiresAtUtc.toUTC();
  if (!now.isValid() || expiry <= now || expiry > now.addSecs(m_options.validity.count())) {
    return failure(PairingOperationError::Expired, QStringLiteral("incoming pairing request expiry is invalid"));
  }

  const auto peer = snapshotFromDeviceInfo(request.sender, source);
  m_active = ActiveSession{
      .role = Role::Incoming,
      .wireSessionId = request.pairingSessionId,
      .peerId = request.sender.deviceId,
      .peerFingerprintSha256 = request.sender.certificateFingerprintSha256,
      .endpoint = source,
  };
  m_manualSnapshot = PairingSnapshot{
      .pairingSessionId = request.pairingSessionId,
      .peer = peer,
      .state = PairingState::AwaitingUserComparison,
      .expiresAtUtc = expiry,
      .attemptsRemaining = m_options.attempts,
  };
  publishManualSnapshot();
  return {};
}

PairingOperationResult PairingManager::handleSubmission(
    const PairingCodeSubmission &submission, const PairingEndpoint &source
)
{
  const auto bound = validateBoundMessage(submission.pairingSessionId, source, Role::Outgoing);
  if (!bound.ok()) {
    return bound;
  }
  if (submission.sender.deviceId != m_active->peerId ||
      submission.sender.certificateFingerprintSha256 != m_active->peerFingerprintSha256) {
    return failure(PairingOperationError::PeerMismatch, QStringLiteral("pairing submission peer identity changed"));
  }
  if (expireIfNeeded()) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  const auto current = m_stateMachine.snapshot();
  if (!current.has_value() || current->state != PairingState::AwaitingUserComparison) {
    return failure(PairingOperationError::DuplicateMessage, QStringLiteral("pairing code was already submitted"));
  }

  const auto compared = m_stateMachine.submitDisplayedSas(submission.pairingSessionId, submission.sixDigitSas);
  if (!compared.ok()) {
    const auto reason = compared.error == PairingError::AttemptsExhausted
                            ? PairingFailureReason::TooManyAttempts
                            : PairingFailureReason::CodeMismatch;
    if (compared.error != PairingError::AttemptsExhausted) {
      (void)m_stateMachine.reject(submission.pairingSessionId, reason);
    }
    const auto sent = sendMessage(PairingResultMessage{
        .pairingSessionId = submission.pairingSessionId,
        .accepted = false,
        .failureReason = reason,
    });
    if (!sent.ok()) {
      return sent;
    }
    return {
        .error = PairingOperationError::InvalidCode,
        .stateError = compared.error,
        .diagnostic = compared.diagnostic,
    };
  }
  m_active->remoteCodeMatched = true;
  return m_active->localConfirmed ? finalizeOutgoing() : PairingOperationResult{};
}

PairingOperationResult PairingManager::handleResult(
    const PairingResultMessage &message, const PairingEndpoint &source
)
{
  const auto bound = validateBoundMessage(message.pairingSessionId, source, Role::Incoming);
  if (!bound.ok()) {
    return bound;
  }
  if (expireIfNeeded()) {
    return failure(PairingOperationError::Expired, QStringLiteral("pairing session expired"));
  }
  if (!message.accepted && m_manualSnapshot.has_value()) {
    m_manualSnapshot->state = PairingState::Rejected;
    m_manualSnapshot->failureReason = message.failureReason;
    publishManualSnapshot();
    return {};
  }
  const auto current = m_stateMachine.snapshot();
  if (!current.has_value() || current->state != PairingState::Confirming) {
    return failure(PairingOperationError::DuplicateMessage, QStringLiteral("pairing result is out of order"));
  }
  if (!message.accepted) {
    return failState(
        operationErrorFor(message.failureReason), QStringLiteral("remote peer rejected pairing"),
        message.failureReason
    );
  }
  return resultFromTrust(PairingTrustCommitter::commit(
      m_stateMachine, m_trustedDevices, message.pairingSessionId
  ));
}

PairingOperationResult PairingManager::validateBoundMessage(
    const QUuid &sessionId, const PairingEndpoint &source, Role requiredRole
) const
{
  if (!m_active.has_value()) {
    return failure(PairingOperationError::SessionNotFound, QStringLiteral("pairing session was not found"));
  }
  if (m_active->wireSessionId != sessionId) {
    return failure(PairingOperationError::SessionMismatch, QStringLiteral("pairing session ID does not match"));
  }
  if (m_active->endpoint != source) {
    return failure(PairingOperationError::EndpointMismatch, QStringLiteral("pairing source endpoint changed"));
  }
  if (m_active->role != requiredRole) {
    return failure(PairingOperationError::UnexpectedMessage, QStringLiteral("pairing message is out of order"));
  }
  return {};
}

PairingOperationResult PairingManager::sendMessage(const PairingMessage &message)
{
  if (!m_active.has_value() || !m_sender) {
    return failure(PairingOperationError::SendFailed, QStringLiteral("pairing transport is unavailable"));
  }
  QString diagnostic;
  const auto encoded = PairingMessageCodec::encode(message, &diagnostic);
  if (encoded.isEmpty()) {
    return failure(PairingOperationError::InvalidState, std::move(diagnostic));
  }
  const auto sent = m_sender(encoded, m_active->endpoint);
  return sent.ok ? PairingOperationResult{}
                 : failure(PairingOperationError::SendFailed, std::move(sent.diagnostic));
}

PairingOperationResult PairingManager::finalizeOutgoing()
{
  const auto current = m_stateMachine.snapshot();
  if (!current.has_value() || current->state != PairingState::Confirming || !m_active->localConfirmed ||
      !m_active->remoteCodeMatched) {
    return failure(PairingOperationError::InvalidState, QStringLiteral("both pairing confirmations are required"));
  }
  const auto committed = PairingTrustCommitter::commit(
      m_stateMachine, m_trustedDevices, current->pairingSessionId
  );
  if (!committed.ok()) {
    (void)sendMessage(PairingResultMessage{
        .pairingSessionId = current->pairingSessionId,
        .accepted = false,
        .failureReason = PairingFailureReason::TrustStoreWriteFailed,
    });
    return resultFromTrust(committed);
  }
  return sendMessage(PairingResultMessage{
      .pairingSessionId = current->pairingSessionId,
      .accepted = true,
  });
}

PairingOperationResult PairingManager::failState(
    PairingOperationError error, QString diagnostic, PairingFailureReason reason
)
{
  const auto current = snapshot();
  if (current.has_value() && m_stateMachine.snapshot().has_value()) {
    (void)m_stateMachine.fail(current->pairingSessionId, reason);
  }
  return failure(error, std::move(diagnostic));
}

PairingOperationResult PairingManager::resultFromState(const PairingActionResult &result) const
{
  if (result.ok()) {
    return {};
  }
  const auto error = result.error == PairingError::Expired ? PairingOperationError::Expired
                                                            : PairingOperationError::InvalidState;
  return {.error = error, .stateError = result.error, .diagnostic = result.diagnostic};
}

PairingOperationResult PairingManager::resultFromTrust(const PairingTrustCommitResult &result) const
{
  if (result.ok()) {
    return {};
  }
  return {
      .error = result.error == PairingTrustCommitError::PersistenceFailed
                   ? PairingOperationError::PersistenceFailed
                   : PairingOperationError::InvalidState,
      .trustError = result.error,
      .diagnostic = result.diagnostic,
  };
}

bool PairingManager::hasNonTerminalSession() const
{
  const auto current = snapshot();
  return current.has_value() && !isTerminal(current->state);
}

QDateTime PairingManager::nowUtc() const
{
  return m_clock().toUTC();
}

void PairingManager::publishManualSnapshot()
{
  Q_EMIT pairingChanged(*m_manualSnapshot);
}

} // namespace deskflow::relaydesk
