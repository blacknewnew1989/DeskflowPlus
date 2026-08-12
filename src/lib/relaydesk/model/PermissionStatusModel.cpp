/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/PermissionStatusModel.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;

int attentionRank(PermissionState state)
{
  switch (state) {
  case PermissionState::Denied:
    return 0;
  case PermissionState::NeedsAction:
    return 1;
  case PermissionState::Unknown:
    return 2;
  case PermissionState::NotRequired:
  case PermissionState::Granted:
    return std::numeric_limits<int>::max();
  }
  return 2;
}

} // namespace

PermissionStatusModel::PermissionStatusModel(PermissionPlatform platform, QObject *parent)
    : QAbstractListModel(parent),
      m_snapshot(normalized({.platform = platform}))
{
}

int PermissionStatusModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(m_snapshot.entries.size());
}

QVariant PermissionStatusModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_snapshot.entries.size())
    return {};

  const auto &entry = m_snapshot.entries.at(index.row());
  switch (role) {
  case Qt::DisplayRole:
  case TitleRole:
    return titleText(entry.kind);
  case KindRole:
    return static_cast<int>(entry.kind);
  case StateRole:
    return static_cast<int>(entry.state);
  case StatusTextRole:
    return statusText(entry.state);
  case MessageTextRole:
    return messageText(entry);
  case ErrorCodeRole:
    return entry.errorCode;
  case NeedsAttentionRole:
    return needsAttention(entry.state);
  case CanOpenSettingsRole:
    return needsAttention(entry.state) && entry.canOpenSettings;
  case ActionTextRole:
    return needsAttention(entry.state) && entry.canOpenSettings ? openSettingsActionText() : QString();
  default:
    return {};
  }
}

QHash<int, QByteArray> PermissionStatusModel::roleNames() const
{
  return {
      {KindRole, "kind"},
      {StateRole, "state"},
      {TitleRole, "title"},
      {StatusTextRole, "statusText"},
      {MessageTextRole, "messageText"},
      {ErrorCodeRole, "errorCode"},
      {NeedsAttentionRole, "needsAttention"},
      {CanOpenSettingsRole, "canOpenSettings"},
      {ActionTextRole, "actionText"},
  };
}

PermissionPlatform PermissionStatusModel::platform() const noexcept
{
  return m_snapshot.platform;
}

bool PermissionStatusModel::bannerVisible() const
{
  return primaryAttentionRow() >= 0;
}

QString PermissionStatusModel::bannerTitle() const
{
  const auto row = primaryAttentionRow();
  if (row < 0)
    return {};
  return i18n::translate(
      m_snapshot.entries.at(row).state == PermissionState::Unknown ? Text::PermissionsBannerUnknownTitle
                                                                   : Text::PermissionsBannerAttentionTitle
  );
}

QString PermissionStatusModel::bannerMessage() const
{
  const auto row = primaryAttentionRow();
  return row < 0 ? QString() : messageText(m_snapshot.entries.at(row));
}

bool PermissionStatusModel::canOpenPrimarySettings() const
{
  const auto row = primaryAttentionRow();
  return row >= 0 && data(index(row, 0), CanOpenSettingsRole).toBool();
}

QString PermissionStatusModel::openSettingsActionText() const
{
  return i18n::translate(Text::PermissionsActionOpenSettings);
}

bool PermissionStatusModel::setSnapshot(const PermissionSnapshot &snapshot)
{
  if (snapshot.platform != m_snapshot.platform)
    return false;

  const auto next = normalized(snapshot);
  if (next == m_snapshot)
    return true;

  beginResetModel();
  m_snapshot = next;
  endResetModel();
  Q_EMIT snapshotChanged();
  return true;
}

bool PermissionStatusModel::requestOpenSettings(int row)
{
  const auto indexForRow = index(row, 0);
  if (!indexForRow.isValid() || !data(indexForRow, CanOpenSettingsRole).toBool())
    return false;
  Q_EMIT openSettingsRequested(m_snapshot.entries.at(row).kind);
  return true;
}

bool PermissionStatusModel::requestPrimarySettings()
{
  return requestOpenSettings(primaryAttentionRow());
}

QList<PermissionKind> PermissionStatusModel::kindsForPlatform(PermissionPlatform platform)
{
  switch (platform) {
  case PermissionPlatform::Windows:
    return {PermissionKind::WindowsFirewall, PermissionKind::WindowsListeningPort};
  case PermissionPlatform::MacOS:
    return {
        PermissionKind::MacLocalNetwork,
        PermissionKind::MacAccessibility,
        PermissionKind::MacInputMonitoring,
    };
  case PermissionPlatform::Other:
    return {};
  }
  return {};
}

