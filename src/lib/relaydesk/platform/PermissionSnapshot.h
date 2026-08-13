/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace deskflow::relaydesk {

enum class PermissionPlatform
{
  Other,
  Windows,
  MacOS,
};

enum class PermissionKind
{
  WindowsFirewall,
  WindowsListeningPort,
  MacLocalNetwork,
  MacAccessibility,
  MacInputMonitoring,
};

enum class PermissionState
{
  Unknown,
  NotRequired,
  Granted,
  Denied,
  NeedsAction,
};

// Stable UI-facing codes. Platform diagnostics remain private to the probe and
// must never be used as user-visible text.
enum class PermissionErrorCode : int
{
  None = 0,
  ProbeUnavailable = 4000,
  WindowsFirewallBlocked = 4101,
  WindowsPortUnavailable = 4102,
  MacLocalNetworkDenied = 4201,
  MacAccessibilityDenied = 4202,
  MacInputMonitoringDenied = 4203,
};

struct PermissionProbeEntry
{
  PermissionKind kind = PermissionKind::WindowsFirewall;
  PermissionState state = PermissionState::Unknown;
  PermissionErrorCode errorCode = PermissionErrorCode::None;
  bool canOpenSettings = false;
  QString diagnostic;

  bool operator==(const PermissionProbeEntry &) const = default;
};

struct PermissionSnapshot
{
  PermissionPlatform platform = PermissionPlatform::Other;
  QList<PermissionProbeEntry> entries;
  QDateTime checkedAtUtc;

  bool operator==(const PermissionSnapshot &) const = default;
};

[[nodiscard]] constexpr PermissionPlatform buildPermissionPlatform() noexcept
{
#if defined(Q_OS_WIN)
  return PermissionPlatform::Windows;
#elif defined(Q_OS_MACOS)
  return PermissionPlatform::MacOS;
#else
  return PermissionPlatform::Other;
#endif
}

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionPlatform)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionKind)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionState)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionErrorCode)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionProbeEntry)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionSnapshot)
