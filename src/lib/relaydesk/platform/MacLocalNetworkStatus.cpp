/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacLocalNetworkStatus.h"

namespace deskflow::relaydesk {

PermissionProbeEntry macLocalNetworkEntry(
    MacLocalNetworkProbeState state, bool policyDenied, int nativeDomain, int nativeCode
)
{
  if (state == MacLocalNetworkProbeState::Ready) {
    return {
        .kind = PermissionKind::MacLocalNetwork,
        .state = PermissionState::Granted,
    };
  }

  const auto diagnostic = QStringLiteral("NWBrowser state=%1 domain=%2 code=%3")
                              .arg(static_cast<int>(state))
                              .arg(nativeDomain)
                              .arg(nativeCode);
  if (state == MacLocalNetworkProbeState::Waiting && policyDenied) {
    return {
        .kind = PermissionKind::MacLocalNetwork,
        .state = PermissionState::Denied,
        .errorCode = static_cast<int>(PermissionErrorCode::MacLocalNetworkDenied),
        .canOpenSettings = true,
        .diagnostic = diagnostic,
    };
  }

  return {
      .kind = PermissionKind::MacLocalNetwork,
      .state = PermissionState::Unknown,
      .errorCode = static_cast<int>(PermissionErrorCode::ProbeUnavailable),
      .diagnostic = diagnostic,
  };
}

} // namespace deskflow::relaydesk

