/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/PairingTrustRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/pairing/PairingService.h"

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
    model::DeviceHomeModel &deviceModel, model::PairingWizardModel &pairingModel, PairingTrustRuntimeOptions options,
    QObject *parent
)
    : IPairingService(parent),
      m_discovery(discovery),
      m_deviceModel(deviceModel),
      m_trustedDevices(std::move(trustedDevicesPath)),
      m_loadResult(m_trustedDevices.load()),
      m_service(
          std::make_unique<PairingService>(
              std::move(localDevice), m_trustedDevices, options.pairing, std::move(options.clock),
              std::move(options.sasGenerator),
              [&discovery](QByteArray bytes, PairingEndpoint endpoint) {
                QString diagnostic;
                const auto written =
                    discovery.service().sendPeerDatagram(bytes, endpoint.address, endpoint.port, &diagnostic);
                return PairingTransportResult{.ok = written == bytes.size(), .diagnostic = std::move(diagnostic)};
              },
              this
          )
      ),
      m_endpointResolver(std::move(options.endpointResolver))
{
  pairingModel.bindService(*this);
  connect(
      &m_discovery.service(), &DiscoveryService::unrecognizedDatagramReceived, m_service.get(),
      [this](const QByteArray &datagram, const QHostAddress &address, quint16 port) {
        if (!isReady()) {
          (void)reportPreflightFailure(failure(PairingOperationError::PersistenceFailed, m_loadResult.diagnostic));
          return;
        }
        (void)m_service->receiveDatagram(datagram, {.address = address, .port = port});
      }
  );
  connect(m_service.get(), &PairingService::pairingChanged, this, [this](const PairingSnapshot &snapshot) {
    updateDevice(snapshot);
    Q_EMIT pairingChanged(snapshot);
  });
  connect(m_service.get(), &PairingService::operationFailed, this, &IPairingService::operationFailed);
  connect(&m_discovery.registry(), &DiscoveryRegistry::deviceAdded, this, &PairingTrustRuntime::syncDiscoveredDevice);
  connect(&m_discovery.registry(), &DiscoveryRegistry::deviceChanged, this, &PairingTrustRuntime::syncDiscoveredDevice);
  for (auto snapshot : m_discovery.registry().snapshots()) {
    syncDiscoveredDevice(std::move(snapshot));
  }
}

PairingTrustRuntime::~PairingTrustRuntime()
{
  m_service->close();
}

const TrustedDeviceStoreResult &PairingTrustRuntime::loadResult() const
{
  return m_loadResult;
}

bool PairingTrustRuntime::isReady() const
{
  return m_loadResult.ok;
}

const TrustedDeviceStore &PairingTrustRuntime::trustedDevices() const
{
  return m_trustedDevices;
}

PairingOperationResult PairingTrustRuntime::startPairing(const DeviceId &deviceId)
{
  if (!isReady()) {
    return reportPreflightFailure(failure(PairingOperationError::PersistenceFailed, m_loadResult.diagnostic));
  }
  const auto peer = m_discovery.registry().snapshot(deviceId);
  const auto device = m_discovery.registry().deviceInfo(deviceId);
  if (!peer.has_value() || !device.has_value() || device->deviceId != deviceId ||
      device->certificateFingerprintSha256.size() != 32) {
    return reportPreflightFailure(
        failure(PairingOperationError::InvalidPeer, QStringLiteral("pairing peer identity is not currently discovered"))
    );
  }
  const auto endpoint = endpointFor(*peer);
  if (!endpoint.has_value()) {
    return reportPreflightFailure(failure(
        PairingOperationError::InvalidEndpoint, QStringLiteral("pairing peer has no usable discovered endpoint")
    ));
  }
  // Discovery identity is only pending evidence. PairingManager binds the
  // authenticated pairing transcript sender identity and fingerprint to this
  // value before PairingTrustCommitter can persist the TLS pin.
  return m_service->startPairing(
      *peer, device->certificateFingerprintSha256, {.address = endpoint->first, .port = endpoint->second}
  );
}

PairingOperationResult PairingTrustRuntime::confirmMatchingSas(const QUuid &sessionId)
{
  return m_service->confirmMatchingSas(sessionId);
}

PairingOperationResult PairingTrustRuntime::submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits)
{
  return m_service->submitDisplayedSas(sessionId, sixDigits);
}

PairingOperationResult PairingTrustRuntime::cancel(const QUuid &sessionId)
{
  return m_service->cancel(sessionId);
}

PairingOperationResult PairingTrustRuntime::revoke(const DeviceId &deviceId)
{
  auto result = m_service->revoke(deviceId);
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
  Q_EMIT trustRevoked(deviceId);
  return result;
}

PairingOperationResult PairingTrustRuntime::setAutoAcceptFiles(const DeviceId &deviceId, bool enabled)
{
  if (!isReady()) {
    return reportPreflightFailure(failure(PairingOperationError::PersistenceFailed, m_loadResult.diagnostic));
  }
  const auto current = m_trustedDevices.find(deviceId);
  if (!current.has_value() || current->revoked) {
    return reportPreflightFailure(failure(PairingOperationError::InvalidPeer, QStringLiteral("device is not trusted")));
  }
  if (current->autoAcceptFiles == enabled)
    return {};

  auto updated = *current;
  updated.autoAcceptFiles = enabled;
  QString diagnostic;
  if (!m_trustedDevices.upsert(updated, &diagnostic)) {
    (void)m_trustedDevices.upsert(*current);
    return reportPreflightFailure(failure(PairingOperationError::PersistenceFailed, std::move(diagnostic)));
  }
  const auto saved = m_trustedDevices.save();
  if (!saved.ok) {
    (void)m_trustedDevices.upsert(*current);
    const auto rollback = m_trustedDevices.save();
    auto diagnostic = saved.diagnostic;
    if (!rollback.ok) {
      diagnostic += QStringLiteral("; rollback failed: %1").arg(rollback.diagnostic);
    }
    return reportPreflightFailure(failure(PairingOperationError::PersistenceFailed, std::move(diagnostic)));
  }
  if (auto snapshot = m_deviceModel.snapshot(deviceId); snapshot.has_value()) {
    snapshot->autoAcceptFiles = enabled;
    m_deviceModel.upsertRemoteDevice(*snapshot);
  }
  return {};
}

std::optional<PairingSnapshot> PairingTrustRuntime::snapshot() const
{
  return m_service->snapshot();
}

std::optional<QByteArray> PairingTrustRuntime::pendingFingerprint(const QUuid &sessionId) const
{
  return m_service->pendingFingerprint(sessionId);
}

bool PairingTrustRuntime::expireIfNeeded()
{
  return m_service->expireIfNeeded();
}

PairingOperationResult PairingTrustRuntime::reportPreflightFailure(PairingOperationResult result)
{
  Q_ASSERT(!result.ok());
  Q_EMIT operationFailed(result);
  return result;
}

std::optional<std::pair<QHostAddress, quint16>> PairingTrustRuntime::endpointFor(const DeviceSnapshot &peer) const
{
  if (m_endpointResolver) {
    return m_endpointResolver(peer.id);
  }
  for (const auto &address : peer.addresses) {
    if (!address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol) {
      return std::pair(address, m_discovery.service().destinationPort());
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

  if (const auto pairing = m_service->snapshot();
      pairing.has_value() && pairing->peer.id == snapshot.id && pairing->state != PairingState::Completed &&
      pairing->state != PairingState::Expired && pairing->state != PairingState::Rejected &&
      pairing->state != PairingState::Failed) {
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
