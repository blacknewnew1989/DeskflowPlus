/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoverySettings.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVariant>

#include <utility>

namespace deskflow::relaydesk {

namespace {
constexpr auto kSettingsPrefix = "relaydesk/discovery/";
constexpr auto kSchemaVersionName = "schemaVersion";
constexpr auto kEnabledName = "enabled";
constexpr auto kManualAddressesName = "manualAddresses";
constexpr auto kLegacyManualHostName = "manualHost";
constexpr auto kLegacyManualInputPortName = "manualInputPort";
constexpr auto kLegacyManualFilePortName = "manualFilePort";
constexpr auto kHostName = "host";
constexpr auto kInputPortName = "inputPort";
constexpr auto kFilePortName = "filePort";

void setDiagnostic(QString *diagnostic, const QString &message)
{
  if (diagnostic != nullptr) {
    *diagnostic = message;
  }
}

QString key(const char *name)
{
  return QString::fromLatin1(kSettingsPrefix) + QString::fromLatin1(name);
}

bool validPort(int port)
{
  return port >= 1 && port <= 65535;
}

bool isValidAceHostname(const QByteArray &hostname)
{
  if (hostname.isEmpty() || hostname.size() > 253) {
    return false;
  }
  static const QRegularExpression labelPattern(QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
  const auto labels = hostname.split('.');
  for (const auto &label : labels) {
    if (!labelPattern.match(QString::fromLatin1(label)).hasMatch()) {
      return false;
    }
  }
  return true;
}

std::optional<int> readPort(const QVariant &value)
{
  bool ok = false;
  const auto port = value.toLongLong(&ok);
  if (!ok || port < 1 || port > 65535) {
    return std::nullopt;
  }
  return static_cast<int>(port);
}

QList<ManualAddress> normalizeAndDeduplicate(
    const QList<ManualAddress> &entries, QString *diagnostic, bool *ok
)
{
  QList<ManualAddress> result;
  *ok = false;
  for (const auto &entry : entries) {
    QString entryDiagnostic;
    const auto normalized = parseManualAddress(entry.host, entry.inputPort, entry.filePort, &entryDiagnostic);
    if (!normalized.has_value()) {
      setDiagnostic(diagnostic, entryDiagnostic);
      return {};
    }
    if (!result.contains(*normalized)) {
      result.append(*normalized);
    }
  }
  *ok = true;
  return result;
}
} // namespace

std::optional<ManualAddress>
parseManualAddress(QString host, int inputPort, int filePort, QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!validPort(inputPort) || !validPort(filePort)) {
    setDiagnostic(diagnostic, QStringLiteral("Manual address ports must be in the range 1..65535"));
    return std::nullopt;
  }

  host = host.trimmed();
  bool bracketed = false;
  if (host.startsWith(QLatin1Char('[')) || host.endsWith(QLatin1Char(']'))) {
    if (host.size() < 3 || !host.startsWith(QLatin1Char('[')) || !host.endsWith(QLatin1Char(']'))) {
      setDiagnostic(diagnostic, QStringLiteral("Manual IPv6 address has mismatched brackets"));
      return std::nullopt;
    }
    bracketed = true;
    host = host.sliced(1, host.size() - 2);
  }
  if (host.isEmpty()) {
    setDiagnostic(diagnostic, QStringLiteral("Manual address host must not be empty"));
    return std::nullopt;
  }

  QHostAddress literalAddress;
  if (literalAddress.setAddress(host)) {
    if (literalAddress.isNull() || literalAddress == QHostAddress::AnyIPv4 ||
        literalAddress == QHostAddress::AnyIPv6 || literalAddress.isMulticast() || literalAddress.isBroadcast()) {
      setDiagnostic(diagnostic, QStringLiteral("Manual address must identify a unicast host"));
      return std::nullopt;
    }
    return ManualAddress{
        .host = literalAddress.toString(),
        .inputPort = static_cast<quint16>(inputPort),
        .filePort = static_cast<quint16>(filePort),
    };
  }

  if (bracketed) {
    setDiagnostic(diagnostic, QStringLiteral("Bracketed manual address must contain IPv6"));
    return std::nullopt;
  }

  if (host.contains(QLatin1Char(':')) ||
      QRegularExpression(QStringLiteral("^[0-9.]+$")).match(host).hasMatch()) {
    setDiagnostic(diagnostic, QStringLiteral("Manual address is not a valid IP address"));
    return std::nullopt;
  }
  if (host.endsWith(QLatin1Char('.'))) {
    host.chop(1);
  }
  if (host.endsWith(QLatin1Char('.'))) {
    setDiagnostic(diagnostic, QStringLiteral("Manual hostname has an empty trailing label"));
    return std::nullopt;
  }
  const auto ace = QUrl::toAce(host).toLower();
  if (!isValidAceHostname(ace)) {
    setDiagnostic(diagnostic, QStringLiteral("Manual address is not a valid hostname"));
    return std::nullopt;
  }

