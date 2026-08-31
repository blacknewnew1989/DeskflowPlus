/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace deskflow::relaydesk {

inline constexpr int kTrustedDeviceStoreSchemaVersion = 1;
inline constexpr qsizetype kSha256FingerprintBytes = 32;

struct TrustedDevice
{
  DeviceId deviceId;
  QString alias;
  QString platform;
  QByteArray fingerprintSha256;
  QStringList lastAddresses;
  bool autoAcceptFiles = false;
  bool revoked = false;

  bool operator==(const TrustedDevice &) const = default;
};

enum class TrustStatus
{
  Unknown,
  Trusted,
  Revoked,
  FingerprintMismatch,
};

enum class TrustedDeviceLoadSource
{
  Empty,
  Primary,
  Backup,
};

struct TrustedDeviceStoreResult
{
  // ok means the authoritative primary store was committed; diagnostic may describe backup degradation.
  bool ok = false;
  TrustedDeviceLoadSource source = TrustedDeviceLoadSource::Empty;
  QString diagnostic;
};

class TrustedDeviceStore final
{
public:
  explicit TrustedDeviceStore(QString path);

  [[nodiscard]] TrustedDeviceStoreResult load();
  // Saves primary first; a backup-only failure preserves a successful primary result.
  [[nodiscard]] TrustedDeviceStoreResult save() const;

  [[nodiscard]] QList<TrustedDevice> devices() const;
  [[nodiscard]] std::optional<TrustedDevice> find(const DeviceId &deviceId) const;
  [[nodiscard]] TrustStatus trustStatus(const DeviceId &deviceId, QByteArrayView fingerprintSha256) const;

  [[nodiscard]] bool upsert(TrustedDevice device, QString *diagnostic = nullptr);
  [[nodiscard]] bool revoke(const DeviceId &deviceId);
  [[nodiscard]] bool remove(const DeviceId &deviceId);

  [[nodiscard]] const QString &path() const noexcept;
  [[nodiscard]] QString backupPath() const;

private:
  QString m_path;
  QHash<DeviceId, TrustedDevice> m_devices;
};

} // namespace deskflow::relaydesk
