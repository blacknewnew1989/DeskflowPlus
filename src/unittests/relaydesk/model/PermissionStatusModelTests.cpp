/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/PermissionStatusModel.h"

#include <QSignalSpy>
#include <QTest>

#include <utility>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {

PermissionProbeEntry permission(
    PermissionKind kind, PermissionState state, PermissionErrorCode error = PermissionErrorCode::None,
    bool canOpenSettings = false, QString diagnostic = {}
)
{
  return {
      .kind = kind,
      .state = state,
      .errorCode = static_cast<int>(error),
      .canOpenSettings = canOpenSettings,
      .diagnostic = std::move(diagnostic),
  };
}

int rowFor(const PermissionStatusModel &model, PermissionKind kind)
{
  for (int row = 0; row < model.rowCount(); ++row) {
    if (model.index(row, 0).data(PermissionStatusModel::KindRole).toInt() == static_cast<int>(kind))
      return row;
  }
  return -1;
}

} // namespace

class PermissionStatusModelTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void unknownIsNeverReportedAsGranted();
  void appliesDeniedAndGrantedUpdatesWithSignals();
  void filtersEntriesForConfiguredPlatform();
  void mapsCodesWithoutExposingDiagnostic();
  void requestsOnlyAvailableSettingsEntry();
};

void PermissionStatusModelTests::unknownIsNeverReportedAsGranted()
{
  PermissionStatusModel model(PermissionPlatform::Windows);
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.bannerVisible());
  QCOMPARE(model.bannerTitle(), QStringLiteral("Permission status not checked"));
  QVERIFY(!model.canOpenPrimarySettings());

  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    QCOMPARE(index.data(PermissionStatusModel::StateRole).toInt(), static_cast<int>(PermissionState::Unknown));
    QCOMPARE(index.data(PermissionStatusModel::StatusTextRole).toString(), QStringLiteral("Not checked"));
    QVERIFY(index.data(PermissionStatusModel::NeedsAttentionRole).toBool());
  }
}

void PermissionStatusModelTests::appliesDeniedAndGrantedUpdatesWithSignals()
{
  PermissionStatusModel model(PermissionPlatform::Windows);
  QSignalSpy changed(&model, &PermissionStatusModel::snapshotChanged);
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  const PermissionSnapshot denied{
      .platform = PermissionPlatform::Windows,
      .entries = {
          permission(
              PermissionKind::WindowsFirewall, PermissionState::Denied, PermissionErrorCode::WindowsFirewallBlocked,
              true
          ),
          permission(PermissionKind::WindowsListeningPort, PermissionState::Granted),
      },
  };

  QVERIFY(model.setSnapshot(denied));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(reset.count(), 1);
  QCOMPARE(model.bannerTitle(), QStringLiteral("Permission needed"));
  QCOMPARE(model.bannerMessage(), QStringLiteral("Allow RelayDesk through Windows Firewall on private networks."));
  QVERIFY(model.canOpenPrimarySettings());
  QVERIFY(model.setSnapshot(denied));
  QCOMPARE(changed.count(), 1);

  const PermissionSnapshot clear{
      .platform = PermissionPlatform::Windows,
      .entries = {
          permission(PermissionKind::WindowsFirewall, PermissionState::Granted),
          permission(PermissionKind::WindowsListeningPort, PermissionState::NotRequired),
      },
  };
  QVERIFY(model.setSnapshot(clear));
  QCOMPARE(changed.count(), 2);
  QVERIFY(!model.bannerVisible());
  const auto firewall = model.index(rowFor(model, PermissionKind::WindowsFirewall), 0);
  const auto port = model.index(rowFor(model, PermissionKind::WindowsListeningPort), 0);
  QCOMPARE(firewall.data(PermissionStatusModel::StatusTextRole).toString(), QStringLiteral("Allowed"));
  QCOMPARE(port.data(PermissionStatusModel::StatusTextRole).toString(), QStringLiteral("Not required"));
}

