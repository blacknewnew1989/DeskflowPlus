/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsFirewallProbe.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <atomic>
#include <optional>

using namespace deskflow::relaydesk;

namespace {

const QDateTime kCheckedAt = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);

const PermissionProbeEntry &entry(const PermissionSnapshot &snapshot, PermissionKind kind)
{
  const auto found = std::find_if(snapshot.entries.cbegin(), snapshot.entries.cend(), [kind](const auto &candidate) {
    return candidate.kind == kind;
  });
  Q_ASSERT(found != snapshot.entries.cend());
  return *found;
}

} // namespace

class WindowsFirewallProbeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void publishesStablePermissionMappings_data();
  void publishesStablePermissionMappings();
  void refreshNeverBlocksCallingThread();
  void staleAsyncInspectionCannotReplaceNewestSnapshot();
  void opensOnlyActionableFirewallSettings();
  void inspectsRealCurrentProcessListener();
  void skipsFirewallInspectionWhenNoListenersAreExpected();
  void excludesStoppedInputCorePortFromActiveFileListenerProbe();
  void collectsOnlyNonZeroListeningPorts_data();
  void collectsOnlyNonZeroListeningPorts();
};

void WindowsFirewallProbeTests::publishesStablePermissionMappings_data()
{
  QTest::addColumn<WindowsFirewallRuleStatus>("firewallStatus");
  QTest::addColumn<PermissionState>("firewallState");
  QTest::addColumn<PermissionErrorCode>("firewallError");
  QTest::addColumn<WindowsListeningPortStatus>("portStatus");
  QTest::addColumn<PermissionState>("portState");
  QTest::addColumn<PermissionErrorCode>("portError");

  QTest::newRow("allowed-listening") << WindowsFirewallRuleStatus::Allowed << PermissionState::Granted
                                     << PermissionErrorCode::None << WindowsListeningPortStatus::Listening
                                     << PermissionState::Granted << PermissionErrorCode::None;
  QTest::newRow("blocked-not-listening")
      << WindowsFirewallRuleStatus::Blocked << PermissionState::Denied
      << PermissionErrorCode::WindowsFirewallBlocked << WindowsListeningPortStatus::NotListening
      << PermissionState::NeedsAction << PermissionErrorCode::WindowsPortUnavailable;
  QTest::newRow("missing-unavailable")
      << WindowsFirewallRuleStatus::MissingAllowRule << PermissionState::NeedsAction
      << PermissionErrorCode::WindowsFirewallBlocked << WindowsListeningPortStatus::Unavailable
      << PermissionState::Unknown << PermissionErrorCode::ProbeUnavailable;
  QTest::newRow("missing-listening") << WindowsFirewallRuleStatus::MissingAllowRule
                                     << PermissionState::NeedsAction
                                     << PermissionErrorCode::WindowsFirewallBlocked
                                     << WindowsListeningPortStatus::Listening << PermissionState::Granted
                                     << PermissionErrorCode::None;
  QTest::newRow("disabled-no-ports") << WindowsFirewallRuleStatus::NotRequired << PermissionState::NotRequired
                                    << PermissionErrorCode::None << WindowsListeningPortStatus::NotRequired
                                    << PermissionState::NotRequired << PermissionErrorCode::None;
  QTest::newRow("probe-unavailable") << WindowsFirewallRuleStatus::Unavailable << PermissionState::Unknown
                                     << PermissionErrorCode::ProbeUnavailable
                                     << WindowsListeningPortStatus::Unavailable << PermissionState::Unknown
                                     << PermissionErrorCode::ProbeUnavailable;
}

void WindowsFirewallProbeTests::publishesStablePermissionMappings()
{
  QFETCH(WindowsFirewallRuleStatus, firewallStatus);
  QFETCH(PermissionState, firewallState);
  QFETCH(PermissionErrorCode, firewallError);
  QFETCH(WindowsListeningPortStatus, portStatus);
  QFETCH(PermissionState, portState);
  QFETCH(PermissionErrorCode, portError);

  std::optional<WindowsFirewallProbeRequest> receivedRequest;
  WindowsFirewallProbe probe(
      [=, &receivedRequest](WindowsFirewallProbeRequest request) {
        receivedRequest = std::move(request);
        return WindowsFirewallInspection{
            .firewall = firewallStatus,
            .listeningPort = portStatus,
            .firewallDiagnostic = QStringLiteral("firewall-private-detail"),
            .listeningPortDiagnostic = QStringLiteral("listener-private-detail"),
        };
      },
      []() { return kCheckedAt; }
  );
  QSignalSpy changed(&probe, &WindowsFirewallProbe::snapshotChanged);

  probe.refresh({
      .executablePath = QStringLiteral(" C:/RelayDesk/RelayDesk.exe "),
      .expectedTcpPorts = {24801, 0, 24800, 24801},
  });

  QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 3000);
  QVERIFY(receivedRequest.has_value());
  QCOMPARE(receivedRequest->executablePath, QStringLiteral("C:/RelayDesk/RelayDesk.exe"));
  QCOMPARE(receivedRequest->expectedTcpPorts, QList<quint16>({24800, 24801}));
  const auto snapshot = probe.current();
  QCOMPARE(snapshot.platform, buildPermissionPlatform());
  QCOMPARE(snapshot.checkedAtUtc, kCheckedAt);
  const auto &firewall = entry(snapshot, PermissionKind::WindowsFirewall);
  QCOMPARE(firewall.state, firewallState);
  QCOMPARE(firewall.errorCode, firewallError);
  QCOMPARE(firewall.diagnostic, QStringLiteral("firewall-private-detail"));
  const auto &port = entry(snapshot, PermissionKind::WindowsListeningPort);
  QCOMPARE(port.state, portState);
  QCOMPARE(port.errorCode, portError);
  QCOMPARE(port.diagnostic, QStringLiteral("listener-private-detail"));
  QVERIFY(!port.canOpenSettings);
  QVERIFY(!probe.isRefreshing());
}

