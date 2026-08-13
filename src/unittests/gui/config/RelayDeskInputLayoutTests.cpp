/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Settings.h"
#include "gui/config/RelayDeskInputLayout.h"
#include "gui/config/ServerConfig.h"
#include "relaydesk/device/DeviceSnapshot.h"

#include <QTemporaryDir>
#include <QTest>

#include <utility>

using deskflow::gui::RelayDeskInputLayoutResult;
using deskflow::gui::syncRelayDeskInputScreen;
using namespace deskflow::relaydesk;

class RelayDeskInputLayoutTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void init();
  void trustedInputPeerIsAddedAndRestored();
  void repeatedObservationIsIdempotent();
  void ineligiblePeersDoNotChangeLayout();
  void invalidNameAndExternalConfigArePreserved();

private:
  [[nodiscard]] DeviceSnapshot peer(QString name = QStringLiteral("Mike")) const;

  QTemporaryDir m_directory;
  QString m_settingsFile;
  int m_testSettingsIndex = 0;
};

void RelayDeskInputLayoutTests::initTestCase()
{
  QVERIFY(m_directory.isValid());
  Settings::setStateFile(m_directory.filePath(QStringLiteral("RelayDesk.state")));
}

void RelayDeskInputLayoutTests::init()
{
  m_settingsFile = m_directory.filePath(QStringLiteral("RelayDesk-%1.conf").arg(++m_testSettingsIndex));
  Settings::setSettingsFile(m_settingsFile);
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("mac-server"));
  Settings::setValue(Settings::Server::ExternalConfig, false);
}

DeviceSnapshot RelayDeskInputLayoutTests::peer(QString name) const
{
  return {
      .id = DeviceId::generate(),
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .presence = DevicePresence::Online,
      .trusted = true,
      .capabilities = {.input = true},
  };
}

void RelayDeskInputLayoutTests::trustedInputPeerIsAddedAndRestored()
{
  ServerConfig config;
  QCOMPARE(syncRelayDeskInputScreen(config, peer()), RelayDeskInputLayoutResult::Added);
  QVERIFY(config.screenExists(QStringLiteral("Mike")));
  QVERIFY(config.screenExists(QStringLiteral("mac-server")));

  config.commit();
  ServerConfig restored;
  QVERIFY(restored.screenExists(QStringLiteral("Mike")));
  QVERIFY(restored.screenExists(QStringLiteral("mac-server")));
}

void RelayDeskInputLayoutTests::repeatedObservationIsIdempotent()
{
  ServerConfig config;
  const auto snapshot = peer();
  QCOMPARE(syncRelayDeskInputScreen(config, snapshot), RelayDeskInputLayoutResult::Added);
  QCOMPARE(syncRelayDeskInputScreen(config, snapshot), RelayDeskInputLayoutResult::AlreadyPresent);
  QCOMPARE(config.numScreens(), 2);
}

void RelayDeskInputLayoutTests::ineligiblePeersDoNotChangeLayout()
{
  ServerConfig config;
  auto snapshot = peer();
  snapshot.trusted = false;
  QCOMPARE(syncRelayDeskInputScreen(config, snapshot), RelayDeskInputLayoutResult::NotTrusted);
  snapshot.trusted = true;
  snapshot.capabilities.input = false;
  QCOMPARE(syncRelayDeskInputScreen(config, snapshot), RelayDeskInputLayoutResult::InputUnsupported);
  QCOMPARE(config.numScreens(), 0);
}

void RelayDeskInputLayoutTests::invalidNameAndExternalConfigArePreserved()
{
  ServerConfig config;
  QCOMPARE(
      syncRelayDeskInputScreen(config, peer(QStringLiteral("invalid screen"))),
      RelayDeskInputLayoutResult::InvalidScreenName
  );
  Settings::setValue(Settings::Server::ExternalConfig, true);
  QCOMPARE(syncRelayDeskInputScreen(config, peer()), RelayDeskInputLayoutResult::ExternalConfigActive);
  QCOMPARE(config.numScreens(), 0);
}

QTEST_MAIN(RelayDeskInputLayoutTests)

#include "RelayDeskInputLayoutTests.moc"
