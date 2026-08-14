/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/BackgroundLifecycleController.h"

namespace deskflow::relaydesk {

BackgroundLifecycleController::BackgroundLifecycleController(BackgroundLifecycleSettings settings)
    : m_settings(settings)
{
}

bool BackgroundLifecycleController::minimizeToTray() const noexcept
{
  return m_settings.minimizeToTray;
}

bool BackgroundLifecycleController::closeToTray() const noexcept
{
  return m_settings.closeToTray;
}

bool BackgroundLifecycleController::shouldHideAfterMinimize(bool operatingSystemShutdown) const noexcept
{
  return !operatingSystemShutdown && m_settings.minimizeToTray && !m_quitRequested;
}

WindowCloseDisposition BackgroundLifecycleController::closeDisposition(
    bool spontaneous, bool operatingSystemShutdown
) const noexcept
{
  if (m_quitRequested || operatingSystemShutdown || !spontaneous || !m_settings.closeToTray) {
    return WindowCloseDisposition::Quit;
  }
  return WindowCloseDisposition::HideToTray;
}

void BackgroundLifecycleController::setMinimizeToTray(bool enabled) noexcept
{
  m_settings.minimizeToTray = enabled;
}

void BackgroundLifecycleController::setCloseToTray(bool enabled) noexcept
{
  m_settings.closeToTray = enabled;
}

bool BackgroundLifecycleController::takeCloseReminder() noexcept
{
  if (!m_settings.closeReminderPending) {
    return false;
  }
  m_settings.closeReminderPending = false;
  return true;
}

void BackgroundLifecycleController::requestQuit() noexcept
{
  m_quitRequested = true;
}

bool BackgroundLifecycleController::quitRequested() const noexcept
{
  return m_quitRequested;
}

bool BackgroundLifecycleController::beginShutdown(const BackgroundShutdownHooks &hooks)
{
  if (m_shutdownStarted) {
    return false;
  }
  m_quitRequested = true;
  m_shutdownStarted = true;

  if (hooks.stopAcceptingOperations) {
    hooks.stopAcceptingOperations();
  }
  if (hooks.stopInputSharing) {
    hooks.stopInputSharing();
  }
  if (hooks.persistAndStopTransfers) {
    hooks.persistAndStopTransfers();
  }
  if (hooks.stopNetworkServices) {
    hooks.stopNetworkServices();
  }
  if (hooks.removeTrayIcon) {
    hooks.removeTrayIcon();
  }
  return true;
}

bool BackgroundLifecycleController::shutdownStarted() const noexcept
{
  return m_shutdownStarted;
}

} // namespace deskflow::relaydesk
