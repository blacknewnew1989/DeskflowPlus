/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionBackend.h"

#include "relaydesk/platform/MacLocalNetworkStatus.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Network/Network.h>
#import <dns_sd.h>

#include <QPointer>

namespace deskflow::relaydesk {
namespace {

PermissionProbeEntry unknownLocalNetwork()
{
  return macLocalNetworkEntry(MacLocalNetworkProbeState::Waiting, false);
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

  ~NativeMacPermissionBackend() override
  {
    if (m_browser != nullptr)
      nw_browser_cancel(m_browser);
  }

  [[nodiscard]] PermissionProbeEntry localNetwork() const override
  {
    return m_localNetwork;
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
    if (m_browser != nullptr) {
      nw_browser_cancel(m_browser);
      m_browser = nullptr;
    }

    m_localNetwork = unknownLocalNetwork();
    const auto generation = ++m_generation;
    auto descriptor = nw_browse_descriptor_create_bonjour_service("_relaydesk._udp", nullptr);
    auto parameters = nw_parameters_create();
    m_browser = nw_browser_create(descriptor, parameters);
    if (m_browser == nullptr) {
      publishLocalNetwork(macLocalNetworkEntry(MacLocalNetworkProbeState::Failed, false));
      return;
    }

    QPointer<NativeMacPermissionBackend> backend(this);
    nw_browser_set_state_changed_handler(m_browser, ^(nw_browser_state_t state, nw_error_t error) {
      if (!backend || backend->m_generation != generation)
        return;
      backend->handleBrowserState(state, error);
    });
    nw_browser_set_queue(m_browser, dispatch_get_main_queue());
    nw_browser_start(m_browser);
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

private:
  void handleBrowserState(nw_browser_state_t state, nw_error_t error)
  {
    const auto domain = error == nullptr ? 0 : static_cast<int>(nw_error_get_error_domain(error));
    const auto code = error == nullptr ? 0 : nw_error_get_error_code(error);
    switch (state) {
    case nw_browser_state_ready:
      publishLocalNetwork(macLocalNetworkEntry(MacLocalNetworkProbeState::Ready, false, domain, code));
      break;
    case nw_browser_state_waiting:
      publishLocalNetwork(macLocalNetworkEntry(
          MacLocalNetworkProbeState::Waiting,
          error != nullptr && nw_error_get_error_domain(error) == nw_error_domain_dns &&
              code == kDNSServiceErr_PolicyDenied,
          domain, code
      ));
      break;
    case nw_browser_state_failed:
      publishLocalNetwork(macLocalNetworkEntry(MacLocalNetworkProbeState::Failed, false, domain, code));
      break;
    case nw_browser_state_invalid:
    case nw_browser_state_cancelled:
      break;
    }
  }

  void publishLocalNetwork(PermissionProbeEntry entry)
  {
    if (entry == m_localNetwork)
      return;
    m_localNetwork = std::move(entry);
    Q_EMIT localNetworkChanged(m_localNetwork);
  }

  nw_browser_t m_browser = nullptr;
  quint64 m_generation = 0;
  PermissionProbeEntry m_localNetwork = unknownLocalNetwork();
};

} // namespace

std::unique_ptr<IMacPermissionBackend> createMacPermissionBackend()
{
  return std::make_unique<NativeMacPermissionBackend>();
}

} // namespace deskflow::relaydesk
