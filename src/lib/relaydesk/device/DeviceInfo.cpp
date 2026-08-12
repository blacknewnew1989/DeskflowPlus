/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/device/DeviceInfo.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QSet>

namespace deskflow::relaydesk {

namespace {
enum DeviceInfoKey : qint64
{
  ProtocolKey = 1,
  SchemaVersionKey = 2,
  DeviceIdKey = 3,
  DisplayNameKey = 4,
  PlatformKey = 5,
  ArchitectureKey = 6,
  AppVersionKey = 7,
  InputPortKey = 8,
  FilePortKey = 9,
  CapabilitiesKey = 10,
  CertificateFingerprintKey = 11,
};

enum CapabilityKey : qint64
{
  InputCapabilityKey = 1,
  ClipboardTextCapabilityKey = 2,
  ClipboardImageCapabilityKey = 3,
  FileV1CapabilityKey = 4,
  FolderV1CapabilityKey = 5,
  ResumeV1CapabilityKey = 6,
};

void setError(QString *errorMessage, const QString &message)
{
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
}

bool hasOnlyIntegerKeys(const QCborMap &map, const QSet<qint64> &allowedKeys)
{
  for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
    if (!it.key().isInteger() || !allowedKeys.contains(it.key().toInteger())) {
      return false;
    }
  }
  return true;
}

const QCborValue valueFor(const QCborMap &map, qint64 key)
{
  return map.value(QCborValue(key));
}

QCborValue cborKey(qint64 key)
{
  return QCborValue(key);
}

bool readRequiredString(const QCborMap &map, qint64 key, QString &output)
{
  const auto value = valueFor(map, key);
  if (!value.isString() || value.toString().isEmpty()) {
    return false;
  }
  output = value.toString();
  return true;
}

bool readPort(const QCborMap &map, qint64 key, quint16 &output)
{
  const auto value = valueFor(map, key);
  if (!value.isInteger()) {
    return false;
  }

  const auto port = value.toInteger();
  if (port < 0 || port > 65535) {
    return false;
  }

  output = static_cast<quint16>(port);
  return true;
}

QCborMap serializeCapabilities(const DeviceCapabilities &capabilities)
{
  return {
      {cborKey(InputCapabilityKey), QCborValue(capabilities.input)},
      {cborKey(ClipboardTextCapabilityKey), QCborValue(capabilities.clipboardText)},
      {cborKey(ClipboardImageCapabilityKey), QCborValue(capabilities.clipboardImage)},
      {cborKey(FileV1CapabilityKey), QCborValue(capabilities.fileV1)},
      {cborKey(FolderV1CapabilityKey), QCborValue(capabilities.folderV1)},
      {cborKey(ResumeV1CapabilityKey), QCborValue(capabilities.resumeV1)},
  };
}

std::optional<DeviceCapabilities> deserializeCapabilities(const QCborValue &value)
{
  if (!value.isMap()) {
    return std::nullopt;
  }

  const auto map = value.toMap();
  const QSet<qint64> allowedKeys = {
      InputCapabilityKey,  ClipboardTextCapabilityKey, ClipboardImageCapabilityKey,
      FileV1CapabilityKey, FolderV1CapabilityKey,      ResumeV1CapabilityKey,
  };
  if (map.size() != allowedKeys.size() || !hasOnlyIntegerKeys(map, allowedKeys)) {
    return std::nullopt;
  }

  for (const auto key : allowedKeys) {
    if (!valueFor(map, key).isBool()) {
      return std::nullopt;
    }
  }

  return DeviceCapabilities{
      .input = valueFor(map, InputCapabilityKey).toBool(),
      .clipboardText = valueFor(map, ClipboardTextCapabilityKey).toBool(),
      .clipboardImage = valueFor(map, ClipboardImageCapabilityKey).toBool(),
      .fileV1 = valueFor(map, FileV1CapabilityKey).toBool(),
      .folderV1 = valueFor(map, FolderV1CapabilityKey).toBool(),
      .resumeV1 = valueFor(map, ResumeV1CapabilityKey).toBool(),
  };
}

bool validateForSerialization(const DeviceInfo &device, QString *errorMessage)
{
  if (device.displayName.isEmpty() || device.platform.isEmpty() || device.architecture.isEmpty() ||
      device.appVersion.isEmpty()) {
    setError(errorMessage, QStringLiteral("Device information contains an empty required string"));
    return false;
  }

  if (!device.certificateFingerprintSha256.isEmpty() && device.certificateFingerprintSha256.size() != 32) {
    setError(errorMessage, QStringLiteral("Certificate fingerprint must contain exactly 32 bytes"));
    return false;
  }

  return true;
}
} // namespace

