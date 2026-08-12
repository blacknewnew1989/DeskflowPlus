/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/PermissionSnapshot.h"

namespace deskflow::relaydesk {

enum class MacLocalNetworkProbeState
{
  Ready,
  Waiting,
  Failed,
};

[[nodiscard]] PermissionProbeEntry macLocalNetworkEntry(
    MacLocalNetworkProbeState state, bool policyDenied, int nativeDomain = 0, int nativeCode = 0
);

} // namespace deskflow::relaydesk

