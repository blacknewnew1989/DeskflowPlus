/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/PermissionSnapshot.h"

#include <QObject>

#include <memory>

namespace deskflow::relaydesk {

class IMacPermissionBackend : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;
  ~IMacPermissionBackend() override = default;

  [[nodiscard]] virtual PermissionProbeEntry localNetwork() const = 0;
  [[nodiscard]] virtual PermissionProbeEntry accessibility() const = 0;
  [[nodiscard]] virtual PermissionProbeEntry inputMonitoring() const = 0;
  virtual void refreshLocalNetwork() = 0;
  [[nodiscard]] virtual bool openSystemSettings(PermissionKind kind) = 0;

Q_SIGNALS:
  void localNetworkChanged(deskflow::relaydesk::PermissionProbeEntry entry);
};

[[nodiscard]] std::unique_ptr<IMacPermissionBackend> createMacPermissionBackend();

} // namespace deskflow::relaydesk

