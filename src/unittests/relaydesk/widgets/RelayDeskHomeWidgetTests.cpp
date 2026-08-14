/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/RelayDeskHomeWidget.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTranslator>

using deskflow::relaydesk::widgets::RelayDeskHomeWidget;

namespace {

class SemanticTranslator final : public QTranslator
{
public:
  QString translate(const char *context, const char *sourceText, const char *, int) const override
  {
    if (QString::fromLatin1(context) != QStringLiteral("RelayDesk"))
      return {};

    const auto key = QString::fromLatin1(sourceText);
    if (key == QStringLiteral("devices.current"))
      return QStringLiteral("Cet appareil");
    if (key == QStringLiteral("transfer.title"))
      return QStringLiteral("Transferts test");
    if (key == QStringLiteral("settings.title"))
      return QStringLiteral("Paramètres test");
    return {};
  }
};

class TranslatorGuard final
{
public:
  explicit TranslatorGuard(QTranslator &translator) : m_translator(translator)
  {
    QCoreApplication::installTranslator(&m_translator);
  }

  ~TranslatorGuard()
  {
    QCoreApplication::removeTranslator(&m_translator);
  }

private:
  QTranslator &m_translator;
};

} // namespace

class RelayDeskHomeWidgetTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void composesTheApprovedSingleColumnHome();
  void exposesHeaderActions();
  void refreshesSemanticTextOnLanguageChange();
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
  QAction sharingAction(QStringLiteral("Pause sharing"), &home);
  QSignalSpy sharing(&sharingAction, &QAction::triggered);
  home.setSharingAction(&sharingAction);
  auto *sharingButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeSharingButton"));
  auto *settingsButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeSettingsButton"));
  auto *historyButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeHistoryButton"));
  QVERIFY(sharingButton != nullptr);
  QVERIFY(settingsButton != nullptr);
  QVERIFY(historyButton != nullptr);
  QCOMPARE(sharingButton->accessibleName(), QStringLiteral("Pause sharing"));

  sharingAction.setText(QStringLiteral("&Continue sharing"));
  QCOMPARE(sharingButton->accessibleName(), QStringLiteral("Continue sharing"));

  sharingButton->click();
  settingsButton->click();
  historyButton->click();
  QCOMPARE(sharing.count(), 1);
  QCOMPARE(settings.count(), 1);
  QCOMPARE(history.count(), 1);

  QAction replacement(QStringLiteral("Continue sharing"), &home);
  QSignalSpy replacementTriggered(&replacement, &QAction::triggered);
  home.setSharingAction(&replacement);
  sharingAction.setText(QStringLiteral("Old action changed"));
  QCOMPARE(sharingButton->accessibleName(), QStringLiteral("Continue sharing"));
  sharingButton->click();
  QCOMPARE(sharing.count(), 1);
  QCOMPARE(replacementTriggered.count(), 1);
}

void RelayDeskHomeWidgetTests::refreshesSemanticTextOnLanguageChange()
{
  auto *devices = new QWidget();
  auto *transferBar = new QWidget();
  RelayDeskHomeWidget home(devices, transferBar);
  home.setLocalDeviceName(QStringLiteral("Studio Mac"));
  home.show();

  auto *localDevice = home.findChild<QLabel *>(QStringLiteral("relaydeskHomeLocalDevice"));
  auto *settingsButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeSettingsButton"));
  auto *historyButton = home.findChild<QToolButton *>(QStringLiteral("relaydeskHomeHistoryButton"));
  QVERIFY(localDevice != nullptr);
  QVERIFY(settingsButton != nullptr);
  QVERIFY(historyButton != nullptr);
  QCOMPARE(localDevice->text(), QStringLiteral("This device · Studio Mac"));
  QCOMPARE(settingsButton->toolTip(), QStringLiteral("Settings"));
  QCOMPARE(historyButton->toolTip(), QStringLiteral("Transfers"));

  SemanticTranslator translator;
  TranslatorGuard guard(translator);
  QEvent languageChange(QEvent::LanguageChange);
  QCoreApplication::sendEvent(&home, &languageChange);

  QCOMPARE(localDevice->text(), QStringLiteral("Cet appareil · Studio Mac"));
  QCOMPARE(localDevice->accessibleName(), localDevice->text());
  QCOMPARE(settingsButton->toolTip(), QStringLiteral("Paramètres test"));
  QCOMPARE(settingsButton->accessibleName(), settingsButton->toolTip());
  QCOMPARE(historyButton->toolTip(), QStringLiteral("Transferts test"));
  QCOMPARE(historyButton->accessibleName(), historyButton->toolTip());
}

QTEST_MAIN(RelayDeskHomeWidgetTests)

#include "RelayDeskHomeWidgetTests.moc"
