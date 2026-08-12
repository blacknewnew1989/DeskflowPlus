/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/DeviceHomeModel.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QHostAddress>
#include <QStringList>

#include <algorithm>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;

int presenceRank(DevicePresence presence)
{
  switch (presence) {
  case DevicePresence::Online:
    return 0;
  case DevicePresence::Pairing:
    return 1;
  case DevicePresence::Discovered:
    return 2;
  case DevicePresence::TrustViolation:
    return 3;
  case DevicePresence::Offline:
    return 4;
  }
  return 5;
}

QString displayName(const DeviceSnapshot &snapshot)
{
  return snapshot.alias.trimmed().isEmpty() ? snapshot.displayName : snapshot.alias;
}

QString statusText(DevicePresence presence)
{
  switch (presence) {
  case DevicePresence::Offline:
    return i18n::translate(Text::DevicesStatusOffline);
  case DevicePresence::Discovered:
    return i18n::translate(Text::DevicesStatusDiscovered);
  case DevicePresence::Pairing:
    return i18n::translate(Text::DevicesStatusPairing);
  case DevicePresence::Online:
    return i18n::translate(Text::DevicesStatusOnline);
  case DevicePresence::TrustViolation:
    return i18n::translate(Text::DevicesStatusTrustViolation);
  }
  return i18n::translate(Text::DevicesStatusError);
}

QList<int> allDataRoles()
{
  QList<int> roles{Qt::DisplayRole};
  for (int role = DeviceHomeModel::DeviceIdRole; role <= DeviceHomeModel::LastSeenUtcRole; ++role)
    roles.append(role);
  return roles;
}

} // namespace

DeviceHomeModel::DeviceHomeModel(QObject *parent) : QAbstractListModel(parent)
{
}

int DeviceHomeModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_devices.size();
}

QVariant DeviceHomeModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.parent().isValid() || index.column() != 0 || index.row() < 0 ||
      index.row() >= m_devices.size()) {
    return {};
  }

  const auto &device = m_devices.at(index.row());
  switch (role) {
  case Qt::DisplayRole:
  case DisplayNameRole:
    return displayName(device);
  case DeviceIdRole:
    return device.id.toString();
  case ReportedNameRole:
    return device.displayName;
  case AliasRole:
    return device.alias;
  case PlatformRole:
    return device.platform;
  case ArchitectureRole:
    return device.architecture;
  case PresenceRole:
    return static_cast<int>(device.presence);
  case StatusTextRole:
    return statusText(device.presence);
  case IsLocalRole:
    return isLocal(device.id);
  case IsOnlineRole:
    return device.presence == DevicePresence::Online;
  case IsTrustedRole:
    return device.trusted;
  case IsPairingRole:
    return device.presence == DevicePresence::Pairing;
  case LatencyMsRole:
    return device.latencyMs;
  case AddressesRole: {
    QStringList addresses;
    addresses.reserve(device.addresses.size());
    for (const auto &address : device.addresses)
      addresses.append(address.toString());
    return addresses;
  }
  case InputCapabilityRole:
    return device.capabilities.input;
  case ClipboardTextCapabilityRole:
    return device.capabilities.clipboardText;
  case ClipboardImageCapabilityRole:
    return device.capabilities.clipboardImage;
  case FileCapabilityRole:
    return device.capabilities.fileV1;
  case FolderCapabilityRole:
    return device.capabilities.folderV1;
  case ResumeCapabilityRole:
    return device.capabilities.resumeV1;
  case AutoAcceptFilesRole:
    return device.autoAcceptFiles;
  case FingerprintRole:
    return device.pinnedFingerprint;
  case LastSeenUtcRole:
    return device.lastSeenUtc;
  default:
    return {};
  }
}

QHash<int, QByteArray> DeviceHomeModel::roleNames() const
{
  return {
      {DeviceIdRole, "deviceId"},
      {DisplayNameRole, "displayName"},
      {ReportedNameRole, "reportedName"},
      {AliasRole, "alias"},
      {PlatformRole, "platform"},
      {ArchitectureRole, "architecture"},
      {PresenceRole, "presence"},
      {StatusTextRole, "statusText"},
      {IsLocalRole, "isLocal"},
      {IsOnlineRole, "isOnline"},
      {IsTrustedRole, "isTrusted"},
      {IsPairingRole, "isPairing"},
      {LatencyMsRole, "latencyMs"},
      {AddressesRole, "addresses"},
      {InputCapabilityRole, "canShareInput"},
      {ClipboardTextCapabilityRole, "canShareClipboardText"},
      {ClipboardImageCapabilityRole, "canShareClipboardImage"},
      {FileCapabilityRole, "canSendFiles"},
      {FolderCapabilityRole, "canSendFolders"},
      {ResumeCapabilityRole, "canResumeTransfers"},
      {AutoAcceptFilesRole, "autoAcceptFiles"},
      {FingerprintRole, "pinnedFingerprint"},
      {LastSeenUtcRole, "lastSeenUtc"},
  };
}

