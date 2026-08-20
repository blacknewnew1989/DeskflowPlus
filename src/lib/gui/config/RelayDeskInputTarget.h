/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

class QSettings;

namespace deskflow::relaydesk {
struct DeviceInfo;
struct DeviceSnapshot;
}

namespace deskflow::gui {

enum class RelayDeskInputTargetResult
{
  Updated,
  AlreadyCurrent,
  NotTrusted,
  InputUnsupported,
  EndpointUnavailable,
  PortMismatchPreserved,
  ManualConfigurationPreserved,
  AnotherManagedDevicePreserved,
};

struct RelayDeskInputTarget
{
  QString host;
  quint16 port = 0;
};

[[nodiscard]] RelayDeskInputTargetResult syncRelayDeskClientTarget(
    QSettings &settings, const relaydesk::DeviceSnapshot &peer, const relaydesk::DeviceInfo &endpoint,
    QString currentHost, quint16 currentPort, bool allowManagedPeerSwitch, RelayDeskInputTarget *target
);

} // namespace deskflow::gui