QByteArray DeviceInfoCodec::serialize(const DeviceInfo &device, QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  if (!validateForSerialization(device, errorMessage)) {
    return {};
  }

  QCborMap map = {
      {cborKey(ProtocolKey), QCborValue(QString::fromLatin1(kDeviceInfoProtocol))},
      {cborKey(SchemaVersionKey), QCborValue(kDeviceInfoSchemaVersion)},
      {cborKey(DeviceIdKey), QCborValue(device.deviceId.toBytes())},
      {cborKey(DisplayNameKey), QCborValue(device.displayName)},
      {cborKey(PlatformKey), QCborValue(device.platform)},
      {cborKey(ArchitectureKey), QCborValue(device.architecture)},
      {cborKey(AppVersionKey), QCborValue(device.appVersion)},
      {cborKey(InputPortKey), QCborValue(device.inputPort)},
      {cborKey(FilePortKey), QCborValue(device.filePort)},
      {cborKey(CapabilitiesKey), QCborValue(serializeCapabilities(device.capabilities))},
  };
  if (!device.certificateFingerprintSha256.isEmpty()) {
    map.insert(cborKey(CertificateFingerprintKey), QCborValue(device.certificateFingerprintSha256));
  }

  return QCborValue(map).toCbor();
}

std::optional<DeviceInfo> DeviceInfoCodec::deserialize(QByteArrayView bytes, QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  if (bytes.isEmpty()) {
    setError(errorMessage, QStringLiteral("Device information payload is empty"));
    return std::nullopt;
  }

  QCborParserError parserError;
  const auto value = QCborValue::fromCbor(bytes.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || !value.isMap()) {
    setError(errorMessage, QStringLiteral("Device information is not a valid CBOR map"));
    return std::nullopt;
  }

  const auto map = value.toMap();
  const QSet<qint64> allowedKeys = {
      ProtocolKey, SchemaVersionKey, DisplayNameKey, PlatformKey,     ArchitectureKey,           AppVersionKey,
      DeviceIdKey, InputPortKey,     FilePortKey,    CapabilitiesKey, CertificateFingerprintKey,
  };
  if (!hasOnlyIntegerKeys(map, allowedKeys) || map.size() < allowedKeys.size() - 1) {
    setError(errorMessage, QStringLiteral("Device information contains missing or unknown fields"));
    return std::nullopt;
  }

  const auto protocol = valueFor(map, ProtocolKey);
  if (!protocol.isString() || protocol.toString() != QString::fromLatin1(kDeviceInfoProtocol)) {
    setError(errorMessage, QStringLiteral("Unsupported device information protocol"));
    return std::nullopt;
  }

  const auto version = valueFor(map, SchemaVersionKey);
  if (!version.isInteger() || version.toInteger() != kDeviceInfoSchemaVersion) {
    setError(errorMessage, QStringLiteral("Unsupported device information schema version"));
    return std::nullopt;
  }

  const auto encodedId = valueFor(map, DeviceIdKey);
  if (!encodedId.isByteArray()) {
    setError(errorMessage, QStringLiteral("Device information contains an invalid device ID"));
    return std::nullopt;
  }
  const auto deviceId = DeviceId::fromBytes(encodedId.toByteArray());
  if (!deviceId.has_value()) {
    setError(errorMessage, QStringLiteral("Device information contains an invalid device ID"));
    return std::nullopt;
  }

  QString displayName;
  QString platform;
  QString architecture;
  QString appVersion;
  quint16 inputPort = 0;
  quint16 filePort = 0;
  if (!readRequiredString(map, DisplayNameKey, displayName) || !readRequiredString(map, PlatformKey, platform) ||
      !readRequiredString(map, ArchitectureKey, architecture) || !readRequiredString(map, AppVersionKey, appVersion) ||
      !readPort(map, InputPortKey, inputPort) || !readPort(map, FilePortKey, filePort)) {
    setError(errorMessage, QStringLiteral("Device information contains an invalid required field"));
    return std::nullopt;
  }

  const auto capabilities = deserializeCapabilities(valueFor(map, CapabilitiesKey));
  if (!capabilities.has_value()) {
    setError(errorMessage, QStringLiteral("Device information contains invalid capabilities"));
    return std::nullopt;
  }

  QByteArray fingerprint;
  const auto encodedFingerprint = valueFor(map, CertificateFingerprintKey);
  if (!encodedFingerprint.isUndefined()) {
    if (!encodedFingerprint.isByteArray() || encodedFingerprint.toByteArray().size() != 32) {
      setError(errorMessage, QStringLiteral("Device information contains an invalid certificate fingerprint"));
      return std::nullopt;
    }
    fingerprint = encodedFingerprint.toByteArray();
  }

  return DeviceInfo{
      .deviceId = *deviceId,
      .displayName = displayName,
      .platform = platform,
      .architecture = architecture,
      .appVersion = appVersion,
      .inputPort = inputPort,
      .filePort = filePort,
      .capabilities = *capabilities,
      .certificateFingerprintSha256 = fingerprint,
  };
}

} // namespace deskflow::relaydesk
