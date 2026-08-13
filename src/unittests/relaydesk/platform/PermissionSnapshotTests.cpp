/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/IPlatformPermissions.h"
#include "relaydesk/platform/MacPermissionProbe.h"
#include "relaydesk/platform/WindowsFirewallProbe.h"

#include <QMetaType>
#include <QTest>

#include <concepts>
#include <type_traits>

using namespace deskflow::relaydesk;

namespace {

using CurrentMethod = PermissionSnapshot (IPlatformPermissions::*)() const;
using OpenMethod = PermissionOpenResult (IPlatformPermissions::*)(PermissionKind);
using MacCurrentMethod = PermissionSnapshot (MacPermissionProbe::*)() const;
using MacOpenMethod = PermissionOpenResult (MacPermissionProbe::*)(PermissionKind);
using WindowsCurrentMethod = PermissionSnapshot (WindowsFirewallProbe::*)() const;
using WindowsOpenMethod = PermissionOpenResult (WindowsFirewallProbe::*)(PermissionKind);
using WindowsOpenFailedSignal = void (WindowsFirewallProbe::*)(PermissionKind, PermissionOpenResult);

static_assert(std::is_abstract_v<IPlatformPermissions>);
static_assert(std::derived_from<MacPermissionProbe, IPlatformPermissions>);
static_assert(std::derived_from<WindowsFirewallProbe, IPlatformPermissions>);
static_assert(std::is_same_v<decltype(&IPlatformPermissions::current), CurrentMethod>);
static_assert(std::is_same_v<decltype(&IPlatformPermissions::openSystemSettings), OpenMethod>);
static_assert(std::is_same_v<decltype(&MacPermissionProbe::current), MacCurrentMethod>);
static_assert(std::is_same_v<decltype(&MacPermissionProbe::openSystemSettings), MacOpenMethod>);
static_assert(std::is_same_v<decltype(&WindowsFirewallProbe::current), WindowsCurrentMethod>);
static_assert(std::is_same_v<decltype(&WindowsFirewallProbe::openSystemSettings), WindowsOpenMethod>);
static_assert(std::is_same_v<decltype(&WindowsFirewallProbe::settingsOpenFailed), WindowsOpenFailedSignal>);
static_assert(std::is_same_v<decltype(PermissionProbeEntry::errorCode), PermissionErrorCode>);
static_assert(static_cast<int>(PermissionErrorCode::None) == 0);
static_assert(static_cast<int>(PermissionErrorCode::ProbeUnavailable) == 4000);
static_assert(static_cast<int>(PermissionErrorCode::WindowsFirewallBlocked) == 4101);
static_assert(static_cast<int>(PermissionErrorCode::WindowsPortUnavailable) == 4102);
static_assert(static_cast<int>(PermissionErrorCode::MacLocalNetworkDenied) == 4201);
static_assert(static_cast<int>(PermissionErrorCode::MacAccessibilityDenied) == 4202);
static_assert(static_cast<int>(PermissionErrorCode::MacInputMonitoringDenied) == 4203);
static_assert(static_cast<int>(PermissionOpenError::None) == 0);
static_assert(static_cast<int>(PermissionOpenError::Unsupported) == 1);
static_assert(static_cast<int>(PermissionOpenError::NotActionable) == 2);
static_assert(static_cast<int>(PermissionOpenError::OpenFailed) == 3);

} // namespace

class PermissionSnapshotTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void isCopyableAndKeepsProbeOnlyDiagnostic();
  void registersSharedValueTypes();
};

void PermissionSnapshotTests::isCopyableAndKeepsProbeOnlyDiagnostic()
{
  const PermissionSnapshot snapshot{
      .platform = PermissionPlatform::MacOS,
      .entries = {{
          .kind = PermissionKind::MacAccessibility,
          .state = PermissionState::Denied,
          .errorCode = PermissionErrorCode::MacAccessibilityDenied,
          .canOpenSettings = true,
          .diagnostic = QStringLiteral("private platform diagnostic"),
      }},
      .checkedAtUtc = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate),
  };

  const auto copy = snapshot;
  QCOMPARE(copy, snapshot);
  QCOMPARE(copy.entries.constFirst().diagnostic, QStringLiteral("private platform diagnostic"));
}

void PermissionSnapshotTests::registersSharedValueTypes()
{
  QVERIFY(QMetaType::fromType<PermissionKind>().isValid());
  QVERIFY(QMetaType::fromType<PermissionState>().isValid());
  QVERIFY(QMetaType::fromType<PermissionErrorCode>().isValid());
  QVERIFY(QMetaType::fromType<PermissionProbeEntry>().isValid());
  QVERIFY(QMetaType::fromType<PermissionSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<PermissionOpenError>().isValid());
  QVERIFY(QMetaType::fromType<PermissionOpenResult>().isValid());
}

QTEST_GUILESS_MAIN(PermissionSnapshotTests)

#include "PermissionSnapshotTests.moc"