void WindowsFirewallProbeTests::refreshNeverBlocksCallingThread()
{
  QSemaphore entered;
  QSemaphore release;
  WindowsFirewallProbe probe([&](WindowsFirewallProbeRequest) {
    entered.release();
    release.acquire();
    return WindowsFirewallInspection{
        .firewall = WindowsFirewallRuleStatus::Allowed,
        .listeningPort = WindowsListeningPortStatus::Listening,
    };
  });
  QSignalSpy changed(&probe, &WindowsFirewallProbe::snapshotChanged);
  QElapsedTimer elapsed;
  elapsed.start();

  probe.refresh({.executablePath = QStringLiteral("C:/RelayDesk.exe"), .expectedTcpPorts = {24801}});

  QVERIFY2(elapsed.elapsed() < 100, "refresh performed blocking inspection on its caller");
  QVERIFY(probe.isRefreshing());
  QVERIFY(entered.tryAcquire(1, 3000));
  QCOMPARE(changed.count(), 0);
  release.release();
  QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 3000);
}

void WindowsFirewallProbeTests::staleAsyncInspectionCannotReplaceNewestSnapshot()
{
  QSemaphore firstEntered;
  QSemaphore releaseFirst;
  std::atomic_int calls = 0;
  WindowsFirewallProbe probe([&](WindowsFirewallProbeRequest) {
    const auto call = ++calls;
    if (call == 1) {
      firstEntered.release();
      releaseFirst.acquire();
      return WindowsFirewallInspection{
          .firewall = WindowsFirewallRuleStatus::Blocked,
          .listeningPort = WindowsListeningPortStatus::NotListening,
          .firewallDiagnostic = QStringLiteral("stale"),
      };
    }
    return WindowsFirewallInspection{
        .firewall = WindowsFirewallRuleStatus::Allowed,
        .listeningPort = WindowsListeningPortStatus::Listening,
        .firewallDiagnostic = QStringLiteral("newest"),
    };
  });
  QSignalSpy changed(&probe, &WindowsFirewallProbe::snapshotChanged);

  probe.refresh({.executablePath = QStringLiteral("C:/RelayDesk.exe"), .expectedTcpPorts = {24801}});
  QVERIFY(firstEntered.tryAcquire(1, 3000));
  probe.refresh({.executablePath = QStringLiteral("C:/RelayDesk.exe"), .expectedTcpPorts = {24801}});
  QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 3000);
  QCOMPARE(entry(probe.current(), PermissionKind::WindowsFirewall).diagnostic, QStringLiteral("newest"));
  releaseFirst.release();
  QTest::qWait(100);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(entry(probe.current(), PermissionKind::WindowsFirewall).state, PermissionState::Granted);
}

void WindowsFirewallProbeTests::opensOnlyActionableFirewallSettings()
{
  int calls = 0;
  PermissionOpenResult nextResult;
  WindowsFirewallProbe probe(
      {}, {},
      [&calls, &nextResult]() {
        ++calls;
        return nextResult;
      }
  );
  QSignalSpy failed(&probe, &WindowsFirewallProbe::settingsOpenFailed);

  QVERIFY(probe.openSystemSettings(PermissionKind::WindowsFirewall).ok());
  QCOMPARE(calls, 1);
  const auto notActionable = probe.openSystemSettings(PermissionKind::WindowsListeningPort);
  QCOMPARE(notActionable.error, PermissionOpenError::NotActionable);
  QCOMPARE(calls, 1);
  QCOMPARE(failed.count(), 1);
  QCOMPARE(failed.constFirst().at(1).value<PermissionOpenResult>(), notActionable);

  const auto unsupported = probe.openSystemSettings(PermissionKind::MacAccessibility);
  QCOMPARE(unsupported.error, PermissionOpenError::Unsupported);
  QCOMPARE(calls, 1);
  QCOMPARE(failed.count(), 2);

  nextResult = {
      .error = PermissionOpenError::OpenFailed,
      .diagnostic = QStringLiteral("simulated settings launcher failure"),
  };
  QCOMPARE(probe.openSystemSettings(PermissionKind::WindowsFirewall), nextResult);
  QCOMPARE(calls, 2);
  QCOMPARE(failed.count(), 3);
}

