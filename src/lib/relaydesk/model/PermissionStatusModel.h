/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/PermissionSnapshot.h"

#include <QAbstractListModel>
#include <QString>

#include <optional>

namespace deskflow::relaydesk::model {

class PermissionStatusModel final : public QAbstractListModel
{
  Q_OBJECT

  Q_PROPERTY(bool bannerVisible READ bannerVisible NOTIFY snapshotChanged)
  Q_PROPERTY(QString bannerTitle READ bannerTitle NOTIFY snapshotChanged)
  Q_PROPERTY(QString bannerMessage READ bannerMessage NOTIFY snapshotChanged)
  Q_PROPERTY(bool canOpenPrimarySettings READ canOpenPrimarySettings NOTIFY snapshotChanged)
  Q_PROPERTY(QString openSettingsActionText READ openSettingsActionText CONSTANT)
  Q_PROPERTY(bool canCaptureInput READ canCaptureInput NOTIFY snapshotChanged)
  Q_PROPERTY(bool canControlInput READ canControlInput NOTIFY snapshotChanged)
  Q_PROPERTY(bool canDiscoverDevices READ canDiscoverDevices NOTIFY snapshotChanged)
  Q_PROPERTY(bool canConnectDevices READ canConnectDevices NOTIFY snapshotChanged)

public:
  enum Role
  {
    KindRole = Qt::UserRole + 1,
    StateRole,
    TitleRole,
    StatusTextRole,
    MessageTextRole,
    ErrorCodeRole,
    NeedsAttentionRole,
    CanOpenSettingsRole,
    ActionTextRole,
    PurposeTextRole,
    AffectedCapabilityTextRole,
  };
  Q_ENUM(Role)

  enum Capability
  {
    CaptureInputCapability,
    ControlInputCapability,
    LocalDiscoveryCapability,
    DirectConnectionCapability,
    FileTransferCapability,
    TransferHistoryCapability,
    SettingsCapability,
  };
  Q_ENUM(Capability)

  explicit PermissionStatusModel(PermissionPlatform platform, QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] PermissionPlatform platform() const noexcept;
  [[nodiscard]] bool bannerVisible() const;
  [[nodiscard]] QString bannerTitle() const;
  [[nodiscard]] QString bannerMessage() const;
  [[nodiscard]] bool canOpenPrimarySettings() const;
  [[nodiscard]] QString openSettingsActionText() const;
  [[nodiscard]] bool canCaptureInput() const;
  [[nodiscard]] bool canControlInput() const;
  [[nodiscard]] bool canDiscoverDevices() const;
  [[nodiscard]] bool canConnectDevices() const;
  [[nodiscard]] Q_INVOKABLE bool allowsCapability(Capability capability) const;

  // Returns false for a snapshot from another platform. No platform APIs are
  // called here; A4/A5 probes inject immutable values through this method.
  bool setSnapshot(const PermissionSnapshot &snapshot);

public Q_SLOTS:
  bool requestOpenSettings(int row);
  bool requestPrimarySettings();

Q_SIGNALS:
  void snapshotChanged();
  void openSettingsRequested(deskflow::relaydesk::PermissionKind kind);

private:
  [[nodiscard]] static QList<PermissionKind> kindsForPlatform(PermissionPlatform platform);
  [[nodiscard]] static PermissionSnapshot normalized(const PermissionSnapshot &snapshot);
  [[nodiscard]] static bool needsAttention(PermissionState state);
  [[nodiscard]] static QString titleText(PermissionKind kind);
  [[nodiscard]] static QString statusText(PermissionState state);
  [[nodiscard]] static QString purposeText(PermissionKind kind);
  [[nodiscard]] static QString affectedCapabilityText(PermissionKind kind);
  [[nodiscard]] static QString messageText(const PermissionProbeEntry &entry);
  [[nodiscard]] static PermissionErrorCode expectedErrorCode(PermissionKind kind);
  [[nodiscard]] bool permissionAllows(PermissionKind kind) const;
  [[nodiscard]] int primaryAttentionRow() const;

  PermissionSnapshot m_snapshot;
};

} // namespace deskflow::relaydesk::model