void DeviceHomeModel::setLocalDevice(const DeviceSnapshot &snapshot)
{
  if (!m_localDeviceId.has_value()) {
    m_localDeviceId = snapshot.id;
    upsert(snapshot);
    return;
  }

  if (*m_localDeviceId == snapshot.id) {
    upsert(snapshot);
    return;
  }

  beginResetModel();
  const auto oldLocalIndex = indexOf(*m_localDeviceId);
  if (oldLocalIndex >= 0)
    m_devices.removeAt(oldLocalIndex);

  m_localDeviceId = snapshot.id;
  const auto existingIndex = indexOf(snapshot.id);
  if (existingIndex >= 0)
    m_devices[existingIndex] = snapshot;
  else
    m_devices.append(snapshot);
  sortRows();
  endResetModel();
}

void DeviceHomeModel::upsertRemoteDevice(const DeviceSnapshot &snapshot)
{
  upsert(snapshot);
}

bool DeviceHomeModel::removeDevice(const DeviceId &deviceId)
{
  const auto row = indexOf(deviceId);
  if (row < 0)
    return false;

  beginRemoveRows(QModelIndex(), row, row);
  m_devices.removeAt(row);
  if (isLocal(deviceId))
    m_localDeviceId.reset();
  endRemoveRows();
  return true;
}

int DeviceHomeModel::indexOf(const DeviceId &deviceId) const
{
  for (int row = 0; row < m_devices.size(); ++row) {
    if (m_devices.at(row).id == deviceId)
      return row;
  }
  return -1;
}

std::optional<DeviceSnapshot> DeviceHomeModel::snapshot(const DeviceId &deviceId) const
{
  const auto row = indexOf(deviceId);
  if (row < 0)
    return std::nullopt;
  return m_devices.at(row);
}

bool DeviceHomeModel::isLocal(const DeviceId &deviceId) const
{
  return m_localDeviceId.has_value() && *m_localDeviceId == deviceId;
}

int DeviceHomeModel::insertionIndex(const DeviceSnapshot &snapshot, int ignoredIndex) const
{
  int destination = 0;
  for (int row = 0; row < m_devices.size(); ++row) {
    if (row == ignoredIndex)
      continue;
    if (compare(snapshot, m_devices.at(row)) < 0)
      break;
    ++destination;
  }
  return destination;
}

int DeviceHomeModel::compare(const DeviceSnapshot &left, const DeviceSnapshot &right) const
{
  const auto leftLocal = isLocal(left.id);
  const auto rightLocal = isLocal(right.id);
  if (leftLocal != rightLocal)
    return leftLocal ? -1 : 1;

  const auto leftPresence = presenceRank(left.presence);
  const auto rightPresence = presenceRank(right.presence);
  if (leftPresence != rightPresence)
    return leftPresence < rightPresence ? -1 : 1;

  if (left.trusted != right.trusted)
    return left.trusted ? -1 : 1;

  const auto caseInsensitiveName = QString::compare(displayName(left), displayName(right), Qt::CaseInsensitive);
  if (caseInsensitiveName != 0)
    return caseInsensitiveName;

  const auto exactName = QString::compare(displayName(left), displayName(right), Qt::CaseSensitive);
  if (exactName != 0)
    return exactName;

  return QString::compare(left.id.toString(), right.id.toString(), Qt::CaseSensitive);
}

void DeviceHomeModel::upsert(const DeviceSnapshot &snapshot)
{
  const auto existingRow = indexOf(snapshot.id);
  if (existingRow < 0) {
    const auto destination = insertionIndex(snapshot);
    beginInsertRows(QModelIndex(), destination, destination);
    m_devices.insert(destination, snapshot);
    endInsertRows();
    return;
  }

  const auto destination = insertionIndex(snapshot, existingRow);
  if (destination == existingRow) {
    m_devices[existingRow] = snapshot;
    const auto changedIndex = index(existingRow, 0);
    Q_EMIT dataChanged(changedIndex, changedIndex, allDataRoles());
    return;
  }

  const auto destinationChild = destination > existingRow ? destination + 1 : destination;
  beginMoveRows(QModelIndex(), existingRow, existingRow, QModelIndex(), destinationChild);
  m_devices.removeAt(existingRow);
  m_devices.insert(destination, snapshot);
  endMoveRows();

  const auto changedIndex = index(destination, 0);
  Q_EMIT dataChanged(changedIndex, changedIndex, allDataRoles());
}

void DeviceHomeModel::sortRows()
{
  std::sort(m_devices.begin(), m_devices.end(), [this](const auto &left, const auto &right) {
    return compare(left, right) < 0;
  });
}

} // namespace deskflow::relaydesk::model
