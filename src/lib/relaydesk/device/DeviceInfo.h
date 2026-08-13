/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QString>
#include <QtTypes>

#include <optional>

namespace deskflow::relaydesk {

inline constexpr auto kDeviceInfoProtocol = "relaydesk-discovery";
inline constexpr qint64 kDeviceInfoSchemaVersion = 1;

struct DeviceCapabilities
{
  bool input = false;
  bool clipboardText = false;
  bool clipboardImage = false;
  bool fileV1 = false;
  bool folderV1 = false;
  bool resumeV1 = false;

  bool operator==(const DeviceCapabilities &) const = default;
};

struct DeviceInfo
{
  DeviceId deviceId;
  QString displayName;
  QString platform;
  QString architecture;
  QString appVersion;
  quint16 inputPort = 0;
  quint16 filePort = 0;
  DeviceCapabilities capabilities;
  QByteArray certificateFingerprintSha256;

  bool operator==(const DeviceInfo &) const = default;
};

class DeviceInfoCodec final
{
public:
  [[nodiscard]] static QByteArray serialize(const DeviceInfo &device, QString *errorMessage = nullptr);
  [[nodiscard]] static std::optional<DeviceInfo> deserialize(QByteArrayView bytes, QString *errorMessage = nullptr);
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::DeviceCapabilities)
Q_DECLARE_METATYPE(deskflow::relaydesk::DeviceInfo)
