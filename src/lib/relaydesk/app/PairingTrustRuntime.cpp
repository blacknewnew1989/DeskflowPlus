/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/PairingTrustRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"

#include <QAbstractSocket>

#include <utility>

namespace deskflow::relaydesk {

namespace {
PairingOperationResult failure(PairingOperationError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}
} // namespace

PairingTrustRuntime::PairingTrustRuntime(
    DeviceInfo localDevice, QString trustedDevicesPath, DeviceDiscoveryRuntime &discovery,
    model::DeviceHomeModel &deviceModel, model::PairingWizardModel &pairingModel,
    PairingTrustRuntimeOptions options, QObject *parent
)
    : QObject(parent), m_discovery(discovery), m_deviceModel(deviceModel),
      m_trustedDevices(std::move(trustedDevicesPath)), m_loadResult(m_trustedDevices.load()),
      m_service(
          std::move(localDevice), m_trustedDevices, options.pairing, std::move(options.clock),
          std::move(options.sasGenerator),
          [&discovery](QByteArray bytes, PairingEndpoint endpoint) {
            QString diagnostic;
            const auto written = discovery.service().sendPeerDatagram(
                bytes, endpoint.address, endpoint.port, &diagnostic
            );
            return PairingTransportResult{.ok = written == bytes.size(), .diagnostic = std::move(diagnostic)};
          },
          this
      ),
      m_endpointResolver(std::move(options.endpointResolver))
{
  pairingModel.bindService(m_service);
  connect(
      &m_discovery.service(), &DiscoveryService::unrecognizedDatagramReceived, &m_service,
      [this](const QByteArray &datagram, const QHostAddress &address, quint16 port) {
        if (!isReady()) {
          Q_EMIT operationFailed(failure(PairingOperationError::PersistenceFailed, m_loadResult.diagnostic));
          return;
        }
        (void)m_service.receiveDatagram(datagram, {.address = address, .port = port});
      }
  );
  connect(&m_service, &IPairingService::pairingChanged, this, [this](const PairingSnapshot &snapshot) {
    updateDevice(snapshot);
    Q_EMIT pairingChanged(snapshot);
  });
  connect(&m_service, &IPairingService::operationFailed, this, &PairingTrustRuntime::operationFailed);
  connect(
      &m_discovery.registry(), &DiscoveryRegistry::deviceAdded, this,
      &PairingTrustRuntime::syncDiscoveredDevice
  );
  connect(
      &m_discovery.registry(), &DiscoveryRegistry::deviceChanged, this,
      &PairingTrustRuntime::syncDiscoveredDevice
  );
  for (auto snapshot : m_discovery.registry().snapshots()) {
    syncDiscoveredDevice(std::move(snapshot));
  }
}

PairingTrustRuntime::~PairingTrustRuntime()
{
  m_service.close();
}

const TrustedDeviceStoreResult &PairingTrustRuntime::loadResult() const
{
  return m_loadResult;
}

bool PairingTrustRuntime::isReady() const
{
  return m_loadResult.ok;
}

PairingService &PairingTrustRuntime::service()
{
  return m_service;
}

const TrustedDeviceStore &PairingTrustRuntime::trustedDevices() const
{
  return m_trustedDevices;
}

PairingOperationResult PairingTrustRuntime::startPairing(const DeviceSnapshot &peer)
{
  if (!isReady()) {
    return failure(PairingOperationError::PersistenceFailed, m_loadResult.diagnostic);
  }
  const auto device = m_discovery.registry().deviceInfo(peer.id);
  if (!device.has_value()) {
    return failure(PairingOperationError::InvalidPeer, QStringLiteral("pairing peer was not discovered"));
  }
  const auto endpoint = endpointFor(peer);
  if (!endpoint.has_value()) {
    return failure(PairingOperationError::InvalidEndpoint, QStringLiteral("pairing peer has no usable endpoint"));
  }
  return m_service.startPairing(peer, device->certificateFingerprintSha256, *endpoint);
}

PairingOperationResult PairingTrustRuntime::confirmMatchingSas(const QUuid &sessionId)
{
  return m_service.confirmMatchingSas(sessionId);
}

PairingOperationResult PairingTrustRuntime::submitDisplayedSas(
    const QUuid &sessionId, const QString &sixDigits
)
{
  return m_service.submitDisplayedSas(sessionId, sixDigits);
}

PairingOperationResult PairingTrustRuntime::cancel(const QUuid &sessionId)
{
  return m_service.cancel(sessionId);
}

PairingOperationResult PairingTrustRuntime::revoke(const DeviceId &deviceId)
{
  auto result = m_service.revoke(deviceId);
  if (!result.ok()) {
    return result;
  }
  auto snapshot = m_deviceModel.snapshot(deviceId);
  if (snapshot.has_value()) {
    snapshot->trusted = false;
    snapshot->autoAcceptFiles = false;
    snapshot->pinnedFingerprint.clear();
    snapshot->presence = DevicePresence::TrustViolation;
    m_deviceModel.upsertRemoteDevice(*snapshot);
  }
  return result;
}

bool PairingTrustRuntime::expireIfNeeded()
{
  return m_service.expireIfNeeded();
}

std::optional<PairingEndpoint> PairingTrustRuntime::endpointFor(const DeviceSnapshot &peer) const
{
  if (m_endpointResolver) {
    return m_endpointResolver(peer);
  }
  for (const auto &address : peer.addresses) {
    if (!address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol) {
      return PairingEndpoint{.address = address, .port = m_discovery.service().destinationPort()};
    }
  }
  return std::nullopt;
}

void PairingTrustRuntime::updateDevice(const PairingSnapshot &pairing)
{
  auto snapshot = m_deviceModel.snapshot(pairing.peer.id).value_or(pairing.peer);
  if (pairing.state == PairingState::Completed) {
    applyTrust(snapshot);
    snapshot.presence = DevicePresence::Online;
  } else if (pairing.state == PairingState::Expired || pairing.state == PairingState::Rejected ||
             pairing.state == PairingState::Failed) {
    applyTrust(snapshot);
    snapshot.presence = snapshot.trusted ? DevicePresence::Online : DevicePresence::Discovered;
  } else {
    snapshot.presence = DevicePresence::Pairing;
    // The advertised fingerprint is only pending pairing evidence. It must not
    // become a pin until the atomic trust commit completes.
    snapshot.trusted = false;
    snapshot.pinnedFingerprint.clear();
  }
  m_deviceModel.upsertRemoteDevice(snapshot);
}

void PairingTrustRuntime::syncDiscoveredDevice(DeviceSnapshot snapshot)
{
  const auto device = m_discovery.registry().deviceInfo(snapshot.id);
  if (!device.has_value()) {
    return;
  }

  if (const auto pairing = m_service.snapshot(); pairing.has_value() && pairing->peer.id == snapshot.id &&
      pairing->state != PairingState::Completed && pairing->state != PairingState::Expired &&
      pairing->state != PairingState::Rejected && pairing->state != PairingState::Failed) {
    snapshot.presence = DevicePresence::Pairing;
    snapshot.trusted = false;
    snapshot.autoAcceptFiles = false;
    snapshot.pinnedFingerprint.clear();
    m_deviceModel.upsertRemoteDevice(snapshot);
    return;
  }

  switch (m_trustedDevices.trustStatus(snapshot.id, device->certificateFingerprintSha256)) {
  case TrustStatus::Trusted:
    applyTrust(snapshot);
    snapshot.presence = DevicePresence::Online;
    break;
  case TrustStatus::Revoked:
  case TrustStatus::FingerprintMismatch:
    applyTrust(snapshot);
    snapshot.trusted = false;
    snapshot.autoAcceptFiles = false;
    snapshot.pinnedFingerprint.clear();
    snapshot.presence = DevicePresence::TrustViolation;
    break;
  case TrustStatus::Unknown:
    snapshot.trusted = false;
    snapshot.autoAcceptFiles = false;
    snapshot.pinnedFingerprint.clear();
    snapshot.presence = DevicePresence::Discovered;
    break;
  }
  m_deviceModel.upsertRemoteDevice(snapshot);
}

void PairingTrustRuntime::applyTrust(DeviceSnapshot &snapshot) const
{
  const auto trusted = m_trustedDevices.find(snapshot.id);
  snapshot.trusted = trusted.has_value() && !trusted->revoked;
  snapshot.autoAcceptFiles = snapshot.trusted && trusted->autoAcceptFiles;
  snapshot.pinnedFingerprint = snapshot.trusted ? trusted->fingerprintSha256 : QByteArray{};
  if (trusted.has_value() && !trusted->alias.isEmpty()) {
    snapshot.alias = trusted->alias;
  }
}

} // namespace deskflow::relaydesk
