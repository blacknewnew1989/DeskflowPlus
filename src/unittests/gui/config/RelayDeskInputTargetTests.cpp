/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/config/RelayDeskInputTarget.h"
#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using deskflow::gui::RelayDeskInputTarget;
using deskflow::gui::RelayDeskInputTargetResult;
using deskflow::gui::syncRelayDeskClientTarget;
using namespace deskflow::relaydesk;

class RelayDeskInputTargetTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void trustedPeerClaimsEmptyClientTarget();
  void manualHostAndPortAreNeverOverwritten();
  void emptyHostWithCustomPortIsPreserved();
  void endpointUpdateChangesHostOnceWithoutChangingPort();
  void automaticDiscoveryDoesNotSwitchManagedPeerButExplicitSelectionCan();

private:
  [[nodiscard]] DeviceSnapshot peer() const;
  [[nodiscard]] DeviceInfo endpoint(const DeviceSnapshot &peer, quint16 port) const;

  QTemporaryDir m_directory;
};

DeviceSnapshot RelayDeskInputTargetTests::peer() const
{
  return {
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("relay-host"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = DevicePresence::Online,
      .trusted = true,
      .addresses = {QHostAddress(QStringLiteral("192.168.1.20"))},
      .capabilities = {.input = true},
  };
}

DeviceInfo RelayDeskInputTargetTests::endpoint(const DeviceSnapshot &peer, quint16 port) const
{
  return {
      .deviceId = peer.id,
      .displayName = peer.displayName,
      .platform = peer.platform,
      .architecture = peer.architecture,
      .inputPort = port,
      .capabilities = peer.capabilities,
  };
}

void RelayDeskInputTargetTests::trustedPeerClaimsEmptyClientTarget()
{
  QVERIFY(m_directory.isValid());
  QSettings settings(m_directory.filePath(QStringLiteral("target.conf")), QSettings::IniFormat);
  const auto snapshot = peer();
  RelayDeskInputTarget target;
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24800), {}, 24800, false, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.20"));
  QCOMPARE(target.port, quint16(24800));
}

void RelayDeskInputTargetTests::manualHostAndPortAreNeverOverwritten()
{
  QSettings settings(m_directory.filePath(QStringLiteral("manual.conf")), QSettings::IniFormat);
  const auto snapshot = peer();
  QCOMPARE(
      syncRelayDeskClientTarget(
          settings, snapshot, endpoint(snapshot, 25000), QStringLiteral("manual-host"), 25000, false, nullptr
      ),
      RelayDeskInputTargetResult::ManualConfigurationPreserved
  );
}

void RelayDeskInputTargetTests::emptyHostWithCustomPortIsPreserved()
{
  QSettings settings(m_directory.filePath(QStringLiteral("custom-port.conf")), QSettings::IniFormat);
  const auto snapshot = peer();
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24900), {}, 25000, false, nullptr),
      RelayDeskInputTargetResult::PortMismatchPreserved
  );
}

void RelayDeskInputTargetTests::endpointUpdateChangesHostOnceWithoutChangingPort()
{
  QSettings settings(m_directory.filePath(QStringLiteral("endpoint-refresh.conf")), QSettings::IniFormat);
  auto snapshot = peer();
  RelayDeskInputTarget target;
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24800), {}, 24800, false, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24800), target.host, target.port, false, &target),
      RelayDeskInputTargetResult::AlreadyCurrent
  );
  snapshot.addresses = {QHostAddress(QStringLiteral("192.168.1.21"))};
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24800), target.host, target.port, false, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.21"));
  QCOMPARE(target.port, quint16(24800));
  QCOMPARE(
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24800), target.host, target.port, false, &target),
      RelayDeskInputTargetResult::AlreadyCurrent
  );
}

void RelayDeskInputTargetTests::automaticDiscoveryDoesNotSwitchManagedPeerButExplicitSelectionCan()
{
  QSettings settings(m_directory.filePath(QStringLiteral("managed.conf")), QSettings::IniFormat);
  auto first = peer();
  RelayDeskInputTarget target;
  QCOMPARE(
      syncRelayDeskClientTarget(settings, first, endpoint(first, 24800), {}, 24800, false, &target),
      RelayDeskInputTargetResult::Updated
  );
  const auto other = peer();
  QCOMPARE(
      syncRelayDeskClientTarget(settings, other, endpoint(other, 24800), target.host, target.port, false, nullptr),
      RelayDeskInputTargetResult::AnotherManagedDevicePreserved
  );
  QCOMPARE(
      syncRelayDeskClientTarget(settings, other, endpoint(other, 24800), target.host, target.port, true, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.20"));
  settings.beginGroup(QStringLiteral("relaydesk/inputTarget"));
  QCOMPARE(settings.value(QStringLiteral("deviceId")).toString(), other.id.toString());
  settings.endGroup();
  first.addresses = {QHostAddress(QStringLiteral("192.168.1.21"))};
  QCOMPARE(
      syncRelayDeskClientTarget(settings, first, endpoint(first, 24800), target.host, target.port, false, &target),
      RelayDeskInputTargetResult::AnotherManagedDevicePreserved
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.20"));
  QCOMPARE(target.port, quint16(24800));
}

QTEST_MAIN(RelayDeskInputTargetTests)

#include "RelayDeskInputTargetTests.moc"
