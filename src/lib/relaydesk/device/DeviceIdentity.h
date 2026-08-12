/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"

#include <QString>

#include <optional>

class QSettings;

namespace deskflow::relaydesk {

class DeviceIdentity final
{
public:
  explicit DeviceIdentity(QSettings &settings);

  [[nodiscard]] std::optional<DeviceId> loadOrCreate(QString *errorMessage = nullptr);

  [[nodiscard]] static QString settingsKey();

private:
  QSettings &m_settings;
};

} // namespace deskflow::relaydesk
