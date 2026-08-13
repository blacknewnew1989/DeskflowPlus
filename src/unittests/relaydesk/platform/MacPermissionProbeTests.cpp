/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacPermissionProbe.h"

#include "relaydesk/platform/MacLocalNetworkStatus.h"

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
  void onlyRoutesMacSettingsKinds();
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
  QVERIFY(!offline.canOpenSettings);

  const auto failed = macLocalNetworkEntry(MacLocalNetworkProbeState::Failed, false, 1, 22);
  QCOMPARE(failed.state, PermissionState::Unknown);
  QCOMPARE(failed.errorCode, PermissionErrorCode::ProbeUnavailable);
}

QTEST_GUILESS_MAIN(MacPermissionProbeTests)

#include "MacPermissionProbeTests.moc"