PermissionSnapshot PermissionStatusModel::normalized(const PermissionSnapshot &snapshot)
{
  PermissionSnapshot result{.platform = snapshot.platform, .checkedAtUtc = snapshot.checkedAtUtc};
  for (const auto kind : kindsForPlatform(snapshot.platform)) {
    const auto found = std::find_if(snapshot.entries.cbegin(), snapshot.entries.cend(), [kind](const auto &entry) {
      return entry.kind == kind;
    });
    auto entry = found == snapshot.entries.cend() ? PermissionProbeEntry{.kind = kind} : *found;
    // Diagnostics are retained by the platform probe for logs, but deliberately
    // discarded at the UI boundary.
    entry.diagnostic.clear();
    result.entries.append(std::move(entry));
  }
  return result;
}

bool PermissionStatusModel::needsAttention(PermissionState state)
{
  return state == PermissionState::Unknown || state == PermissionState::Denied || state == PermissionState::NeedsAction;
}

QString PermissionStatusModel::titleText(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::WindowsFirewall:
    return i18n::translate(Text::PermissionsKindWindowsFirewall);
  case PermissionKind::WindowsListeningPort:
    return i18n::translate(Text::PermissionsKindWindowsPort);
  case PermissionKind::MacLocalNetwork:
    return i18n::translate(Text::PermissionsKindMacLocalNetwork);
  case PermissionKind::MacAccessibility:
    return i18n::translate(Text::PermissionsKindMacAccessibility);
  case PermissionKind::MacInputMonitoring:
    return i18n::translate(Text::PermissionsKindMacInputMonitoring);
  }
  return {};
}

QString PermissionStatusModel::statusText(PermissionState state)
{
  switch (state) {
  case PermissionState::Unknown:
    return i18n::translate(Text::PermissionsStatusUnknown);
  case PermissionState::NotRequired:
    return i18n::translate(Text::PermissionsStatusNotRequired);
  case PermissionState::Granted:
    return i18n::translate(Text::PermissionsStatusGranted);
  case PermissionState::Denied:
    return i18n::translate(Text::PermissionsStatusDenied);
  case PermissionState::NeedsAction:
    return i18n::translate(Text::PermissionsStatusNeedsAction);
  }
  return i18n::translate(Text::PermissionsStatusUnknown);
}

QString PermissionStatusModel::messageText(const PermissionProbeEntry &entry)
{
  if (entry.state == PermissionState::Granted || entry.state == PermissionState::NotRequired)
    return {};
  if (entry.errorCode == static_cast<int>(PermissionErrorCode::ProbeUnavailable))
    return i18n::translate(Text::PermissionsMessageProbeUnavailable);
  if (entry.state == PermissionState::Unknown)
    return i18n::translate(Text::PermissionsMessageUnknown);
  if (entry.errorCode != static_cast<int>(PermissionErrorCode::None) &&
      entry.errorCode != expectedErrorCode(entry.kind)) {
    return i18n::translate(Text::PermissionsMessageReview);
  }

  switch (entry.kind) {
  case PermissionKind::WindowsFirewall:
    return i18n::translate(Text::PermissionsMessageWindowsFirewall);
  case PermissionKind::WindowsListeningPort:
    return i18n::translate(Text::PermissionsMessageWindowsPort);
  case PermissionKind::MacLocalNetwork:
    return i18n::translate(Text::PermissionsMessageMacLocalNetwork);
  case PermissionKind::MacAccessibility:
    return i18n::translate(Text::PermissionsMessageMacAccessibility);
  case PermissionKind::MacInputMonitoring:
    return i18n::translate(Text::PermissionsMessageMacInputMonitoring);
  }
  return i18n::translate(Text::PermissionsMessageReview);
}

int PermissionStatusModel::expectedErrorCode(PermissionKind kind)
{
  switch (kind) {
  case PermissionKind::WindowsFirewall:
    return static_cast<int>(PermissionErrorCode::WindowsFirewallBlocked);
  case PermissionKind::WindowsListeningPort:
    return static_cast<int>(PermissionErrorCode::WindowsPortUnavailable);
  case PermissionKind::MacLocalNetwork:
    return static_cast<int>(PermissionErrorCode::MacLocalNetworkDenied);
  case PermissionKind::MacAccessibility:
    return static_cast<int>(PermissionErrorCode::MacAccessibilityDenied);
  case PermissionKind::MacInputMonitoring:
    return static_cast<int>(PermissionErrorCode::MacInputMonitoringDenied);
  }
  return static_cast<int>(PermissionErrorCode::None);
}

int PermissionStatusModel::primaryAttentionRow() const
{
  int primaryRow = -1;
  int primaryRank = std::numeric_limits<int>::max();
  for (int row = 0; row < m_snapshot.entries.size(); ++row) {
    const auto rank = attentionRank(m_snapshot.entries.at(row).state);
    if (rank < primaryRank) {
      primaryRank = rank;
      primaryRow = row;
    }
  }
  return primaryRow;
}

} // namespace deskflow::relaydesk::model
