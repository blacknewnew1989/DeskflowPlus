/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtTypes>

#include <optional>

class QSettings;

namespace deskflow::relaydesk {

inline constexpr int kDiscoverySettingsSchemaVersion = 1;
inline constexpr quint16 kDefaultManualInputPort = 24800;
inline constexpr quint16 kDefaultManualFilePort = 24801;

struct ManualAddress
{
  QString host;
  quint16 inputPort = kDefaultManualInputPort;
  quint16 filePort = kDefaultManualFilePort;

  bool operator==(const ManualAddress &) const = default;
};

struct DiscoverySettings
{
  bool enabled = true;
  QList<ManualAddress> manualAddresses;

  bool operator==(const DiscoverySettings &) const = default;
};

struct DiscoverySettingsLoadResult
{
  bool ok = false;
  DiscoverySettings settings;
  bool migrated = false;
  QString diagnostic;
};

[[nodiscard]] std::optional<ManualAddress> parseManualAddress(
    QString host, int inputPort = kDefaultManualInputPort, int filePort = kDefaultManualFilePort,
    QString *diagnostic = nullptr
);

class DiscoverySettingsStore final
{
public:
  explicit DiscoverySettingsStore(QSettings &settings);

  [[nodiscard]] DiscoverySettingsLoadResult load();
  [[nodiscard]] bool save(DiscoverySettings settings, QString *diagnostic = nullptr);

  [[nodiscard]] static QString schemaVersionKey();
  [[nodiscard]] static QString enabledKey();
  [[nodiscard]] static QString manualAddressesKey();
  [[nodiscard]] static QString legacyManualHostKey();
  [[nodiscard]] static QString legacyManualInputPortKey();
  [[nodiscard]] static QString legacyManualFilePortKey();

private:
  [[nodiscard]] bool saveValidated(DiscoverySettings settings, QString *diagnostic);

  QSettings &m_settings;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::ManualAddress)
Q_DECLARE_METATYPE(deskflow::relaydesk::DiscoverySettings)
