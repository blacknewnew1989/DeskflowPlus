/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/IPairingService.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QHostAddress>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace deskflow::relaydesk::model {
class DeviceHomeModel;
class PairingWizardModel;
}

namespace deskflow::relaydesk {

class DeviceDiscoveryRuntime;
class PairingService;
class AutoReconnectRuntime;

struct PairingTrustRuntimeOptions
{
  PairingOptions pairing;
  PairingStateMachine::Clock clock;
  PairingStateMachine::SasGenerator sasGenerator;
  std::function<std::optional<std::pair<QHostAddress, quint16>>(const DeviceId &)> endpointResolver;
};

class PairingTrustRuntime final : public IPairingService
{
  Q_OBJECT

public:
  PairingTrustRuntime(
      DeviceInfo localDevice, QString trustedDevicesPath, DeviceDiscoveryRuntime &discovery,
      model::DeviceHomeModel &deviceModel, model::PairingWizardModel &pairingModel,
      PairingTrustRuntimeOptions options = {}, QObject *parent = nullptr
  );
  ~PairingTrustRuntime() override;

  Q_DISABLE_COPY_MOVE(PairingTrustRuntime)

  [[nodiscard]] const TrustedDeviceStoreResult &loadResult() const;
  [[nodiscard]] bool isReady() const;
  [[nodiscard]] const TrustedDeviceStore &trustedDevices() const;

  [[nodiscard]] PairingOperationResult startPairing(const DeviceId &deviceId) override;
  [[nodiscard]] PairingOperationResult confirmMatchingSas(const QUuid &sessionId) override;
  [[nodiscard]] PairingOperationResult submitDisplayedSas(
      const QUuid &sessionId, const QString &sixDigits
  ) override;
  [[nodiscard]] PairingOperationResult cancel(const QUuid &sessionId) override;
  [[nodiscard]] PairingOperationResult revoke(const DeviceId &deviceId) override;
  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const override;
  [[nodiscard]] std::optional<QByteArray> pendingFingerprint(const QUuid &sessionId) const override;
  [[nodiscard]] bool expireIfNeeded();

Q_SIGNALS:
  void trustRevoked(deskflow::relaydesk::DeviceId deviceId);

private:
  friend class AutoReconnectRuntime;
  [[nodiscard]] PairingOperationResult reportPreflightFailure(PairingOperationResult result);
  [[nodiscard]] std::optional<std::pair<QHostAddress, quint16>> endpointFor(
      const DeviceSnapshot &peer
  ) const;
  void updateDevice(const PairingSnapshot &snapshot);
  void syncDiscoveredDevice(DeviceSnapshot snapshot);
  void applyTrust(DeviceSnapshot &snapshot) const;

  DeviceDiscoveryRuntime &m_discovery;
  model::DeviceHomeModel &m_deviceModel;
  TrustedDeviceStore m_trustedDevices;
  TrustedDeviceStoreResult m_loadResult;
  std::unique_ptr<PairingService> m_service;
  std::function<std::optional<std::pair<QHostAddress, quint16>>(const DeviceId &)> m_endpointResolver;
};

} // namespace deskflow::relaydesk
