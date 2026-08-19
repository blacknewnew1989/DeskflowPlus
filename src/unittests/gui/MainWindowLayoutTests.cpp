/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/MainWindow.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/widgets/LogDock.h"
#include "relaydesk/discovery/DiscoverySettings.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/RelayDeskHomeWidget.h"
#include "relaydesk/widgets/TransferCenterDock.h"
#include "relaydesk/widgets/TransferMiniBar.h"

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLineEdit>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

class MainWindowLayoutTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void init();
  void cleanupTestCase();

  void freshLaunchUsesCompactSingleHomeSurface();
  void settingsCanConfigureInputRoleAndRemoteHost();
  void hiddenWindowKeepsCurrentSessionGeometry();
  void restoredSmallGeometryIsClampedToMinimumSize();

private:
  std::unique_ptr<QTemporaryDir> m_directory;
  QString m_corePath;
  bool m_createdCorePlaceholder = false;
};

void MainWindowLayoutTests::initTestCase()
{
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());

  Settings::setSettingsFile(m_directory->filePath(QStringLiteral("RelayDesk.conf")));
  Settings::setStateFile(m_directory->filePath(QStringLiteral("RelayDesk.state")));

  Settings::setValue(Settings::Security::TlsEnabled, false);
  Settings::setValue(Settings::Gui::AutoStartCore, false);
  Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  Settings::setValue(Settings::Gui::Autohide, false);
  Settings::setValue(Settings::Gui::CloseToTray, false);
  Settings::setValue(Settings::Gui::MinimizeToTray, false);
  Settings::setValue(Settings::Gui::LogExpanded, false);
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("127.0.0.1"));

  QSettings relayDeskSettings(Settings::settingsFile(), QSettings::IniFormat);
  deskflow::relaydesk::DiscoverySettingsStore discoveryStore(relayDeskSettings);
  QString diagnostic;
  QVERIFY2(discoveryStore.save({.enabled = false}, &diagnostic), qPrintable(diagnostic));

  m_corePath = QDir(QCoreApplication::applicationDirPath()).filePath(kCoreBinName);
  if (!QFile::exists(m_corePath)) {
    QFile placeholder(m_corePath);
    QVERIFY2(placeholder.open(QIODevice::WriteOnly), qPrintable(placeholder.errorString()));
    QVERIFY(placeholder.write("RelayDesk MainWindow layout test placeholder\n") > 0);
    placeholder.close();
    m_createdCorePlaceholder = true;
  }
}

void MainWindowLayoutTests::init()
{
  Settings::setValue(Settings::Gui::WindowGeometry, {});
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::None);
  Settings::setValue(Settings::Client::RemoteHost);
}

void MainWindowLayoutTests::cleanupTestCase()
{
  if (m_createdCorePlaceholder) {
    QVERIFY(QFile::remove(m_corePath));
  }
}

void MainWindowLayoutTests::freshLaunchUsesCompactSingleHomeSurface()
{
  MainWindow window;

  QCOMPARE(window.minimumSize(), QSize(520, 380));
  QCOMPARE(window.size(), QSize(560, 420));

  window.open(false);
  QTRY_VERIFY(window.isVisible());
  QCOMPARE(window.size(), QSize(560, 420));

  auto &devices = window.relayDeskDevicesDock();
  auto &transfers = window.relayDeskTransferCenterDock();
  auto *log = window.findChild<LogDock *>();
  auto *home =
      window.findChild<deskflow::relaydesk::widgets::RelayDeskHomeWidget *>(QStringLiteral("relaydeskCompactHome"));
  auto *header = window.findChild<QWidget *>(QStringLiteral("relaydeskHomeHeader"));
  auto *legacy = window.findChild<QWidget *>(QStringLiteral("relaydeskLegacyControls"));
  auto *miniBar =
      window.findChild<deskflow::relaydesk::widgets::TransferMiniBar *>(QStringLiteral("relaydeskTransferMiniBar"));

  QVERIFY(home != nullptr);
  QCOMPARE(window.centralWidget(), home);
  QVERIFY(home->isVisible());
  QVERIFY(header != nullptr);
  QCOMPARE(header->height(), 52);
  QVERIFY(legacy != nullptr);
  QVERIFY(!legacy->isVisible());
  QVERIFY(devices.isVisible());
  QVERIFY(!devices.isFloating());
  QCOMPARE(window.dockWidgetArea(&devices), Qt::NoDockWidgetArea);
  QVERIFY(miniBar != nullptr);
  QCOMPARE(miniBar->minimumHeight(), 52);
  QCOMPARE(miniBar->maximumHeight(), 52);
  QVERIFY(!miniBar->isVisible());
  QVERIFY(!transfers.isVisible());
  QVERIFY(log != nullptr);
  QVERIFY(!log->isVisible());

  QAction *windowQuit = nullptr;
  QAction *trayQuit = nullptr;
  for (auto *action : window.findChildren<QAction *>()) {
    if (action->menuRole() == QAction::QuitRole)
      windowQuit = action;
    else if (action->menuRole() == QAction::NoRole && action->text().contains(QStringLiteral("Quit")))
      trayQuit = action;
  }
  QVERIFY(windowQuit != nullptr);
  QVERIFY(trayQuit != nullptr);
  QVERIFY(!windowQuit->shortcut().isEmpty());
  QVERIFY(trayQuit->shortcut().isEmpty());

  int visibleDockSurfaces = 0;
  for (auto *dock : window.findChildren<QDockWidget *>()) {
    if (dock->isVisible()) {
      ++visibleDockSurfaces;
    }
  }
  QCOMPARE(visibleDockSurfaces, 1);

  int visibleTopLevelSurfaces = 0;
  for (auto *widget : QApplication::topLevelWidgets()) {
    if (widget->isVisible() &&
        (qobject_cast<MainWindow *>(widget) != nullptr || qobject_cast<QDialog *>(widget) != nullptr)) {
      ++visibleTopLevelSurfaces;
    }
  }
  QCOMPARE(visibleTopLevelSurfaces, 1);
}

