/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>
#include <optional>

namespace deskflow::relaydesk {

inline constexpr auto kDefaultDiscoveryTtl = std::chrono::seconds(15);
inline constexpr qsizetype kMaximumDiscoveryAddressesPerDevice = 8;

class DiscoveryRegistry final : public QObject
{
  Q_OBJECT

public:
  using Clock = std::function<QDateTime()>;

  explicit DiscoveryRegistry(
      DeviceId localDeviceId, std::chrono::milliseconds ttl = kDefaultDiscoveryTtl, Clock clock = {},
      QObject *parent = nullptr
  );

  [[nodiscard]] QList<DeviceSnapshot> snapshots() const;
  [[nodiscard]] std::optional<DeviceSnapshot> snapshot(const DeviceId &deviceId) const;
  [[nodiscard]] std::optional<DeviceInfo> deviceInfo(const DeviceId &deviceId) const;
  [[nodiscard]] std::chrono::milliseconds ttl() const;

public Q_SLOTS:
  bool observeAdvertisement(const DeviceInfo &device, const QHostAddress &senderAddress);
  void expireStaleDevices();

Q_SIGNALS:
  void deviceAdded(DeviceSnapshot snapshot);
  void deviceChanged(DeviceSnapshot snapshot);

private:
  struct Record
  {
    DeviceInfo info;
    DeviceSnapshot snapshot;
    QDateTime observedAtUtc;
  };

  [[nodiscard]] QDateTime currentTimeUtc() const;
  [[nodiscard]] static QList<QHostAddress>
  updatedAddresses(const QList<QHostAddress> &existing, const QHostAddress &latest);

  DeviceId m_localDeviceId;
  std::chrono::milliseconds m_ttl;
  Clock m_clock;
  QHash<DeviceId, Record> m_records;
  QTimer m_expiryTimer;
};

} // namespace deskflow::relaydesk