  return ManualAddress{
      .host = QString::fromLatin1(ace),
      .inputPort = static_cast<quint16>(inputPort),
      .filePort = static_cast<quint16>(filePort),
  };
}

DiscoverySettingsStore::DiscoverySettingsStore(QSettings &settings) : m_settings(settings)
{
}

DiscoverySettingsLoadResult DiscoverySettingsStore::load()
{
  const auto schemaValue = m_settings.value(schemaVersionKey());
  if (!schemaValue.isValid()) {
    const auto legacyHost = m_settings.value(legacyManualHostKey()).toString();
    if (legacyHost.trimmed().isEmpty()) {
      DiscoverySettings migrated{
          .enabled = m_settings.value(enabledKey(), true).toBool(),
      };
      if (!m_settings.contains(enabledKey())) {
        return {.ok = true, .settings = std::move(migrated)};
      }
      QString diagnostic;
      if (!save(migrated, &diagnostic)) {
        return {.ok = false, .diagnostic = QStringLiteral("Unable to migrate discovery settings: %1").arg(diagnostic)};
      }
      return {.ok = true, .settings = std::move(migrated), .migrated = true};
    }
    const auto inputPort = m_settings.value(legacyManualInputPortKey(), kDefaultManualInputPort).toInt();
    const auto filePort = m_settings.value(legacyManualFilePortKey(), kDefaultManualFilePort).toInt();
    QString diagnostic;
    const auto migratedAddress = parseManualAddress(legacyHost, inputPort, filePort, &diagnostic);
    if (!migratedAddress.has_value()) {
      return {.ok = false, .diagnostic = QStringLiteral("Legacy manual address is invalid: %1").arg(diagnostic)};
    }
    DiscoverySettings migrated{
        .enabled = m_settings.value(enabledKey(), true).toBool(),
        .manualAddresses = {*migratedAddress},
    };
    if (!save(migrated, &diagnostic)) {
      return {.ok = false, .diagnostic = QStringLiteral("Unable to migrate discovery settings: %1").arg(diagnostic)};
    }
    m_settings.remove(legacyManualHostKey());
    m_settings.remove(legacyManualInputPortKey());
    m_settings.remove(legacyManualFilePortKey());
    m_settings.sync();
    return {.ok = true, .settings = std::move(migrated), .migrated = true};
  }

  bool schemaOk = false;
  const auto schemaVersion = schemaValue.toInt(&schemaOk);
  if (!schemaOk || schemaVersion != kDiscoverySettingsSchemaVersion) {
    return {.ok = false, .diagnostic = QStringLiteral("Unsupported discovery settings schema version")};
  }

  DiscoverySettings settings;
  settings.enabled = m_settings.value(enabledKey(), true).toBool();
  const auto count = m_settings.beginReadArray(manualAddressesKey());
  for (int index = 0; index < count; ++index) {
    m_settings.setArrayIndex(index);
    const auto inputPort = readPort(
        m_settings.value(QString::fromLatin1(kInputPortName), kDefaultManualInputPort)
    );
    const auto filePort = readPort(
        m_settings.value(QString::fromLatin1(kFilePortName), kDefaultManualFilePort)
    );
    if (!inputPort.has_value() || !filePort.has_value()) {
      m_settings.endArray();
      return {.ok = false, .diagnostic = QStringLiteral("Manual address contains an invalid port")};
    }
    QString diagnostic;
    const auto address = parseManualAddress(
        m_settings.value(QString::fromLatin1(kHostName)).toString(), *inputPort, *filePort, &diagnostic
    );
    if (!address.has_value()) {
      m_settings.endArray();
      return {.ok = false, .diagnostic = std::move(diagnostic)};
    }
    if (!settings.manualAddresses.contains(*address)) {
      settings.manualAddresses.append(*address);
    }
  }
  m_settings.endArray();
  return {.ok = true, .settings = std::move(settings)};
}

bool DiscoverySettingsStore::save(DiscoverySettings settings, QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  bool normalized = false;
  settings.manualAddresses =
      normalizeAndDeduplicate(settings.manualAddresses, diagnostic, &normalized);
  if (!normalized) {
    return false;
  }

  m_settings.setValue(schemaVersionKey(), kDiscoverySettingsSchemaVersion);
  m_settings.setValue(enabledKey(), settings.enabled);
  m_settings.remove(manualAddressesKey());
  m_settings.beginWriteArray(manualAddressesKey(), settings.manualAddresses.size());
  for (qsizetype index = 0; index < settings.manualAddresses.size(); ++index) {
    const auto &entry = settings.manualAddresses.at(index);
    m_settings.setArrayIndex(static_cast<int>(index));
    m_settings.setValue(QString::fromLatin1(kHostName), entry.host);
    m_settings.setValue(QString::fromLatin1(kInputPortName), entry.inputPort);
    m_settings.setValue(QString::fromLatin1(kFilePortName), entry.filePort);
  }
  m_settings.endArray();
  m_settings.sync();
  if (m_settings.status() != QSettings::NoError) {
    setDiagnostic(diagnostic, QStringLiteral("Unable to persist RelayDesk discovery settings"));
    return false;
  }
  return true;
}

QString DiscoverySettingsStore::schemaVersionKey()
{
  return key(kSchemaVersionName);
}

QString DiscoverySettingsStore::enabledKey()
{
  return key(kEnabledName);
}

QString DiscoverySettingsStore::manualAddressesKey()
{
  return key(kManualAddressesName);
}

QString DiscoverySettingsStore::legacyManualHostKey()
{
  return key(kLegacyManualHostName);
}

QString DiscoverySettingsStore::legacyManualInputPortKey()
{
  return key(kLegacyManualInputPortName);
}

QString DiscoverySettingsStore::legacyManualFilePortKey()
{
  return key(kLegacyManualFilePortName);
}

} // namespace deskflow::relaydesk
