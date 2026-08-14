/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/IPlatformPermissions.h"

#include <functional>

namespace deskflow::relaydesk {

using MacSettingsUrlOpener = std::function<bool(const QString &url)>;

/** Opens the permission-specific pane, then Privacy & Security as fallback. */
[[nodiscard]] PermissionOpenResult openMacPermissionSettings(PermissionKind kind, const MacSettingsUrlOpener &opener);

} // namespace deskflow::relaydesk
