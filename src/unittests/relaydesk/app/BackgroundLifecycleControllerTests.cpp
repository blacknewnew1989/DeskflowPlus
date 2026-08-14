/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/BackgroundLifecycleController.h"

#include <QTest>

using deskflow::relaydesk::BackgroundLifecycleController;
using deskflow::relaydesk::BackgroundLifecycleSettings;
using deskflow::relaydesk::BackgroundShutdownHooks;
using deskflow::relaydesk::WindowCloseDisposition;

class BackgroundLifecycleControllerTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void minimizeAndCloseSettingsRemainIndependent();
  void reminderIsConsumedExactlyOnce();
  void explicitAndOperatingSystemQuitCannotHideTheProcess();
  void shutdownRunsInTheFrozenOrderAndIsIdempotent();
};

void BackgroundLifecycleControllerTests::minimizeAndCloseSettingsRemainIndependent()
{
  BackgroundLifecycleController controller;

  QVERIFY(controller.shouldHideAfterMinimize(false));
  QCOMPARE(controller.closeDisposition(true, false), WindowCloseDisposition::HideToTray);

  controller.setMinimizeToTray(false);
  QVERIFY(!controller.shouldHideAfterMinimize(false));
  QCOMPARE(controller.closeDisposition(true, false), WindowCloseDisposition::HideToTray);

  controller.setCloseToTray(false);
  QCOMPARE(controller.closeDisposition(true, false), WindowCloseDisposition::Quit);

  controller.setMinimizeToTray(true);
  QVERIFY(controller.shouldHideAfterMinimize(false));
  QCOMPARE(controller.closeDisposition(true, false), WindowCloseDisposition::Quit);
}

void BackgroundLifecycleControllerTests::reminderIsConsumedExactlyOnce()
{
  BackgroundLifecycleController pending;
  QVERIFY(pending.takeCloseReminder());
  QVERIFY(!pending.takeCloseReminder());

  BackgroundLifecycleController acknowledged({
      .minimizeToTray = true,
      .closeToTray = true,
      .closeReminderPending = false,
  });
  QVERIFY(!acknowledged.takeCloseReminder());
}

void BackgroundLifecycleControllerTests::explicitAndOperatingSystemQuitCannotHideTheProcess()
{
  BackgroundLifecycleController controller;

  QCOMPARE(controller.closeDisposition(true, true), WindowCloseDisposition::Quit);
  QCOMPARE(controller.closeDisposition(false, false), WindowCloseDisposition::Quit);

  controller.requestQuit();
  QVERIFY(controller.quitRequested());
  QVERIFY(!controller.shouldHideAfterMinimize(false));
  QCOMPARE(controller.closeDisposition(true, false), WindowCloseDisposition::Quit);
}

void BackgroundLifecycleControllerTests::shutdownRunsInTheFrozenOrderAndIsIdempotent()
{
  BackgroundLifecycleController controller;
  QStringList calls;
  const BackgroundShutdownHooks hooks{
      .stopAcceptingOperations = [&calls] { calls.append(QStringLiteral("quiesce")); },
      .stopInputSharing = [&calls] { calls.append(QStringLiteral("input")); },
      .persistAndStopTransfers = [&calls] { calls.append(QStringLiteral("transfers")); },
      .stopNetworkServices = [&calls] { calls.append(QStringLiteral("network")); },
      .removeTrayIcon = [&calls] { calls.append(QStringLiteral("tray")); },
  };

  QVERIFY(controller.beginShutdown(hooks));
  QVERIFY(controller.shutdownStarted());
  QCOMPARE(calls, QStringList({"quiesce", "input", "transfers", "network", "tray"}));

  QVERIFY(!controller.beginShutdown(hooks));
  QCOMPARE(calls.size(), 5);
}

QTEST_GUILESS_MAIN(BackgroundLifecycleControllerTests)

#include "BackgroundLifecycleControllerTests.moc"
