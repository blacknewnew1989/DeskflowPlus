/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionSettings.h"

namespace deskflow::relaydesk {
namespace {

QString specificSettingsUrl(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::MacLocalNetwork:
    return QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork");
  case PermissionKind::MacAccessibility:
    return QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
  case PermissionKind::MacInputMonitoring:
    return QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent");
  case PermissionKind::WindowsFirewall:
  case PermissionKind::WindowsListeningPort:
    return {};
  }
  return {};
}

} // namespace

PermissionOpenResult openMacPermissionSettings(PermissionKind kind, const MacSettingsUrlOpener &opener)
{
  const auto specificUrl = specificSettingsUrl(kind);
  if (specificUrl.isEmpty()) {
    return {
        .error = PermissionOpenError::Unsupported,
        .diagnostic = QStringLiteral("permission kind is not supported by the macOS backend"),
    };
  }

  if (opener && opener(specificUrl))
    return {};

  const auto fallbackUrl = QStringLiteral("x-apple.systempreferences:com.apple.preference.security");
  if (opener && opener(fallbackUrl))
    return {};

  return {
      .error = PermissionOpenError::OpenFailed,
      .diagnostic = QStringLiteral("macOS permission settings and Privacy & Security fallback could not be opened"),
  };
}

} // namespace deskflow::relaydesk
