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
  void onlyTheManagedPeerCanRefreshItsEndpoint();

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
      syncRelayDeskClientTarget(settings, snapshot, endpoint(snapshot, 24900), {}, 24800, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.20"));
  QCOMPARE(target.port, quint16(24900));
}

void RelayDeskInputTargetTests::manualHostAndPortAreNeverOverwritten()
{
  QSettings settings(m_directory.filePath(QStringLiteral("manual.conf")), QSettings::IniFormat);
  const auto snapshot = peer();
  QCOMPARE(
      syncRelayDeskClientTarget(
          settings, snapshot, endpoint(snapshot, 24900), QStringLiteral("manual-host"), 25000, nullptr
      ),
      RelayDeskInputTargetResult::ManualConfigurationPreserved
  );
}

void RelayDeskInputTargetTests::onlyTheManagedPeerCanRefreshItsEndpoint()
{
  QSettings settings(m_directory.filePath(QStringLiteral("managed.conf")), QSettings::IniFormat);
  const auto first = peer();
  RelayDeskInputTarget target;
  QCOMPARE(
      syncRelayDeskClientTarget(settings, first, endpoint(first, 24900), {}, 24800, &target),
      RelayDeskInputTargetResult::Updated
  );
  const auto other = peer();
  QCOMPARE(
      syncRelayDeskClientTarget(settings, other, endpoint(other, 24800), target.host, target.port, nullptr),
      RelayDeskInputTargetResult::AnotherManagedDevicePreserved
  );
  first.addresses = {QHostAddress(QStringLiteral("192.168.1.21"))};
  QCOMPARE(
      syncRelayDeskClientTarget(settings, first, endpoint(first, 24901), target.host, target.port, &target),
      RelayDeskInputTargetResult::Updated
  );
  QCOMPARE(target.host, QStringLiteral("192.168.1.21"));
  QCOMPARE(target.port, quint16(24901));
}

QTEST_MAIN(RelayDeskInputTargetTests)

#include "RelayDeskInputTargetTests.moc"
