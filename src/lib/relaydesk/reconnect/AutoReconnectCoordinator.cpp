/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/reconnect/AutoReconnectCoordinator.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace deskflow::relaydesk {

AutoReconnectCoordinator::AutoReconnectCoordinator(
    TrustedDeviceStore &trustedDevices, AddressCandidateProvider &candidateProvider, Connector connector,
    Scheduler scheduler, AutoReconnectOptions options, QObject *parent
)
    : QObject(parent), m_trustedDevices(trustedDevices), m_candidateProvider(candidateProvider),
      m_connector(std::move(connector)), m_scheduler(std::move(scheduler)), m_options(options)
{
  m_options.maxCandidatesPerRound = std::max<qsizetype>(1, m_options.maxCandidatesPerRound);
  m_options.maxRememberedAddresses = std::max<qsizetype>(1, m_options.maxRememberedAddresses);
  m_options.initialRetryDelay = std::max(ReconnectDelay{1}, m_options.initialRetryDelay);
  m_options.maxRetryDelay = std::max(m_options.initialRetryDelay, m_options.maxRetryDelay);

  if (!m_scheduler) {
    m_scheduler = [this](ReconnectDelay delay, std::function<void()> callback) {
      QTimer::singleShot(delay, this, std::move(callback));
    };
  }

  connect(
      &m_candidateProvider, &AddressCandidateProvider::candidatesResolved, this,
      &AutoReconnectCoordinator::candidatesReady
  );
  connect(
      &m_candidateProvider, &AddressCandidateProvider::resolutionFailed, this,
      [this](const QString &diagnostic) {
        if (m_hasRequest && m_waitingForCandidates) {
          m_waitingForCandidates = false;
          scheduleRetry(m_generation, diagnostic);
        }
      }
  );
}

void AutoReconnectCoordinator::start(AutoReconnectRequest request)
{
  ++m_generation;
  m_request = std::move(request);
  m_hasRequest = true;
  m_connected = false;
  m_retryAttempt = 0;
  beginRound();
}

void AutoReconnectCoordinator::networkAvailable()
{
  if (!m_hasRequest || m_connected) {
    return;
  }
  ++m_generation;
  m_retryAttempt = 0;
  beginRound();
}

void AutoReconnectCoordinator::stop()
{
  ++m_generation;
  m_hasRequest = false;
  m_request.reset();
  m_connected = false;
  m_waitingForCandidates = false;
  m_candidates.clear();
}

void AutoReconnectCoordinator::beginRound()
{
  const auto pinning = verifyTrustedDevice();
  if (!pinning.ok()) {
    m_waitingForCandidates = false;
    Q_EMIT trustBlocked(m_request->deviceId, pinning.error, pinning.diagnostic);
    return;
  }

  const auto trustedDevice = m_trustedDevices.find(m_request->deviceId);
  if (!trustedDevice.has_value()) {
    Q_EMIT trustBlocked(
        m_request->deviceId, PeerPinningError::UnknownPeer, QStringLiteral("TLS peer device is not paired")
    );
    return;
  }

  QList<QHostAddress> recentAddresses;
  for (const auto &addressText : trustedDevice->lastAddresses) {
    QHostAddress address;
    if (address.setAddress(addressText)) {
      recentAddresses.append(address);
    }
  }

  m_candidates.clear();
  m_nextCandidate = 0;
  m_waitingForCandidates = true;
  m_candidateProvider.resolveCandidates({
      .settings = m_request->settings,
      .recentSuccessfulAddresses = std::move(recentAddresses),
      .discoveredAddresses = m_request->discoveredAddresses,
      .inputPort = m_request->inputPort,
      .filePort = m_request->filePort,
  });
}

PeerPinningResult AutoReconnectCoordinator::verifyTrustedDevice() const
{
  const auto trustedDevice = m_trustedDevices.find(m_request->deviceId);
  if (!trustedDevice.has_value()) {
    return {
        .error = PeerPinningError::UnknownPeer,
        .diagnostic = QStringLiteral("TLS peer device is not paired"),
    };
  }
  if (trustedDevice->revoked) {
    return {
        .error = PeerPinningError::RevokedPeer,
        .diagnostic = QStringLiteral("TLS peer trust was revoked"),
    };
  }
  return {};
}

PeerPinningResult AutoReconnectCoordinator::verifyAuthenticatedPeer(const AuthenticatedReconnectPeer &peer) const
{
  if (peer.deviceId != m_request->deviceId) {
    return {
        .error = PeerPinningError::DeviceIdMismatch,
        .diagnostic = QStringLiteral("TLS authenticated device ID does not match the requested peer"),
    };
  }
  return TlsPeerPinningPolicy::verify(
      m_trustedDevices, peer.deviceId, QByteArrayView(peer.certificateFingerprintSha256)
  );
}

void AutoReconnectCoordinator::candidatesReady(AddressCandidateResult result)
{
  if (!m_hasRequest || !m_waitingForCandidates) {
    return;
  }
  m_waitingForCandidates = false;
  m_candidates = result.candidates.mid(0, m_options.maxCandidatesPerRound);
  m_nextCandidate = 0;
  if (m_candidates.isEmpty()) {
    scheduleRetry(m_generation, QStringLiteral("No usable address candidates were resolved"));
    return;
  }
  tryNextCandidate(m_generation);
}

void AutoReconnectCoordinator::tryNextCandidate(quint64 generation)
{
  if (!m_hasRequest || generation != m_generation) {
    return;
  }

  const auto pinning = verifyTrustedDevice();
  if (!pinning.ok()) {
    Q_EMIT trustBlocked(m_request->deviceId, pinning.error, pinning.diagnostic);
    return;
  }
  if (m_nextCandidate >= m_candidates.size()) {
    scheduleRetry(generation, QStringLiteral("All address candidates failed"));
    return;
  }

  const auto candidate = m_candidates.at(m_nextCandidate++);
  const auto deviceId = m_request->deviceId;
  Q_EMIT connecting(deviceId, candidate);
  QPointer<AutoReconnectCoordinator> guard(this);
  m_connector(
      deviceId, candidate,
      [guard, generation, candidate](AutoReconnectConnectResult result) mutable {
        if (guard != nullptr) {
          guard->candidateFinished(generation, candidate, std::move(result));
        }
      }
  );
}

void AutoReconnectCoordinator::candidateFinished(
    quint64 generation, AddressCandidate candidate, AutoReconnectConnectResult result
)
{
  if (!m_hasRequest || generation != m_generation) {
    return;
  }
  if (result.error == AutoReconnectConnectError::AuthenticationFailed) {
    Q_EMIT trustBlocked(
        m_request->deviceId, PeerPinningError::UnauthenticatedPeer,
        result.diagnostic.isEmpty() ? QStringLiteral("TLS connector could not authenticate the peer")
                                    : result.diagnostic
    );
    return;
  }
  if (result.error != AutoReconnectConnectError::None) {
    tryNextCandidate(generation);
    return;
  }
  if (!result.authenticatedPeer.has_value()) {
    Q_EMIT trustBlocked(
        m_request->deviceId, PeerPinningError::UnauthenticatedPeer,
        QStringLiteral("TLS connector reported success without an authenticated peer identity")
    );
    return;
  }

  const auto pinning = verifyAuthenticatedPeer(*result.authenticatedPeer);
  if (!pinning.ok()) {
    Q_EMIT trustBlocked(m_request->deviceId, pinning.error, pinning.diagnostic);
    return;
  }
  m_connected = true;
  m_retryAttempt = 0;
  rememberSuccessfulAddress(candidate);
  Q_EMIT connected(m_request->deviceId, candidate);
}

void AutoReconnectCoordinator::scheduleRetry(quint64 generation, QString diagnostic)
{
  if (!m_hasRequest || generation != m_generation) {
    return;
  }
  Q_EMIT roundFailed(m_request->deviceId, diagnostic);
  if (m_retryAttempt < m_options.maxRetryDelay.count()) {
    ++m_retryAttempt;
  }
  const auto uncappedDelay = ReconnectDelay{m_options.initialRetryDelay.count() * m_retryAttempt};
  const auto delay = std::min(uncappedDelay, m_options.maxRetryDelay);
  const auto deviceId = m_request->deviceId;
  Q_EMIT retryScheduled(deviceId, delay);
  QPointer<AutoReconnectCoordinator> guard(this);
  m_scheduler(delay, [guard, generation]() {
    if (guard != nullptr && guard->m_hasRequest && generation == guard->m_generation) {
      ++guard->m_generation;
      guard->beginRound();
    }
  });
}

void AutoReconnectCoordinator::rememberSuccessfulAddress(const AddressCandidate &candidate)
{
  auto trustedDevice = m_trustedDevices.find(m_request->deviceId);
  if (!trustedDevice.has_value()) {
    return;
  }
  const auto addressText = candidate.address.toString();
  trustedDevice->lastAddresses.removeAll(addressText);
  trustedDevice->lastAddresses.prepend(addressText);
  trustedDevice->lastAddresses = trustedDevice->lastAddresses.mid(0, m_options.maxRememberedAddresses);

  QString diagnostic;
  if (!m_trustedDevices.upsert(*trustedDevice, &diagnostic)) {
    Q_EMIT persistenceFailed(m_request->deviceId, diagnostic);
    return;
  }
  const auto saved = m_trustedDevices.save();
  if (!saved.ok) {
    Q_EMIT persistenceFailed(m_request->deviceId, saved.diagnostic);
  }
}

} // namespace deskflow::relaydesk
