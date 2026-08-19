/* SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "relaydesk/discovery/DiscoverySettings.h"
#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/reconnect/AutoReconnectCoordinator.h"

#include <QHash>
#include <QObject>

class AutoReconnectRuntimeTests;

namespace deskflow::relaydesk {
class DeviceDiscoveryRuntime;
class FileTransferRuntime;
class PairingTrustRuntime;

// Product-only adapter: discovered trusted peers are reconnected through the
// production RDFT TLS runtime. Success is reported only after peerReady, which
// is emitted after certificate/HELLO pinning and capability negotiation.
class AutoReconnectRuntime final : public QObject
{
  Q_OBJECT
public:
  AutoReconnectRuntime(
      PairingTrustRuntime &pairing, DeviceDiscoveryRuntime &discovery, FileTransferRuntime &files,
      DiscoverySettings settings, QObject *parent = nullptr
  );
  void stop();

private:
  friend class ::AutoReconnectRuntimeTests;
  void observe(DeviceSnapshot snapshot);
  void connectCandidate(
      const DeviceId &deviceId, const AddressCandidate &candidate,
      AutoReconnectCoordinator::ConnectCallback callback
  );
  void stopPeer(const DeviceId &deviceId);
  void completeReady(const DeviceId &deviceId);

  PairingTrustRuntime &m_pairing;
  DeviceDiscoveryRuntime &m_discovery;
  FileTransferRuntime &m_files;
  DiscoverySettings m_settings;
  QHash<DeviceId, AddressCandidateProvider *> m_providers;
  QHash<DeviceId, AutoReconnectCoordinator *> m_coordinators;
  QHash<DeviceId, AutoReconnectCoordinator::ConnectCallback> m_pending;
};
} // namespace deskflow::relaydesk
