/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionProbe.h"

#include "relaydesk/platform/MacLocalNetworkStatus.h"
#include "relaydesk/platform/MacPermissionSettings.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace deskflow::relaydesk;

namespace {

PermissionProbeEntry entry(PermissionKind kind, PermissionState state, PermissionErrorCode error)
{
  return {
      .kind = kind,
      .state = state,
      .errorCode = error,
      .canOpenSettings = state == PermissionState::Denied,
  };
}

class FakeBackend final : public IMacPermissionBackend
{
public:
  PermissionProbeEntry local = entry(
      PermissionKind::MacLocalNetwork, PermissionState::Unknown, PermissionErrorCode::None
  );
  PermissionProbeEntry accessibilityValue = entry(
      PermissionKind::MacAccessibility, PermissionState::Denied, PermissionErrorCode::MacAccessibilityDenied
  );
  PermissionProbeEntry inputValue = entry(
      PermissionKind::MacInputMonitoring, PermissionState::Granted, PermissionErrorCode::None
  );
  int refreshCount = 0;
  bool resetLocalWhenRefreshed = false;
  QList<PermissionKind> opened;
  PermissionOpenResult openResult;

  [[nodiscard]] PermissionProbeEntry localNetwork() const override
  {
    return local;
  }

  [[nodiscard]] PermissionProbeEntry accessibility() const override
  {
    return accessibilityValue;
  }

  [[nodiscard]] PermissionProbeEntry inputMonitoring() const override
  {
    return inputValue;
  }

  void refreshLocalNetwork() override
  {
    ++refreshCount;
    if (resetLocalWhenRefreshed) {
      local = entry(PermissionKind::MacLocalNetwork, PermissionState::Unknown, PermissionErrorCode::ProbeUnavailable);
      local.canOpenSettings = true;
    }
  }

  [[nodiscard]] PermissionOpenResult openSystemSettings(PermissionKind kind) override
  {
    opened.append(kind);
    return openResult;
  }

  void publishLocal(PermissionProbeEntry value)
  {
    Q_EMIT localNetworkChanged(std::move(value));
  }
};

} // namespace

class MacPermissionProbeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void publishesTypedSnapshotWithoutInventingGrants();
  void appliesAsynchronousLocalNetworkResult();
  void coalescesForegroundRefreshAndRechecksEveryPermission();
  void treatsInputMonitoringAsNotRequiredWhenAccessibilityIsGranted();
  void onlyRoutesMacSettingsKinds();
  void fallsBackToPrivacySettingsWhenSpecificDeepLinkFails();
  void mapsLocalNetworkNativeStatesConservatively();
};

void MacPermissionProbeTests::publishesTypedSnapshotWithoutInventingGrants()
{
  auto backend = std::make_unique<FakeBackend>();
  auto *fake = backend.get();
  const auto checkedAt = QDateTime::fromString(QStringLiteral("2026-08-13T01:02:03Z"), Qt::ISODate);
  MacPermissionProbe probe(std::move(backend), [checkedAt] { return checkedAt; });

  const auto snapshot = probe.current();
  QCOMPARE(snapshot.platform, PermissionPlatform::MacOS);
  QCOMPARE(snapshot.checkedAtUtc, checkedAt);
  QCOMPARE(snapshot.entries.size(), 3);
  QCOMPARE(snapshot.entries.at(0).kind, PermissionKind::MacLocalNetwork);
  QCOMPARE(snapshot.entries.at(0).state, PermissionState::Unknown);
  QCOMPARE(snapshot.entries.at(1).errorCode, PermissionErrorCode::MacAccessibilityDenied);
  QCOMPARE(snapshot.entries.at(2).state, PermissionState::Granted);
  QCOMPARE(fake->refreshCount, 1);
}

void MacPermissionProbeTests::appliesAsynchronousLocalNetworkResult()
{
  auto backend = std::make_unique<FakeBackend>();
  auto *fake = backend.get();
  MacPermissionProbe probe(std::move(backend));
  QSignalSpy changed(&probe, &MacPermissionProbe::snapshotChanged);

  fake->publishLocal(entry(
      PermissionKind::MacLocalNetwork, PermissionState::Denied, PermissionErrorCode::MacLocalNetworkDenied
  ));

  QCOMPARE(changed.count(), 1);
  const auto snapshot = changed.takeFirst().constFirst().value<PermissionSnapshot>();
  QCOMPARE(snapshot.entries.constFirst().state, PermissionState::Denied);
  QCOMPARE(snapshot.entries.constFirst().errorCode, PermissionErrorCode::MacLocalNetworkDenied);
  QCOMPARE(snapshot.entries.at(1).kind, PermissionKind::MacAccessibility);
}

void MacPermissionProbeTests::coalescesForegroundRefreshAndRechecksEveryPermission()
{
  QCOMPARE(kMacPermissionRefreshDebounceMs, 150);
  auto backend = std::make_unique<FakeBackend>();
  auto *fake = backend.get();
  MacPermissionProbe probe(std::move(backend));
  QSignalSpy changed(&probe, &MacPermissionProbe::snapshotChanged);

  fake->local = entry(PermissionKind::MacLocalNetwork, PermissionState::Granted, PermissionErrorCode::None);
  fake->accessibilityValue =
      entry(PermissionKind::MacAccessibility, PermissionState::Granted, PermissionErrorCode::None);
  fake->inputValue = entry(
      PermissionKind::MacInputMonitoring, PermissionState::NeedsAction, PermissionErrorCode::MacInputMonitoringDenied
  );
  fake->resetLocalWhenRefreshed = true;

  probe.refresh();
  probe.refresh();
  probe.refresh();

  QCOMPARE(fake->refreshCount, 1);
  QTRY_COMPARE_WITH_TIMEOUT(fake->refreshCount, 2, 1000);
  QCOMPARE(changed.count(), 1);
  const auto snapshot = probe.current();
  QCOMPARE(snapshot.entries.at(0).state, PermissionState::Unknown);
  QCOMPARE(snapshot.entries.at(1).state, PermissionState::Granted);
  QCOMPARE(snapshot.entries.at(2).state, PermissionState::NotRequired);
}