void MainWindowLayoutTests::settingsCanConfigureInputRoleAndRemoteHost()
{
  MainWindow window;

  bool serverControlsFound = false;
  QTimer::singleShot(0, &window, [&] {
    auto *dialog = window.findChild<QDialog *>();
    QVERIFY(dialog != nullptr);
    auto *server = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleServer"));
    auto *client = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleClient"));
    auto *remoteHostRow = dialog->findChild<QWidget *>(QStringLiteral("widgetInputRoleRemoteHost"));
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    serverControlsFound = server != nullptr && client != nullptr && remoteHostRow != nullptr && buttons != nullptr;
    if (!serverControlsFound) {
      dialog->reject();
      return;
    }
    server->setChecked(true);
    QVERIFY(remoteHostRow->isHidden());
    buttons->button(QDialogButtonBox::Save)->click();
  });
  QVERIFY(QMetaObject::invokeMethod(&window, "openSettings", Qt::DirectConnection));
  QVERIFY(serverControlsFound);
  QCOMPARE(window.coreMode(), Settings::CoreMode::Server);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::CoreMode::Server);

  bool clientControlsFound = false;
  QTimer::singleShot(0, &window, [&] {
    auto *dialog = window.findChild<QDialog *>();
    QVERIFY(dialog != nullptr);
    auto *client = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleClient"));
    auto *remoteHost = dialog->findChild<QLineEdit *>(QStringLiteral("lineInputRoleRemoteHost"));
    auto *remoteHostRow = dialog->findChild<QWidget *>(QStringLiteral("widgetInputRoleRemoteHost"));
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    clientControlsFound = client != nullptr && remoteHost != nullptr && remoteHostRow != nullptr && buttons != nullptr;
    if (!clientControlsFound) {
      dialog->reject();
      return;
    }
    client->setChecked(true);
    QVERIFY(!remoteHostRow->isHidden());
    remoteHost->setText(QStringLiteral("  192.168.1.20  "));
    buttons->button(QDialogButtonBox::Save)->click();
  });
  QVERIFY(QMetaObject::invokeMethod(&window, "openSettings", Qt::DirectConnection));
  QVERIFY(clientControlsFound);
  QCOMPARE(window.coreMode(), Settings::CoreMode::Client);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::CoreMode::Client);
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("192.168.1.20"));
}

void MainWindowLayoutTests::restoredSmallGeometryIsClampedToMinimumSize()
{
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(80, 80, 320, 240));

  MainWindow window;

  QCOMPARE(window.minimumSize(), QSize(520, 380));
  QVERIFY(window.width() >= 520);
  QVERIFY(window.height() >= 380);
}

void MainWindowLayoutTests::hiddenWindowKeepsCurrentSessionGeometry()
{
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(40, 40, 560, 420));
  MainWindow window;
  window.open(false);
  QTRY_VERIFY(window.isVisible());

  window.setGeometry(QRect(120, 100, 620, 460));
  QCoreApplication::processEvents();
  const auto currentSessionGeometry = window.geometry();
  window.hide();

  // macOS hides the application natively, so QWidget::isVisible() remains true.
  // Change the persisted value to prove that the foreground restore uses the
  // geometry remembered by the running window instead of reloading settings.
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(10, 10, 540, 400));

  window.open(false);
  QTRY_VERIFY(window.isVisible());
  QCOMPARE(window.geometry(), currentSessionGeometry);
}

QTEST_MAIN(MainWindowLayoutTests)

#include "MainWindowLayoutTests.moc"
