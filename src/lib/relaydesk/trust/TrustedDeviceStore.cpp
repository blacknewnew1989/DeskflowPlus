/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace deskflow::relaydesk {
namespace {

bool validateAndNormalize(TrustedDevice &device, QString *diagnostic)
{
  if (device.fingerprintSha256.size() != kSha256FingerprintBytes) {
    if (diagnostic != nullptr) {
      *diagnostic = QStringLiteral("trusted device fingerprint must be a SHA-256 digest");
    }
    return false;
  }
  if (device.platform.trimmed().isEmpty()) {
    if (diagnostic != nullptr) {
      *diagnostic = QStringLiteral("trusted device platform must not be empty");
    }
    return false;
  }

  QStringList normalizedAddresses;
  for (const auto &text : std::as_const(device.lastAddresses)) {
    QHostAddress address;
    if (!address.setAddress(text)) {
      if (diagnostic != nullptr) {
        *diagnostic = QStringLiteral("trusted device address is invalid: %1").arg(text);
      }
      return false;
    }
    const QString normalized = address.toString();
    if (!normalizedAddresses.contains(normalized)) {
      normalizedAddresses.append(normalized);
    }
  }
  device.alias = device.alias.trimmed();
  device.platform = device.platform.trimmed();
  device.lastAddresses = std::move(normalizedAddresses);
  return true;
}

QJsonObject toJson(const TrustedDevice &device)
{
  QJsonArray addresses;
  for (const auto &address : device.lastAddresses) {
    addresses.append(address);
  }
  return {
      {QStringLiteral("deviceId"), device.deviceId.toString()},
      {QStringLiteral("alias"), device.alias},
      {QStringLiteral("platform"), device.platform},
      {QStringLiteral("fingerprintSha256"), QString::fromLatin1(device.fingerprintSha256.toBase64())},
      {QStringLiteral("lastAddresses"), addresses},
      {QStringLiteral("autoAcceptFiles"), device.autoAcceptFiles},
      {QStringLiteral("revoked"), device.revoked},
  };
}

std::optional<TrustedDevice> fromJson(const QJsonValue &value, QString *diagnostic)
{
  if (!value.isObject()) {
    *diagnostic = QStringLiteral("trusted device entry must be an object");
    return std::nullopt;
  }
  const QJsonObject object = value.toObject();
  const auto idValue = object.value(QStringLiteral("deviceId"));
  const auto aliasValue = object.value(QStringLiteral("alias"));
  const auto platformValue = object.value(QStringLiteral("platform"));
  const auto fingerprintValue = object.value(QStringLiteral("fingerprintSha256"));
  const auto addressesValue = object.value(QStringLiteral("lastAddresses"));
  const auto autoAcceptValue = object.value(QStringLiteral("autoAcceptFiles"));
  const auto revokedValue = object.value(QStringLiteral("revoked"));
  if (!idValue.isString() || !aliasValue.isString() || !platformValue.isString() || !fingerprintValue.isString() ||
      !addressesValue.isArray() || !autoAcceptValue.isBool() || !revokedValue.isBool()) {
    *diagnostic = QStringLiteral("trusted device entry has missing or invalid fields");
    return std::nullopt;
  }

  const auto deviceId = DeviceId::fromString(idValue.toString());
  if (!deviceId.has_value()) {
    *diagnostic = QStringLiteral("trusted device id is invalid");
    return std::nullopt;
  }
  const auto decodedFingerprint =
      QByteArray::fromBase64Encoding(fingerprintValue.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  if (!decodedFingerprint || decodedFingerprint.decoded.size() != kSha256FingerprintBytes) {
    *diagnostic = QStringLiteral("trusted device fingerprint is invalid");
    return std::nullopt;
  }

  QStringList addresses;
  for (const auto &addressValue : addressesValue.toArray()) {
    if (!addressValue.isString()) {
      *diagnostic = QStringLiteral("trusted device address must be a string");
      return std::nullopt;
    }
    addresses.append(addressValue.toString());
  }

  TrustedDevice device{
      .deviceId = *deviceId,
      .alias = aliasValue.toString(),
      .platform = platformValue.toString(),
      .fingerprintSha256 = decodedFingerprint.decoded,
      .lastAddresses = std::move(addresses),
      .autoAcceptFiles = autoAcceptValue.toBool(),
      .revoked = revokedValue.toBool(),
  };
  if (!validateAndNormalize(device, diagnostic)) {
    return std::nullopt;
  }
  return device;
}

std::optional<QHash<DeviceId, TrustedDevice>> parseStore(const QByteArray &contents, QString *diagnostic)
{
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    *diagnostic = QStringLiteral("trusted device store is not valid JSON: %1").arg(parseError.errorString());
    return std::nullopt;
  }
  const QJsonObject root = document.object();
  const auto schemaValue = root.value(QStringLiteral("schemaVersion"));
  const auto devicesValue = root.value(QStringLiteral("devices"));
  if (!schemaValue.isDouble() || schemaValue.toInt(-1) != kTrustedDeviceStoreSchemaVersion || !devicesValue.isArray()) {
    *diagnostic = QStringLiteral("trusted device store schema is unsupported");
    return std::nullopt;
  }

  QHash<DeviceId, TrustedDevice> devices;
  for (const auto &value : devicesValue.toArray()) {
    const auto device = fromJson(value, diagnostic);
    if (!device.has_value()) {
      return std::nullopt;
    }
    if (devices.contains(device->deviceId)) {
      *diagnostic = QStringLiteral("trusted device store contains a duplicate device id");
      return std::nullopt;
    }
    devices.insert(device->deviceId, *device);
  }
  return devices;
}

std::optional<QHash<DeviceId, TrustedDevice>> readStore(const QString &path, QString *diagnostic)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *diagnostic = QStringLiteral("could not open trusted device store %1: %2").arg(path, file.errorString());
    return std::nullopt;
  }
  return parseStore(file.readAll(), diagnostic);
}