void MacPermissionProbeTests::treatsInputMonitoringAsNotRequiredWhenAccessibilityIsGranted()
{
  auto backend = std::make_unique<FakeBackend>();
  auto *fake = backend.get();
  fake->accessibilityValue =
      entry(PermissionKind::MacAccessibility, PermissionState::Granted, PermissionErrorCode::None);
  fake->inputValue = entry(
      PermissionKind::MacInputMonitoring, PermissionState::Denied, PermissionErrorCode::MacInputMonitoringDenied
  );

  MacPermissionProbe probe(std::move(backend));

  const auto snapshot = probe.current();
  QCOMPARE(snapshot.entries.at(1).state, PermissionState::Granted);
  QCOMPARE(snapshot.entries.at(2).kind, PermissionKind::MacInputMonitoring);
  QCOMPARE(snapshot.entries.at(2).state, PermissionState::NotRequired);
  QCOMPARE(snapshot.entries.at(2).errorCode, PermissionErrorCode::None);
  QVERIFY(!snapshot.entries.at(2).canOpenSettings);
}

void MacPermissionProbeTests::onlyRoutesMacSettingsKinds()
{
  auto backend = std::make_unique<FakeBackend>();
  auto *fake = backend.get();
  MacPermissionProbe probe(std::move(backend));

  QVERIFY(probe.openSystemSettings(PermissionKind::MacAccessibility).ok());
  QVERIFY(probe.openSystemSettings(PermissionKind::MacInputMonitoring).ok());
  QVERIFY(probe.openSystemSettings(PermissionKind::MacLocalNetwork).ok());
  const auto unsupported = probe.openSystemSettings(PermissionKind::WindowsFirewall);
  QCOMPARE(unsupported.error, PermissionOpenError::Unsupported);
  QCOMPARE(fake->opened.size(), 3);

  fake->openResult = {
      .error = PermissionOpenError::OpenFailed,
      .diagnostic = QStringLiteral("simulated native open failure"),
  };
  const auto failed = probe.openSystemSettings(PermissionKind::MacAccessibility);
  QCOMPARE(failed.error, PermissionOpenError::OpenFailed);
  QCOMPARE(failed.diagnostic, fake->openResult.diagnostic);
}

void MacPermissionProbeTests::fallsBackToPrivacySettingsWhenSpecificDeepLinkFails()
{
  QStringList openedUrls;
  const auto result = openMacPermissionSettings(PermissionKind::MacInputMonitoring, [&openedUrls](const QString &url) {
    openedUrls.append(url);
    return openedUrls.size() == 2;
  });
  QVERIFY(result.ok());
  QCOMPARE(openedUrls.size(), 2);
  QVERIFY(openedUrls.at(0).endsWith(QStringLiteral("Privacy_ListenEvent")));
  QCOMPARE(openedUrls.at(1), QStringLiteral("x-apple.systempreferences:com.apple.preference.security"));

  const auto failed = openMacPermissionSettings(PermissionKind::MacLocalNetwork, [](const QString &) { return false; });
  QCOMPARE(failed.error, PermissionOpenError::OpenFailed);
  QVERIFY(failed.diagnostic.contains(QStringLiteral("fallback")));

  int unsupportedOpenAttempts = 0;
  const auto unsupported =
      openMacPermissionSettings(PermissionKind::WindowsFirewall, [&unsupportedOpenAttempts](const QString &) {
        ++unsupportedOpenAttempts;
        return true;
      });
  QCOMPARE(unsupported.error, PermissionOpenError::Unsupported);
  QCOMPARE(unsupportedOpenAttempts, 0);
}

void MacPermissionProbeTests::mapsLocalNetworkNativeStatesConservatively()
{
  const auto ready = macLocalNetworkEntry(MacLocalNetworkProbeState::Ready, false);
  QCOMPARE(ready.state, PermissionState::Granted);
  QCOMPARE(ready.errorCode, PermissionErrorCode::None);

  const auto denied = macLocalNetworkEntry(MacLocalNetworkProbeState::Waiting, true, 2, -65570);
  QCOMPARE(denied.state, PermissionState::Denied);
  QCOMPARE(denied.errorCode, PermissionErrorCode::MacLocalNetworkDenied);
  QVERIFY(denied.canOpenSettings);
  QVERIFY(denied.diagnostic.contains(QStringLiteral("-65570")));

  const auto offline = macLocalNetworkEntry(MacLocalNetworkProbeState::Waiting, false, 1, 50);
  QCOMPARE(offline.state, PermissionState::Unknown);
  QCOMPARE(offline.errorCode, PermissionErrorCode::ProbeUnavailable);
  QVERIFY(offline.canOpenSettings);

  const auto failed = macLocalNetworkEntry(MacLocalNetworkProbeState::Failed, false, 1, 22);
  QCOMPARE(failed.state, PermissionState::Unknown);
  QCOMPARE(failed.errorCode, PermissionErrorCode::ProbeUnavailable);
  QVERIFY(failed.canOpenSettings);
}

QTEST_GUILESS_MAIN(MacPermissionProbeTests)

#include "MacPermissionProbeTests.moc"
