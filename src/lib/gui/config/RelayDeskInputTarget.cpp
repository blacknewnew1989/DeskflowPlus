/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RelayDeskInputTarget.h"

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"

#include <QSettings>

namespace deskflow::gui {
namespace {
constexpr auto kGroup = "relaydesk/inputTarget";
constexpr auto kDeviceId = "deviceId";
constexpr auto kHost = "host";
constexpr auto kPort = "port";
}

RelayDeskInputTargetResult syncRelayDeskClientTarget(
    QSettings &settings, const relaydesk::DeviceSnapshot &peer, const relaydesk::DeviceInfo &endpoint,
    QString currentHost, quint16 currentPort, bool allowManagedPeerSwitch, RelayDeskInputTarget *target
)
{
  if (!peer.trusted) return RelayDeskInputTargetResult::NotTrusted;
  if (!peer.capabilities.input) return RelayDeskInputTargetResult::InputUnsupported;
  if (endpoint.inputPort == 0 || peer.addresses.isEmpty()) return RelayDeskInputTargetResult::EndpointUnavailable;
  if (endpoint.inputPort != currentPort) return RelayDeskInputTargetResult::PortMismatchPreserved;

  const auto nextHost = peer.addresses.constFirst().toString();
  if (nextHost.isEmpty()) return RelayDeskInputTargetResult::EndpointUnavailable;

  settings.beginGroup(QString::fromLatin1(kGroup));
  const auto managedDevice = settings.value(QString::fromLatin1(kDeviceId)).toString();
  const auto managedHost = settings.value(QString::fromLatin1(kHost)).toString();
  const auto managedPort = static_cast<quint16>(settings.value(QString::fromLatin1(kPort)).toUInt());
  settings.endGroup();

  currentHost = currentHost.trimmed();
  const auto peerId = peer.id.toString();
  if (!managedDevice.isEmpty() && managedDevice != peerId && !allowManagedPeerSwitch) {
    return RelayDeskInputTargetResult::AnotherManagedDevicePreserved;
  }
  if (!currentHost.isEmpty() && (currentHost != managedHost || currentPort != managedPort)) {
    return RelayDeskInputTargetResult::ManualConfigurationPreserved;
  }

  const RelayDeskInputTarget next{.host = nextHost, .port = endpoint.inputPort};
  if (target != nullptr) *target = next;
  if (managedDevice == peerId && currentHost == next.host && currentPort == next.port) {
    return RelayDeskInputTargetResult::AlreadyCurrent;
  }

  settings.beginGroup(QString::fromLatin1(kGroup));
  settings.setValue(QString::fromLatin1(kDeviceId), peerId);
  settings.setValue(QString::fromLatin1(kHost), next.host);
  settings.setValue(QString::fromLatin1(kPort), next.port);
  settings.endGroup();
  return RelayDeskInputTargetResult::Updated;
}

} // namespace deskflow::gui
