/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/config/IServerConfig.h"
#include "gui/core/CoreProcess.h"

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace deskflow::gui {
namespace {

class FakeServerConfig final : public IServerConfig
{
public:
  [[nodiscard]] bool isFull() const override
  {
    return false;
  }

  [[nodiscard]] bool screenExists(const QString &) const override
  {
    return false;
  }

  [[nodiscard]] bool save(const QString &) const override
  {
    return true;
  }

  void save(QFile &) const override
  {
  }

  [[nodiscard]] const ScreenList &screens() const override
  {
    return m_screens;
  }

private:
  ScreenList m_screens;
};

} // namespace

class CoreProcessTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void stopCancelsRetryPendingAndTransitionsToStopped();

private:
  QTemporaryDir m_settingsDirectory;
  QString m_corePath;
  bool m_createdCorePlaceholder = false;
};

void CoreProcessTests::initTestCase()
{
  QVERIFY(m_settingsDirectory.isValid());
  Settings::setSettingsFile(m_settingsDirectory.filePath(QStringLiteral("RelayDesk.conf")));
  Settings::setStateFile(m_settingsDirectory.filePath(QStringLiteral("RelayDesk.state")));

  m_corePath = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(m_corePath)) {
    QFile placeholder(m_corePath);
    QVERIFY(placeholder.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    placeholder.close();
    m_createdCorePlaceholder = true;
  }
}

void CoreProcessTests::cleanupTestCase()
{
  if (m_createdCorePlaceholder) {
    QVERIFY(QFile::remove(m_corePath));
  }
}

void CoreProcessTests::stopCancelsRetryPendingAndTransitionsToStopped()
{
  FakeServerConfig serverConfig;
  CoreProcess process(serverConfig);
  QSignalSpy stateChanged(&process, &CoreProcess::processStateChanged);

  process.m_processState = CoreProcess::ProcessState::RetryPending;
  process.m_retryTimer.setSingleShot(true);
  process.m_retryTimer.start(20);
  QVERIFY(process.m_retryTimer.isActive());

  process.stop();

  QCOMPARE(process.processState(), CoreProcess::ProcessState::Stopped);
  QVERIFY(!process.m_retryTimer.isActive());
  QCOMPARE(stateChanged.count(), 1);
  QCOMPARE(
      stateChanged.constFirst().constFirst().value<CoreProcess::ProcessState>(),
      CoreProcess::ProcessState::Stopped
  );

  QTest::qWait(40);
  QCOMPARE(process.processState(), CoreProcess::ProcessState::Stopped);
  QCOMPARE(stateChanged.count(), 1);
}

} // namespace deskflow::gui

using deskflow::gui::CoreProcessTests;
QTEST_GUILESS_MAIN(CoreProcessTests)

#include "CoreProcessTests.moc"
