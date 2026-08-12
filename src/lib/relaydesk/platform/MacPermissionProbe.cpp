/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionProbe.h"

#include <QDateTime>

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
      m_snapshot({.platform = PermissionPlatform::MacOS})
{
  Q_ASSERT(m_backend);
  connect(
      m_backend.get(), &IMacPermissionBackend::localNetworkChanged, this, &MacPermissionProbe::updateLocalNetwork
  );
  refresh();
}

PermissionSnapshot MacPermissionProbe::current() const
{
  return m_snapshot;
}

void MacPermissionProbe::refresh()
{
  m_snapshot = {
      .platform = PermissionPlatform::MacOS,
      .entries = {m_backend->localNetwork(), m_backend->accessibility(), m_backend->inputMonitoring()},
      .checkedAtUtc = nowUtc(),
  };
  Q_EMIT snapshotChanged(m_snapshot);
  m_backend->refreshLocalNetwork();
}

bool MacPermissionProbe::openSystemSettings(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::MacLocalNetwork:
  case PermissionKind::MacAccessibility:
  case PermissionKind::MacInputMonitoring:
    return m_backend->openSystemSettings(kind);
  case PermissionKind::WindowsFirewall:
  case PermissionKind::WindowsListeningPort:
    return false;
  }
  return false;
}

void MacPermissionProbe::updateLocalNetwork(PermissionProbeEntry entry)
{
  if (entry.kind != PermissionKind::MacLocalNetwork)
    return;
  m_snapshot.entries[0] = std::move(entry);
  m_snapshot.checkedAtUtc = nowUtc();
  Q_EMIT snapshotChanged(m_snapshot);
}

QDateTime MacPermissionProbe::nowUtc() const
{
  return m_nowProvider ? m_nowProvider() : QDateTime::currentDateTimeUtc();
}

} // namespace deskflow::relaydesk

