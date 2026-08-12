/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceSnapshot.h"

#include <QAbstractListModel>
#include <QList>

#include <optional>

namespace deskflow::relaydesk::model {

class DeviceHomeModel final : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role
  {
    DeviceIdRole = Qt::UserRole + 1,
    DisplayNameRole,
    ReportedNameRole,
    AliasRole,
    PlatformRole,
    ArchitectureRole,
    PresenceRole,
    StatusTextRole,
    IsLocalRole,
    IsOnlineRole,
    IsTrustedRole,
    IsPairingRole,
    LatencyMsRole,
    AddressesRole,
    InputCapabilityRole,
    ClipboardTextCapabilityRole,
    ClipboardImageCapabilityRole,
    FileCapabilityRole,
    FolderCapabilityRole,
    ResumeCapabilityRole,
    AutoAcceptFilesRole,
    FingerprintRole,
    LastSeenUtcRole,
    CanStartPairingRole,
    PairActionTextRole,
    CanSendItemsRole,
  };
  Q_ENUM(Role)

  explicit DeviceHomeModel(QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setLocalDevice(const DeviceSnapshot &snapshot);
  void upsertRemoteDevice(const DeviceSnapshot &snapshot);
  bool removeDevice(const DeviceId &deviceId);

  [[nodiscard]] int indexOf(const DeviceId &deviceId) const;
  [[nodiscard]] std::optional<DeviceSnapshot> snapshot(const DeviceId &deviceId) const;
  [[nodiscard]] bool canSendItems(const DeviceId &deviceId) const;

private:
  [[nodiscard]] bool isLocal(const DeviceId &deviceId) const;
  [[nodiscard]] int insertionIndex(const DeviceSnapshot &snapshot, int ignoredIndex = -1) const;
  [[nodiscard]] int compare(const DeviceSnapshot &left, const DeviceSnapshot &right) const;
  void upsert(const DeviceSnapshot &snapshot);
  void sortRows();

  QList<DeviceSnapshot> m_devices;
  std::optional<DeviceId> m_localDeviceId;
};

} // namespace deskflow::relaydesk::model
