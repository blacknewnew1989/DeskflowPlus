/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionBackend.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace deskflow::relaydesk {
namespace {

PermissionProbeEntry unknownLocalNetwork()
{
  return {
      .kind = PermissionKind::MacLocalNetwork,
      .state = PermissionState::Unknown,
      .errorCode = static_cast<int>(PermissionErrorCode::ProbeUnavailable),
      .diagnostic = QStringLiteral("Local Network authorization has not been checked"),
  };
}

PermissionProbeEntry permissionEntry(
    PermissionKind kind, bool granted, PermissionErrorCode deniedCode, const QString &deniedDiagnostic
)
{
  return {
      .kind = kind,
      .state = granted ? PermissionState::Granted : PermissionState::Denied,
      .errorCode = static_cast<int>(granted ? PermissionErrorCode::None : deniedCode),
      .canOpenSettings = !granted,
      .diagnostic = granted ? QString() : deniedDiagnostic,
  };
}

const char *settingsUrl(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::MacLocalNetwork:
    return "x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork";
  case PermissionKind::MacAccessibility:
    return "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";
  case PermissionKind::MacInputMonitoring:
    return "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent";
  case PermissionKind::WindowsFirewall:
  case PermissionKind::WindowsListeningPort:
    return nullptr;
  }
  return nullptr;
}

class NativeMacPermissionBackend final : public IMacPermissionBackend
{
public:
  using IMacPermissionBackend::IMacPermissionBackend;

  [[nodiscard]] PermissionProbeEntry localNetwork() const override
  {
    return unknownLocalNetwork();
  }

  [[nodiscard]] PermissionProbeEntry accessibility() const override
  {
    return permissionEntry(
        PermissionKind::MacAccessibility, AXIsProcessTrusted(), PermissionErrorCode::MacAccessibilityDenied,
        QStringLiteral("AXIsProcessTrusted returned false")
    );
  }

  [[nodiscard]] PermissionProbeEntry inputMonitoring() const override
  {
    if (@available(macOS 10.15, *)) {
      return permissionEntry(
          PermissionKind::MacInputMonitoring, CGPreflightListenEventAccess(),
          PermissionErrorCode::MacInputMonitoringDenied,
          QStringLiteral("CGPreflightListenEventAccess returned false")
      );
    }
    return {
        .kind = PermissionKind::MacInputMonitoring,
        .state = PermissionState::NotRequired,
    };
  }

  void refreshLocalNetwork() override
  {
  }

  [[nodiscard]] bool openSystemSettings(PermissionKind kind) override
  {
    const auto *urlText = settingsUrl(kind);
    if (urlText == nullptr)
      return false;

    // Prompting is limited to this explicit user action. Status probes above
    // never display system UI.
    if (kind == PermissionKind::MacAccessibility) {
      const void *keys[] = {kAXTrustedCheckOptionPrompt};
      const void *values[] = {kCFBooleanTrue};
      auto options = CFDictionaryCreate(nullptr, keys, values, 1, nullptr, nullptr);
      AXIsProcessTrustedWithOptions(options);
      CFRelease(options);
    } else if (kind == PermissionKind::MacInputMonitoring) {
      if (@available(macOS 10.15, *))
        CGRequestListenEventAccess();
    }

    auto url = [NSURL URLWithString:[NSString stringWithUTF8String:urlText]];
    return url != nil && [[NSWorkspace sharedWorkspace] openURL:url];
  }
};

} // namespace

std::unique_ptr<IMacPermissionBackend> createMacPermissionBackend()
{
  return std::make_unique<NativeMacPermissionBackend>();
}

} // namespace deskflow::relaydesk

