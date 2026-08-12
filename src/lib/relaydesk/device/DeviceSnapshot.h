/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"

#include <QByteArray>
#include <QDateTime>
#include <QHostAddress>
#include <QList>
#include <QMetaType>
#include <QString>

namespace deskflow::relaydesk {

enum class DevicePresence
{
  Offline,
  Discovered,
  Pairing,
  Online,
  TrustViolation,
};

struct DeviceSnapshot
{
  DeviceId id;
  QString displayName;
  QString alias;
  QString platform;
  QString architecture;
  DevicePresence presence;
  bool trusted = false;
  bool autoAcceptFiles = false;
  int latencyMs = -1;
  QList<QHostAddress> addresses;
  DeviceCapabilities capabilities;
  QByteArray pinnedFingerprint;
  QDateTime lastSeenUtc;

  bool operator==(const DeviceSnapshot &) const = default;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::DevicePresence)
Q_DECLARE_METATYPE(deskflow::relaydesk::DeviceSnapshot)
