/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/MacPermissionBackend.h"

#include <QObject>

#include <functional>
#include <memory>

namespace deskflow::relaydesk {

class MacPermissionProbe final : public QObject
{
  Q_OBJECT

public:
  using NowProvider = std::function<QDateTime()>;

  explicit MacPermissionProbe(QObject *parent = nullptr);
  explicit MacPermissionProbe(
      std::unique_ptr<IMacPermissionBackend> backend, NowProvider nowProvider = {}, QObject *parent = nullptr
  );

  [[nodiscard]] PermissionSnapshot current() const;

public Q_SLOTS:
  void refresh();
  bool openSystemSettings(deskflow::relaydesk::PermissionKind kind);

Q_SIGNALS:
  void snapshotChanged(deskflow::relaydesk::PermissionSnapshot snapshot);

private:
  void updateLocalNetwork(PermissionProbeEntry entry);
  [[nodiscard]] QDateTime nowUtc() const;

  std::unique_ptr<IMacPermissionBackend> m_backend;
  NowProvider m_nowProvider;
  PermissionSnapshot m_snapshot;
};

} // namespace deskflow::relaydesk

