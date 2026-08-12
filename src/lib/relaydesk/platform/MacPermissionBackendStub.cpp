/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionBackend.h"

namespace deskflow::relaydesk {
namespace {

class UnsupportedMacPermissionBackend final : public IMacPermissionBackend
{
public:
  using IMacPermissionBackend::IMacPermissionBackend;

  [[nodiscard]] PermissionProbeEntry localNetwork() const override
  {
    return unavailable(PermissionKind::MacLocalNetwork);
  }

  [[nodiscard]] PermissionProbeEntry accessibility() const override
  {
    return unavailable(PermissionKind::MacAccessibility);
  }

  [[nodiscard]] PermissionProbeEntry inputMonitoring() const override
  {
    return unavailable(PermissionKind::MacInputMonitoring);
  }

  void refreshLocalNetwork() override
  {
  }

  [[nodiscard]] bool openSystemSettings(PermissionKind) override
  {
    return false;
  }

private:
  [[nodiscard]] static PermissionProbeEntry unavailable(PermissionKind kind)
  {
    return {
        .kind = kind,
        .state = PermissionState::Unknown,
        .errorCode = static_cast<int>(PermissionErrorCode::ProbeUnavailable),
        .diagnostic = QStringLiteral("macOS permission APIs are unavailable on this platform"),
    };
  }
};

} // namespace

std::unique_ptr<IMacPermissionBackend> createMacPermissionBackend()
{
  return std::make_unique<UnsupportedMacPermissionBackend>();
}

} // namespace deskflow::relaydesk

