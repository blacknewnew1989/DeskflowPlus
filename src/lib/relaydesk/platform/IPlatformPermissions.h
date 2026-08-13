/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/PermissionSnapshot.h"

#include <QMetaType>
#include <QString>

namespace deskflow::relaydesk {

enum class PermissionOpenError : int
{
  None = 0,
  Unsupported = 1,
  NotActionable = 2,
  OpenFailed = 3,
};

struct PermissionOpenResult
{
  PermissionOpenError error = PermissionOpenError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PermissionOpenError::None;
  }

  bool operator==(const PermissionOpenResult &) const = default;
};

/** Common read/open boundary. Platform-specific refresh inputs stay in adapters. */
class IPlatformPermissions
{
public:
  virtual ~IPlatformPermissions() = default;

  [[nodiscard]] virtual PermissionSnapshot current() const = 0;
  [[nodiscard]] virtual PermissionOpenResult openSystemSettings(PermissionKind kind) = 0;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionOpenError)
Q_DECLARE_METATYPE(deskflow::relaydesk::PermissionOpenResult)
