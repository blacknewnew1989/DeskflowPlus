/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/device/DeviceIdentity.h"

#include <QMetaType>
#include <QSettings>

namespace deskflow::relaydesk {

namespace {
constexpr auto kDeviceIdSettingsKey = "relaydesk/device/id";

void setError(QString *errorMessage, const QString &message)
{
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
}
} // namespace

DeviceIdentity::DeviceIdentity(QSettings &settings) : m_settings(settings)
{
}

std::optional<DeviceId> DeviceIdentity::loadOrCreate(QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }

  const auto storedValue = m_settings.value(settingsKey());
  if (storedValue.metaType().id() == QMetaType::QString) {
    const auto storedId = DeviceId::fromString(storedValue.toString());
    if (storedId.has_value()) {
      return storedId;
    }
  }

  const auto generatedId = DeviceId::generate();
  m_settings.setValue(settingsKey(), generatedId.toString());
  m_settings.sync();

  if (m_settings.status() != QSettings::NoError) {
    setError(errorMessage, QStringLiteral("Unable to persist RelayDesk device identity"));
    return std::nullopt;
  }

  return generatedId;
}

QString DeviceIdentity::settingsKey()
{
  return QString::fromLatin1(kDeviceIdSettingsKey);
}

} // namespace deskflow::relaydesk
