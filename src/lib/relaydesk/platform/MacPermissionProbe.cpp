/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionProbe.h"

#include <QDateTime>

#include <algorithm>
#include <utility>

namespace deskflow::relaydesk {

MacPermissionProbe::MacPermissionProbe(QObject *parent)
    : MacPermissionProbe(createMacPermissionBackend(), {}, parent)
{
}

MacPermissionProbe::MacPermissionProbe(
    std::unique_ptr<IMacPermissionBackend> backend, NowProvider nowProvider, QObject *parent
)
    : QObject(parent),
      m_backend(std::move(backend)),
      m_nowProvider(std::move(nowProvider)),
      m_snapshot({.platform = PermissionPlatform::MacOS}),
      m_refreshTimer(this)
{
  Q_ASSERT(m_backend);
  m_refreshTimer.setInterval(kMacPermissionRefreshDebounceMs);
  m_refreshTimer.setSingleShot(true);
  connect(&m_refreshTimer, &QTimer::timeout, this, &MacPermissionProbe::refreshNow);
  connect(
      m_backend.get(), &IMacPermissionBackend::localNetworkChanged, this, &MacPermissionProbe::updateLocalNetwork
  );
  refreshNow();
}

PermissionSnapshot MacPermissionProbe::current() const
{
  return m_snapshot;
}

void MacPermissionProbe::refresh()
{
  // Application activation can be delivered repeatedly while macOS is
  // switching away from System Settings. Restarting this single-shot timer
  // coalesces those events while keeping the actual probes off the event
  // handler's call stack.
  m_refreshTimer.start();
}

void MacPermissionProbe::refreshNow()
{
  m_refreshInProgress = true;
  // Reset/restart the asynchronous Local Network probe before reading its
  // value. This prevents a grant from an earlier browser generation from
  // surviving a foreground refresh while the new result is still pending.
  m_backend->refreshLocalNetwork();
  m_snapshot = {
      .platform = PermissionPlatform::MacOS,
      .entries = {m_backend->localNetwork(), m_backend->accessibility(), m_backend->inputMonitoring()},
      .checkedAtUtc = nowUtc(),
  };
  m_refreshInProgress = false;
  Q_EMIT snapshotChanged(m_snapshot);
}

PermissionOpenResult MacPermissionProbe::openSystemSettings(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::MacLocalNetwork:
  case PermissionKind::MacAccessibility:
  case PermissionKind::MacInputMonitoring:
    return m_backend->openSystemSettings(kind);
  case PermissionKind::WindowsFirewall:
  case PermissionKind::WindowsListeningPort:
    return {
        .error = PermissionOpenError::Unsupported,
        .diagnostic = QStringLiteral("permission kind is not supported by the macOS adapter"),
    };
  }
  return {
      .error = PermissionOpenError::Unsupported,
      .diagnostic = QStringLiteral("permission kind is not supported by the macOS adapter"),
  };
}

void MacPermissionProbe::updateLocalNetwork(PermissionProbeEntry entry)
{
  if (entry.kind != PermissionKind::MacLocalNetwork || m_refreshInProgress)
    return;
  const auto found = std::find_if(m_snapshot.entries.begin(), m_snapshot.entries.end(), [](const auto &candidate) {
    return candidate.kind == PermissionKind::MacLocalNetwork;
  });
  if (found == m_snapshot.entries.end())
    return;
  *found = std::move(entry);
  m_snapshot.checkedAtUtc = nowUtc();
  Q_EMIT snapshotChanged(m_snapshot);
}

QDateTime MacPermissionProbe::nowUtc() const
{
  return m_nowProvider ? m_nowProvider() : QDateTime::currentDateTimeUtc();
}

} // namespace deskflow::relaydesk
