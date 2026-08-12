/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryRegistry.h"

#include <QAbstractSocket>

#include <algorithm>
#include <utility>

namespace deskflow::relaydesk {

namespace {
constexpr int kMinimumExpiryIntervalMs = 250;
constexpr int kMaximumExpiryIntervalMs = 1000;
}

DiscoveryRegistry::DiscoveryRegistry(
    DeviceId localDeviceId, std::chrono::milliseconds ttl, Clock clock, QObject *parent
)
    : QObject(parent), m_localDeviceId(std::move(localDeviceId)), m_ttl(ttl), m_clock(std::move(clock)),
      m_expiryTimer(this)
{
  if (!m_clock) {
    m_clock = []() { return QDateTime::currentDateTimeUtc(); };
  }

  const auto expiryInterval = std::clamp<qint64>(
      m_ttl.count() / 3, kMinimumExpiryIntervalMs, kMaximumExpiryIntervalMs
  );
  m_expiryTimer.setInterval(static_cast<int>(expiryInterval));
  m_expiryTimer.setTimerType(Qt::CoarseTimer);
  connect(&m_expiryTimer, &QTimer::timeout, this, &DiscoveryRegistry::expireStaleDevices);
  if (m_ttl.count() > 0) {
    m_expiryTimer.start();
  }
}

QList<DeviceSnapshot> DiscoveryRegistry::snapshots() const
{
  QList<DeviceSnapshot> result;
  result.reserve(m_records.size());
  for (const auto &record : m_records) {
    result.append(record.snapshot);
  }
  std::sort(result.begin(), result.end(), [](const DeviceSnapshot &left, const DeviceSnapshot &right) {
    const auto nameOrder = QString::compare(left.displayName, right.displayName, Qt::CaseInsensitive);
    return nameOrder != 0 ? nameOrder < 0 : left.id.toString() < right.id.toString();
  });
  return result;
}

std::optional<DeviceSnapshot> DiscoveryRegistry::snapshot(const DeviceId &deviceId) const
{
  const auto iterator = m_records.constFind(deviceId);
  return iterator == m_records.constEnd() ? std::nullopt : std::optional<DeviceSnapshot>(iterator->snapshot);
}

std::optional<DeviceInfo> DiscoveryRegistry::deviceInfo(const DeviceId &deviceId) const
{
  const auto iterator = m_records.constFind(deviceId);
  return iterator == m_records.constEnd() ? std::nullopt : std::optional<DeviceInfo>(iterator->info);
}

std::chrono::milliseconds DiscoveryRegistry::ttl() const
{
  return m_ttl;
}

bool DiscoveryRegistry::observeAdvertisement(const DeviceInfo &device, const QHostAddress &senderAddress)
{
  if (device.deviceId == m_localDeviceId || senderAddress.isNull() ||
      senderAddress.protocol() != QAbstractSocket::IPv4Protocol) {
    return false;
  }

  const auto observedAt = currentTimeUtc();
  auto iterator = m_records.find(device.deviceId);
  if (iterator == m_records.end()) {
    const DeviceSnapshot snapshot{
        .id = device.deviceId,
        .displayName = device.displayName,
        .platform = device.platform,
        .architecture = device.architecture,
        .presence = DevicePresence::Discovered,
        .addresses = {senderAddress},
        .capabilities = device.capabilities,
        .lastSeenUtc = observedAt,
    };
    iterator = m_records.insert(
        device.deviceId,
        Record{
            .info = device,
            .snapshot = snapshot,
            .observedAtUtc = observedAt,
        }
    );
    Q_EMIT deviceAdded(iterator->snapshot);
    return true;
  }

  iterator->info = device;
  iterator->observedAtUtc = observedAt;
  iterator->snapshot.displayName = device.displayName;
  iterator->snapshot.platform = device.platform;
  iterator->snapshot.architecture = device.architecture;
  iterator->snapshot.presence = DevicePresence::Discovered;
  iterator->snapshot.addresses = updatedAddresses(iterator->snapshot.addresses, senderAddress);
  iterator->snapshot.capabilities = device.capabilities;
  iterator->snapshot.lastSeenUtc = observedAt;
  Q_EMIT deviceChanged(iterator->snapshot);
  return true;
}

void DiscoveryRegistry::expireStaleDevices()
{
  if (m_ttl.count() <= 0) {
    return;
  }

  const auto now = currentTimeUtc();
  for (auto iterator = m_records.begin(); iterator != m_records.end(); ++iterator) {
    if (iterator->snapshot.presence == DevicePresence::Offline) {
      continue;
    }
    const auto elapsed = iterator->observedAtUtc.msecsTo(now);
    if (elapsed >= m_ttl.count()) {
      iterator->snapshot.presence = DevicePresence::Offline;
      Q_EMIT deviceChanged(iterator->snapshot);
    }
  }
}

QDateTime DiscoveryRegistry::currentTimeUtc() const
{
  const auto current = m_clock();
  return current.timeSpec() == Qt::UTC ? current : current.toUTC();
}

QList<QHostAddress>
DiscoveryRegistry::updatedAddresses(const QList<QHostAddress> &existing, const QHostAddress &latest)
{
  QList<QHostAddress> result;
  result.reserve(std::min(existing.size() + 1, kMaximumDiscoveryAddressesPerDevice));
  result.append(latest);
  for (const auto &address : existing) {
    if (address != latest && result.size() < kMaximumDiscoveryAddressesPerDevice) {
      result.append(address);
    }
  }
  return result;
}

} // namespace deskflow::relaydesk
