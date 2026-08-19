/* SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "relaydesk/app/AutoReconnectRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/PairingTrustRuntime.h"

namespace deskflow::relaydesk {

AutoReconnectRuntime::AutoReconnectRuntime(
    PairingTrustRuntime &pairing, DeviceDiscoveryRuntime &discovery, FileTransferRuntime &files,
    DiscoverySettings settings, QObject *parent
)
    : QObject(parent), m_pairing(pairing), m_discovery(discovery), m_files(files), m_settings(std::move(settings))
{
  connect(&discovery.registry(), &DiscoveryRegistry::deviceAdded, this, &AutoReconnectRuntime::observe);
  connect(&discovery.registry(), &DiscoveryRegistry::deviceChanged, this, &AutoReconnectRuntime::observe);
  connect(&pairing, &PairingTrustRuntime::trustRevoked, this, &AutoReconnectRuntime::stopPeer);
  connect(&files, &FileTransferRuntime::peerReady, this, [this](const DeviceId &deviceId, const auto &) {
    completeReady(deviceId);
  });
  connect(&files, &FileTransferRuntime::peerDisconnected, this, [this](const DeviceId &deviceId) {
    const auto it = m_coordinators.find(deviceId);
    if (it != m_coordinators.end()) {
      it.value()->networkAvailable();
    }
  });
  connect(&files, &FileTransferRuntime::errorOccurred, this, [this](
      FileTransferRuntimeError, FileTlsError transportError, const QString &diagnostic
  ) {
    const bool authenticationFailure =
        transportError == FileTlsError::PeerCertificateMissing || transportError == FileTlsError::UnknownPeer ||
        transportError == FileTlsError::RevokedPeer || transportError == FileTlsError::FingerprintChanged ||
        transportError == FileTlsError::HelloInvalid || transportError == FileTlsError::NotAuthenticated;
    const auto pending = m_pending.keys();
    for (const auto &deviceId : pending) {
      auto callback = m_pending.take(deviceId);
      callback({
          .error = authenticationFailure ? AutoReconnectConnectError::AuthenticationFailed
                                         : AutoReconnectConnectError::NetworkError,
          .diagnostic = diagnostic,
      });
    }
  });
  for (const auto &snapshot : discovery.registry().snapshots()) {
    observe(snapshot);
  }
}

void AutoReconnectRuntime::stop()
{
  for (auto *coordinator : m_coordinators) coordinator->stop();
  m_pending.clear();
}

void AutoReconnectRuntime::observe(DeviceSnapshot snapshot)
{
  const auto trusted = m_pairing.m_trustedDevices.find(snapshot.id);
  if (!trusted.has_value() || trusted->revoked || snapshot.presence == DevicePresence::TrustViolation) return;
  if (m_files.isPeerReady(snapshot.id)) return;

  if (!m_coordinators.contains(snapshot.id)) {
    auto *provider = new AddressCandidateProvider({}, this);
    auto *coordinator = new AutoReconnectCoordinator(
        m_pairing.m_trustedDevices, *provider,
        [this](const DeviceId &id, const AddressCandidate &candidate, auto callback) {
          connectCandidate(id, candidate, std::move(callback));
        }
    );
    m_providers.insert(snapshot.id, provider);
    m_coordinators.insert(snapshot.id, coordinator);
  }
  const auto info = m_discovery.registry().deviceInfo(snapshot.id);
  if (!info.has_value() || info->filePort == 0) return;
  m_coordinators.value(snapshot.id)->start({
      .deviceId = snapshot.id,
      .discoveredAddresses = snapshot.addresses,
      .settings = m_settings,
      .inputPort = info->inputPort,
      .filePort = info->filePort,
  });
}

void AutoReconnectRuntime::connectCandidate(
    const DeviceId &deviceId, const AddressCandidate &candidate, AutoReconnectCoordinator::ConnectCallback callback
)
{
  if (m_files.isPeerReady(deviceId)) {
    m_pending.insert(deviceId, std::move(callback));
    completeReady(deviceId);
    return;
  }
  m_pending.insert(deviceId, std::move(callback));
  QString diagnostic;
  if (!m_files.connectPeerAt(deviceId, candidate.address, candidate.filePort, &diagnostic)) {
    auto completion = m_pending.take(deviceId);
    completion({.error = AutoReconnectConnectError::NetworkError, .diagnostic = std::move(diagnostic)});
  }
}

void AutoReconnectRuntime::stopPeer(const DeviceId &deviceId)
{
  if (auto *coordinator = m_coordinators.take(deviceId); coordinator != nullptr) {
    coordinator->stop();
    coordinator->deleteLater();
  }
  if (auto *provider = m_providers.take(deviceId); provider != nullptr)
    provider->deleteLater();
  m_pending.remove(deviceId);
  (void)m_files.disconnectPeer(deviceId);
}

void AutoReconnectRuntime::completeReady(const DeviceId &deviceId)
{
  auto callback = m_pending.take(deviceId);
  if (!callback) return;
  const auto trusted = m_pairing.m_trustedDevices.find(deviceId);
  if (!trusted.has_value() || trusted->revoked) {
    callback({.error = AutoReconnectConnectError::AuthenticationFailed,
              .diagnostic = QStringLiteral("TLS-authenticated peer is no longer trusted")});
    return;
  }
  callback({.authenticatedPeer = AuthenticatedReconnectPeer{
                .deviceId = deviceId,
                .certificateFingerprintSha256 = trusted->fingerprintSha256,
            }});
}
} // namespace deskflow::relaydesk