void PermissionStatusModelTests::filtersEntriesForConfiguredPlatform()
{
  PermissionStatusModel windows(PermissionPlatform::Windows);
  QSignalSpy windowsChanged(&windows, &PermissionStatusModel::snapshotChanged);
  QVERIFY(windows.setSnapshot({
      .platform = PermissionPlatform::Windows,
      .entries = {
          permission(PermissionKind::MacAccessibility, PermissionState::Denied),
          permission(PermissionKind::WindowsFirewall, PermissionState::Granted),
          permission(PermissionKind::WindowsListeningPort, PermissionState::Granted),
      },
  }));
  QCOMPARE(windows.rowCount(), 2);
  QCOMPARE(rowFor(windows, PermissionKind::MacAccessibility), -1);
  QVERIFY(!windows.setSnapshot({.platform = PermissionPlatform::MacOS}));
  QCOMPARE(windowsChanged.count(), 1);

  PermissionStatusModel mac(PermissionPlatform::MacOS);
  QVERIFY(mac.setSnapshot({
      .platform = PermissionPlatform::MacOS,
      .entries = {
          permission(PermissionKind::WindowsFirewall, PermissionState::Denied),
          permission(PermissionKind::MacLocalNetwork, PermissionState::Granted),
          permission(PermissionKind::MacAccessibility, PermissionState::Granted),
          permission(PermissionKind::MacInputMonitoring, PermissionState::NotRequired),
      },
  }));
  QCOMPARE(mac.rowCount(), 3);
  QCOMPARE(rowFor(mac, PermissionKind::WindowsFirewall), -1);
  QVERIFY(!mac.bannerVisible());

  PermissionStatusModel unsupported(PermissionPlatform::Other);
  QCOMPARE(unsupported.rowCount(), 0);
  QVERIFY(!unsupported.bannerVisible());
}

void PermissionStatusModelTests::mapsCodesWithoutExposingDiagnostic()
{
  PermissionStatusModel model(PermissionPlatform::MacOS);
  const auto hostile = QStringLiteral("<script>remote diagnostic secret</script>");
  QVERIFY(model.setSnapshot({
      .platform = PermissionPlatform::MacOS,
      .entries = {
          permission(
              PermissionKind::MacLocalNetwork, PermissionState::Denied, PermissionErrorCode::MacLocalNetworkDenied,
              true, hostile
          ),
          permission(PermissionKind::MacAccessibility, PermissionState::Granted),
          permission(PermissionKind::MacInputMonitoring, PermissionState::NotRequired),
      },
  }));
  QCOMPARE(model.bannerMessage(), QStringLiteral("Allow Local Network access so RelayDesk can find nearby devices."));
  for (int row = 0; row < model.rowCount(); ++row) {
    for (const auto role : model.roleNames().keys())
      QVERIFY(!model.index(row, 0).data(role).toString().contains(QStringLiteral("remote diagnostic")));
  }
  QVERIFY(!model.roleNames().values().contains(QByteArrayLiteral("diagnostic")));

  const PermissionSnapshot unknownCode{
      .platform = PermissionPlatform::MacOS,
      .entries = {
          {
              .kind = PermissionKind::MacLocalNetwork,
              .state = PermissionState::Denied,
              .errorCode = 999999,
              .canOpenSettings = true,
              .diagnostic = QStringLiteral("raw socket failure from peer"),
          },
          permission(PermissionKind::MacAccessibility, PermissionState::Granted),
          permission(PermissionKind::MacInputMonitoring, PermissionState::NotRequired),
      },
  };
  QVERIFY(model.setSnapshot(unknownCode));
  QCOMPARE(
      model.bannerMessage(), QStringLiteral("Review this system setting to keep local device connections working.")
  );
  QVERIFY(!model.bannerMessage().contains(QStringLiteral("socket")));
}

void PermissionStatusModelTests::requestsOnlyAvailableSettingsEntry()
{
  qRegisterMetaType<PermissionKind>();
  PermissionStatusModel model(PermissionPlatform::Windows);
  QSignalSpy requested(&model, &PermissionStatusModel::openSettingsRequested);
  QVERIFY(model.setSnapshot({
      .platform = PermissionPlatform::Windows,
      .entries = {
          permission(
              PermissionKind::WindowsFirewall, PermissionState::NeedsAction, PermissionErrorCode::WindowsFirewallBlocked
          ),
          permission(PermissionKind::WindowsListeningPort, PermissionState::Granted),
      },
  }));
  QVERIFY(!model.requestPrimarySettings());
  QCOMPARE(requested.count(), 0);

  const PermissionSnapshot actionable{
      .platform = PermissionPlatform::Windows,
      .entries = {
          permission(
              PermissionKind::WindowsFirewall, PermissionState::NeedsAction,
              PermissionErrorCode::WindowsFirewallBlocked, true
          ),
          permission(PermissionKind::WindowsListeningPort, PermissionState::Granted),
      },
  };
  QVERIFY(model.setSnapshot(actionable));
  QVERIFY(model.requestPrimarySettings());
  QCOMPARE(requested.count(), 1);
  QCOMPARE(requested.takeFirst().constFirst().value<PermissionKind>(), PermissionKind::WindowsFirewall);
  QVERIFY(!model.requestOpenSettings(rowFor(model, PermissionKind::WindowsListeningPort)));
}

QTEST_GUILESS_MAIN(PermissionStatusModelTests)

#include "PermissionStatusModelTests.moc"
