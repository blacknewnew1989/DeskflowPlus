/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/RelayDeskHomeWidget.h"

#include <QFrame>
#include <QLabel>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using deskflow::relaydesk::widgets::RelayDeskHomeWidget;

class RelayDeskHomeWidgetTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void composesTheApprovedSingleColumnHome();
  void exposesHeaderActions();
};

void RelayDeskHomeWidgetTests::composesTheApprovedSingleColumnHome()
{
  auto *devices = new QWidget();
  devices->setObjectName(QStringLiteral("devices"));
  auto *transferBar = new QWidget();
  transferBar->setObjectName(QStringLiteral("transferBar"));
  transferBar->setFixedHeight(52);
  RelayDeskHomeWidget home(devices, transferBar);
  home.setProductName(QStringLiteral("RelayDesk"));
  home.setStatusText(QStringLiteral("Online"));
  home.setLocalDeviceName(QStringLiteral("Studio Mac"));
  home.resize(560, 420);
  home.show();

  QTRY_COMPARE(home.size(), QSize(560, 420));
  auto *header = home.findChild<QFrame *>(QStringLiteral("relaydeskHomeHeader"));
  auto *footer = home.findChild<QFrame *>(QStringLiteral("relaydeskHomeFooter"));
  auto *product = home.findChild<QLabel *>(QStringLiteral("relaydeskHomeProductName"));
  auto *status = home.findChild<QLabel *>(QStringLiteral("relaydeskHomeStatus"));
  auto *localDevice = home.findChild<QLabel *>(QStringLiteral("relaydeskHomeLocalDevice"));
  QVERIFY(header != nullptr);
  QVERIFY(footer != nullptr);
  QVERIFY(product != nullptr);
  QVERIFY(status != nullptr);
  QVERIFY(localDevice != nullptr);
  QCOMPARE(header->height(), 52);
  QCOMPARE(footer->height(), 26);
  QCOMPARE(transferBar->height(), 52);
  QCOMPARE(product->text(), QStringLiteral("RelayDesk"));
  QVERIFY(status->text().contains(QStringLiteral("Online")));
  QVERIFY(localDevice->text().endsWith(QStringLiteral("Studio Mac")));
  QVERIFY(devices->isVisible());
  QVERIFY(transferBar->isVisible());
}

void RelayDeskHomeWidgetTests::exposesHeaderActions()
{
  auto *devices = new QWidget();
  auto *transferBar = new QWidget();
  transferBar->setFixedHeight(52);
  RelayDeskHomeWidget home(devices, transferBar);
  home.show();

  QSignalSpy settings(&home, &RelayDeskHomeWidget::settingsRequested);
  QSignalSpy history(&home, &RelayDeskHomeWidget::transferHistoryRequested);
  auto *settingsButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeSettingsButton"));
  auto *historyButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeHistoryButton"));
  QVERIFY(settingsButton != nullptr);
  QVERIFY(historyButton != nullptr);

  settingsButton->click();
  historyButton->click();
  QCOMPARE(settings.count(), 1);
  QCOMPARE(history.count(), 1);
}

QTEST_MAIN(RelayDeskHomeWidgetTests)

#include "RelayDeskHomeWidgetTests.moc"
