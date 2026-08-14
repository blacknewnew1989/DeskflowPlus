/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <functional>

namespace deskflow::relaydesk {

struct BackgroundLifecycleSettings
{
  bool minimizeToTray = true;
  bool closeToTray = true;
  bool closeReminderPending = true;
};

enum class WindowCloseDisposition
{
  HideToTray,
  Quit,
};

struct BackgroundShutdownHooks
{
  std::function<void()> stopAcceptingOperations;
  std::function<void()> stopInputSharing;
  std::function<void()> persistAndStopTransfers;
  std::function<void()> stopNetworkServices;
  std::function<void()> removeTrayIcon;
};

// Shared, platform-independent policy for the tray/menu-bar lifecycle. The
// platform window adapter supplies whether an event was user initiated or is
// part of an operating-system shutdown; the policy never calls native APIs.
class BackgroundLifecycleController final
{
public:
  explicit BackgroundLifecycleController(BackgroundLifecycleSettings settings = {});

  [[nodiscard]] bool minimizeToTray() const noexcept;
  [[nodiscard]] bool closeToTray() const noexcept;
  [[nodiscard]] bool shouldHideAfterMinimize(bool operatingSystemShutdown) const noexcept;
  [[nodiscard]] WindowCloseDisposition closeDisposition(
      bool spontaneous, bool operatingSystemShutdown
  ) const noexcept;

  void setMinimizeToTray(bool enabled) noexcept;
  void setCloseToTray(bool enabled) noexcept;

  // Returns true exactly once while the reminder is pending. Call this only
  // after closeDisposition() selected HideToTray.
  [[nodiscard]] bool takeCloseReminder() noexcept;

  // Once true, every close path is a real quit. This prevents a programmatic
  // or operating-system quit from being converted back into a hidden window.
  void requestQuit() noexcept;
  [[nodiscard]] bool quitRequested() const noexcept;
  [[nodiscard]] bool beginShutdown(const BackgroundShutdownHooks &hooks);
  [[nodiscard]] bool shutdownStarted() const noexcept;

private:
  BackgroundLifecycleSettings m_settings;
  bool m_quitRequested = false;
  bool m_shutdownStarted = false;
};

} // namespace deskflow::relaydesk
