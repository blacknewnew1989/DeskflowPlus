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
  m_options.initialRetryDelayMs = std::max(1, m_options.initialRetryDelayMs);
  m_options.maxRetryDelayMs = std::max(m_options.initialRetryDelayMs, m_options.maxRetryDelayMs);

  if (!m_scheduler) {
    m_scheduler = [this](int delayMs, std::function<void()> callback) {
      QTimer::singleShot(delayMs, this, std::move(callback));
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
  const auto pinning = verifyCurrentPeer();
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

PeerPinningResult AutoReconnectCoordinator::verifyCurrentPeer() const
{
  return TlsPeerPinningPolicy::verify(
      m_trustedDevices, m_request->deviceId, QByteArrayView(m_request->presentedFingerprintSha256)
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

  const auto pinning = verifyCurrentPeer();
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
  if (result.error == AutoReconnectConnectError::FingerprintChanged) {
    Q_EMIT trustBlocked(
        m_request->deviceId, PeerPinningError::FingerprintChanged,
        result.diagnostic.isEmpty() ? QStringLiteral("TLS peer certificate fingerprint changed") : result.diagnostic
    );
    return;
  }
  if (!result.ok()) {
    tryNextCandidate(generation);
    return;
  }

  const auto pinning = verifyCurrentPeer();
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
  if (m_retryAttempt < m_options.maxRetryDelayMs) {
    ++m_retryAttempt;
  }
  const qint64 uncappedDelay = qint64(m_options.initialRetryDelayMs) * m_retryAttempt;
  const int delayMs = int(std::min<qint64>(uncappedDelay, m_options.maxRetryDelayMs));
  const auto deviceId = m_request->deviceId;
  Q_EMIT retryScheduled(deviceId, delayMs);
  QPointer<AutoReconnectCoordinator> guard(this);
  m_scheduler(delayMs, [guard, generation]() {
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
