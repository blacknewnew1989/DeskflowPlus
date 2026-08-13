/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingService.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QObject>

#include <functional>
#include <optional>

namespace deskflow::relaydesk::model {
class DeviceHomeModel;
class PairingWizardModel;
}

namespace deskflow::relaydesk {

class DeviceDiscoveryRuntime;

struct PairingTrustRuntimeOptions
{
  PairingOptions pairing;
  PairingManager::Clock clock;
  PairingManager::SasGenerator sasGenerator;
  std::function<std::optional<PairingEndpoint>(const DeviceSnapshot &)> endpointResolver;
};

class PairingTrustRuntime final : public QObject
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
  [[nodiscard]] PairingService &service();
  [[nodiscard]] const TrustedDeviceStore &trustedDevices() const;

  [[nodiscard]] PairingOperationResult startPairing(const DeviceSnapshot &peer);
  [[nodiscard]] PairingOperationResult confirmMatchingSas(const QUuid &sessionId);
  [[nodiscard]] PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits);
  [[nodiscard]] PairingOperationResult cancel(const QUuid &sessionId);
  [[nodiscard]] PairingOperationResult revoke(const DeviceId &deviceId);
  [[nodiscard]] bool expireIfNeeded();

Q_SIGNALS:
  void pairingChanged(PairingSnapshot snapshot);
  void operationFailed(PairingOperationResult result);

private:
  [[nodiscard]] std::optional<PairingEndpoint> endpointFor(const DeviceSnapshot &peer) const;
  void updateDevice(const PairingSnapshot &snapshot);
  void syncDiscoveredDevice(DeviceSnapshot snapshot);
  void applyTrust(DeviceSnapshot &snapshot) const;

  DeviceDiscoveryRuntime &m_discovery;
  model::DeviceHomeModel &m_deviceModel;
  TrustedDeviceStore m_trustedDevices;
  TrustedDeviceStoreResult m_loadResult;
  PairingService m_service;
  std::function<std::optional<PairingEndpoint>(const DeviceSnapshot &)> m_endpointResolver;
};

} // namespace deskflow::relaydesk