QByteArray serializeStore(const QList<TrustedDevice> &devices)
{
  QJsonArray array;
  for (const auto &device : devices) {
    array.append(toJson(device));
  }
  return QJsonDocument(QJsonObject{
                           {QStringLiteral("schemaVersion"), kTrustedDeviceStoreSchemaVersion},
                           {QStringLiteral("devices"), array},
                       })
      .toJson(QJsonDocument::Indented);
}

bool writeAtomic(const QString &path, QByteArrayView contents, QString *diagnostic)
{
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath())) {
    *diagnostic = QStringLiteral("could not create trusted device store directory: %1").arg(info.absolutePath());
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    *diagnostic = QStringLiteral("could not open trusted device store for writing: %1").arg(file.errorString());
    return false;
  }
  if (file.write(contents.data(), contents.size()) != contents.size()) {
    *diagnostic = QStringLiteral("could not write trusted device store: %1").arg(file.errorString());
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    *diagnostic = QStringLiteral("could not atomically commit trusted device store: %1").arg(file.errorString());
    return false;
  }
  return true;
}

} // namespace

TrustedDeviceStore::TrustedDeviceStore(QString path) : m_path(std::move(path))
{
}

TrustedDeviceStoreResult TrustedDeviceStore::load()
{
  m_devices.clear();
  if (!QFileInfo::exists(m_path) && !QFileInfo::exists(backupPath())) {
    return {.ok = true, .source = TrustedDeviceLoadSource::Empty};
  }

  QString primaryDiagnostic;
  if (QFileInfo::exists(m_path)) {
    if (auto devices = readStore(m_path, &primaryDiagnostic); devices.has_value()) {
      m_devices = std::move(*devices);
      return {.ok = true, .source = TrustedDeviceLoadSource::Primary};
    }
  } else {
    primaryDiagnostic = QStringLiteral("primary trusted device store is missing");
  }

  QString backupDiagnostic;
  if (QFileInfo::exists(backupPath())) {
    if (auto devices = readStore(backupPath(), &backupDiagnostic); devices.has_value()) {
      m_devices = std::move(*devices);
      return {
          .ok = true,
          .source = TrustedDeviceLoadSource::Backup,
          .diagnostic = QStringLiteral("recovered trusted devices from backup after: %1").arg(primaryDiagnostic),
      };
    }
  } else {
    backupDiagnostic = QStringLiteral("backup trusted device store is missing");
  }

  return {
      .ok = false,
      .source = TrustedDeviceLoadSource::Empty,
      .diagnostic = QStringLiteral("trusted device store and backup are unusable: %1; %2")
                        .arg(primaryDiagnostic, backupDiagnostic),
  };
}

TrustedDeviceStoreResult TrustedDeviceStore::save() const
{
  const QByteArray contents = serializeStore(devices());
  QString diagnostic;
  if (!writeAtomic(m_path, QByteArrayView(contents), &diagnostic)) {
    return {.ok = false, .diagnostic = std::move(diagnostic)};
  }
  if (!writeAtomic(backupPath(), QByteArrayView(contents), &diagnostic)) {
    return {
        .ok = false,
        .source = TrustedDeviceLoadSource::Primary,
        .diagnostic = QStringLiteral("primary store was saved but backup failed: %1").arg(diagnostic),
    };
  }
  return {.ok = true, .source = TrustedDeviceLoadSource::Primary};
}

QList<TrustedDevice> TrustedDeviceStore::devices() const
{
  QList<TrustedDevice> result = m_devices.values();
  std::ranges::sort(result, {}, [](const TrustedDevice &device) { return device.deviceId.toString(); });
  return result;
}

std::optional<TrustedDevice> TrustedDeviceStore::find(const DeviceId &deviceId) const
{
  const auto iterator = m_devices.constFind(deviceId);
  if (iterator == m_devices.cend()) {
    return std::nullopt;
  }
  return iterator.value();
}

TrustStatus TrustedDeviceStore::trustStatus(const DeviceId &deviceId, QByteArrayView fingerprintSha256) const
{
  const auto device = find(deviceId);
  if (!device.has_value()) {
    return TrustStatus::Unknown;
  }
  if (device->revoked) {
    return TrustStatus::Revoked;
  }
  if (fingerprintSha256.size() != kSha256FingerprintBytes ||
      fingerprintSha256 != QByteArrayView(device->fingerprintSha256)) {
    return TrustStatus::FingerprintMismatch;
  }
  return TrustStatus::Trusted;
}

bool TrustedDeviceStore::upsert(TrustedDevice device, QString *diagnostic)
{
  if (!validateAndNormalize(device, diagnostic)) {
    return false;
  }
  m_devices.insert(device.deviceId, std::move(device));
  return true;
}

bool TrustedDeviceStore::revoke(const DeviceId &deviceId)
{
  auto iterator = m_devices.find(deviceId);
  if (iterator == m_devices.end()) {
    return false;
  }
  iterator->revoked = true;
  iterator->autoAcceptFiles = false;
  return true;
}

bool TrustedDeviceStore::remove(const DeviceId &deviceId)
{
  return m_devices.remove(deviceId) > 0;
}

const QString &TrustedDeviceStore::path() const noexcept
{
  return m_path;
}

QString TrustedDeviceStore::backupPath() const
{
  return m_path + QStringLiteral(".bak");
}

} // namespace deskflow::relaydesk