void WindowsFirewallProbeTests::inspectsRealCurrentProcessListener()
{
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  const auto inspection = WindowsFirewallProbe::inspectCurrentSystem({
      .executablePath = QCoreApplication::applicationFilePath(),
      .expectedTcpPorts = {server.serverPort()},
  });

#if defined(Q_OS_WIN)
  QCOMPARE(inspection.listeningPort, WindowsListeningPortStatus::Listening);
  QVERIFY(!inspection.listeningPortDiagnostic.isEmpty());
  QVERIFY(inspection.firewall != WindowsFirewallRuleStatus::Unavailable);
#else
  QCOMPARE(inspection.listeningPort, WindowsListeningPortStatus::Unavailable);
  QCOMPARE(inspection.firewall, WindowsFirewallRuleStatus::Unavailable);
#endif
}

void WindowsFirewallProbeTests::skipsFirewallInspectionWhenNoListenersAreExpected()
{
  const WindowsFirewallProbeRequest request{
      .executablePath = QCoreApplication::applicationFilePath(),
      .processId = static_cast<quint32>(QCoreApplication::applicationPid()),
  };
  const auto inspection = WindowsFirewallProbe::inspectCurrentSystem(request);

#if defined(Q_OS_WIN)
  QCOMPARE(inspection.firewall, WindowsFirewallRuleStatus::NotRequired);
  QCOMPARE(inspection.listeningPort, WindowsListeningPortStatus::NotRequired);

  WindowsFirewallProbe probe;
  QSignalSpy changed(&probe, &WindowsFirewallProbe::snapshotChanged);
  probe.refresh(request);
  QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 3000);
  const auto snapshot = probe.current();
  const auto &firewall = entry(snapshot, PermissionKind::WindowsFirewall);
  const auto &listener = entry(snapshot, PermissionKind::WindowsListeningPort);
  QCOMPARE(firewall.state, PermissionState::NotRequired);
  QCOMPARE(firewall.errorCode, PermissionErrorCode::None);
  QCOMPARE(listener.state, PermissionState::NotRequired);
  QCOMPARE(listener.errorCode, PermissionErrorCode::None);
#else
  QCOMPARE(inspection.firewall, WindowsFirewallRuleStatus::Unavailable);
  QCOMPARE(inspection.listeningPort, WindowsListeningPortStatus::Unavailable);
#endif
}

void WindowsFirewallProbeTests::excludesStoppedInputCorePortFromActiveFileListenerProbe()
{
  const auto request = WindowsFirewallProbe::requestForListeningServices(
      QStringLiteral("C:/RelayDesk/RelayDesk.exe"), 24800, false, 24801, 4242
  );

  QCOMPARE(request.executablePath, QStringLiteral("C:/RelayDesk/RelayDesk.exe"));
  QCOMPARE(request.processId, quint32{4242});
  QCOMPARE(request.expectedTcpPorts, QList<quint16>({24801}));

  const auto activeInputRequest = WindowsFirewallProbe::requestForListeningServices(
      QStringLiteral("C:/RelayDesk/RelayDesk.exe"), 24800, true, 24801, 4242
  );
  QCOMPARE(activeInputRequest.expectedTcpPorts, QList<quint16>({24800, 24801}));
}

void WindowsFirewallProbeTests::collectsOnlyNonZeroListeningPorts_data()
{
  QTest::addColumn<bool>("inputIsListening");
  QTest::addColumn<quint16>("inputPort");
  QTest::addColumn<quint16>("filePort");
  QTest::addColumn<QList<quint16>>("expectedPorts");

  QTest::newRow("core-stopped-file-listening") << false << quint16{24800} << quint16{24801}
                                                << QList<quint16>({24801});
  QTest::newRow("core-client-file-listening") << false << quint16{24800} << quint16{24801}
                                               << QList<quint16>({24801});
  QTest::newRow("core-listening-file-listening") << true << quint16{24800} << quint16{24801}
                                                  << QList<quint16>({24800, 24801});
  QTest::newRow("core-listening-file-failed") << true << quint16{24800} << quint16{0}
                                               << QList<quint16>({24800});
  QTest::newRow("core-stopped-file-failed") << false << quint16{24800} << quint16{0}
                                             << QList<quint16>();
  QTest::newRow("same-listener-port") << true << quint16{24801} << quint16{24801}
                                       << QList<quint16>({24801});
  QTest::newRow("invalid-listener-ports") << true << quint16{0} << quint16{0} << QList<quint16>();
}

void WindowsFirewallProbeTests::collectsOnlyNonZeroListeningPorts()
{
  QFETCH(bool, inputIsListening);
  QFETCH(quint16, inputPort);
  QFETCH(quint16, filePort);
  QFETCH(QList<quint16>, expectedPorts);

  const auto request = WindowsFirewallProbe::requestForListeningServices(
      QStringLiteral("C:/RelayDesk/RelayDesk.exe"), inputPort, inputIsListening, filePort, 4242
  );
  QCOMPARE(request.expectedTcpPorts, expectedPorts);
}

QTEST_GUILESS_MAIN(WindowsFirewallProbeTests)

#include "WindowsFirewallProbeTests.moc"
