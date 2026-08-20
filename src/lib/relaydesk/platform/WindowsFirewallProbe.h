/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/IPlatformPermissions.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QtTypes>

#include <functional>

namespace deskflow::relaydesk {

struct WindowsListeningService
{
  QString executablePath;
  quint16 tcpPort = 0;
  quint32 processId = 0;
};

struct WindowsFirewallProbeRequest
{
  QString executablePath;
  QList<quint16> expectedTcpPorts;
  quint32 processId = 0;
  QList<WindowsListeningService> listeners;
};

enum class WindowsFirewallRuleStatus
{
  Allowed,
  Blocked,
  MissingAllowRule,
  NotRequired,
  Unavailable,
};

enum class WindowsListeningPortStatus
{
  Listening,
  NotListening,
  NotRequired,
  Unavailable,
};

struct WindowsFirewallInspection
{
  WindowsFirewallRuleStatus firewall = WindowsFirewallRuleStatus::Unavailable;
  WindowsListeningPortStatus listeningPort = WindowsListeningPortStatus::Unavailable;
  QString firewallDiagnostic;
  QString listeningPortDiagnostic;
};

class WindowsFirewallProbe final : public QObject, public IPlatformPermissions
{
  Q_OBJECT

public:
  using Inspector = std::function<WindowsFirewallInspection(WindowsFirewallProbeRequest)>;
  using Clock = std::function<QDateTime()>;
  using SettingsOpener = std::function<PermissionOpenResult()>;

  explicit WindowsFirewallProbe(
      Inspector inspector = {}, Clock clock = {}, SettingsOpener settingsOpener = {}, QObject *parent = nullptr
  );

  // COM and TCP table inspection run on the global worker pool. This method
  // only validates/schedules work and never blocks the calling GUI thread.
  void refresh(WindowsFirewallProbeRequest request);
  [[nodiscard]] PermissionSnapshot current() const override;
  [[nodiscard]] bool isRefreshing() const noexcept;
  [[nodiscard]] PermissionOpenResult openSystemSettings(PermissionKind kind) override;

  [[nodiscard]] static WindowsFirewallInspection inspectCurrentSystem(WindowsFirewallProbeRequest request);
  [[nodiscard]] static WindowsFirewallProbeRequest requestForListeningServices(
      QString executablePath, quint16 inputPort, bool inputIsListening, quint16 filePort, quint32 processId
  );
  [[nodiscard]] static WindowsFirewallProbeRequest requestForListeningServices(
      QString coreExecutablePath, quint16 inputPort, bool inputIsListening, quint32 coreProcessId,
      QString guiExecutablePath, quint16 filePort, quint32 guiProcessId
  );

Q_SIGNALS:
  void snapshotChanged(PermissionSnapshot snapshot);
  void settingsOpenFailed(PermissionKind kind, PermissionOpenResult result);

private:
  [[nodiscard]] PermissionSnapshot snapshotFromInspection(const WindowsFirewallInspection &inspection) const;

  Inspector m_inspector;
  Clock m_clock;
  SettingsOpener m_settingsOpener;
  PermissionSnapshot m_snapshot;
  quint64 m_generation = 0;
  bool m_refreshing = false;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::WindowsFirewallRuleStatus)
Q_DECLARE_METATYPE(deskflow::relaydesk::WindowsListeningPortStatus)
Q_DECLARE_METATYPE(deskflow::relaydesk::WindowsFirewallInspection)
