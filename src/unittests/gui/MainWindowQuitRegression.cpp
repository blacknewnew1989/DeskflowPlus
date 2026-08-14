/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/MainWindow.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "relaydesk/discovery/DiscoverySettings.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

namespace {

QAction *findQuitAction(MainWindow &window, bool trayEntryPoint)
{
  for (auto *action : window.findChildren<QAction *>()) {
    if (!trayEntryPoint && action->menuRole() == QAction::QuitRole)
      return action;
    if (trayEntryPoint && action->menuRole() == QAction::NoRole && action->text().contains(QStringLiteral("Quit"))) {
      return action;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  const auto arguments = QCoreApplication::arguments();
  const bool trayEntryPoint = arguments.contains(QStringLiteral("--entrypoint=tray"));
  if (!trayEntryPoint && !arguments.contains(QStringLiteral("--entrypoint=menu")))
    return 2;

  QTemporaryDir directory;
  if (!directory.isValid())
    return 3;

  Settings::setSettingsFile(directory.filePath(QStringLiteral("RelayDesk.conf")));
  Settings::setStateFile(directory.filePath(QStringLiteral("RelayDesk.state")));
  Settings::setValue(Settings::Security::TlsEnabled, false);
  Settings::setValue(Settings::Gui::AutoStartCore, false);
  Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  Settings::setValue(Settings::Gui::Autohide, false);
  Settings::setValue(Settings::Gui::CloseToTray, true);
  Settings::setValue(Settings::Gui::MinimizeToTray, true);
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("127.0.0.1"));

  QSettings relayDeskSettings(Settings::settingsFile(), QSettings::IniFormat);
  deskflow::relaydesk::DiscoverySettingsStore discoveryStore(relayDeskSettings);
  QString diagnostic;
  if (!discoveryStore.save({.enabled = false}, &diagnostic))
    return 4;

  const auto corePath = QDir(QCoreApplication::applicationDirPath()).filePath(kCoreBinName);
  bool createdCorePlaceholder = false;
  if (!QFile::exists(corePath)) {
    QFile placeholder(corePath);
    if (!placeholder.open(QIODevice::WriteOnly) ||
        placeholder.write("RelayDesk true-quit regression placeholder\n") <= 0) {
      return 5;
    }
    createdCorePlaceholder = true;
  }

  int result = 0;
  {
    MainWindow window;
    auto *quitAction = findQuitAction(window, trayEntryPoint);
    if (quitAction == nullptr) {
      result = 6;
    } else {
      QTimer watchdog;
      watchdog.setSingleShot(true);
      QObject::connect(&watchdog, &QTimer::timeout, &app, [] { QCoreApplication::exit(86); });
      watchdog.start(3000);
      QTimer::singleShot(0, quitAction, &QAction::trigger);
      result = app.exec();
    }
  }

  if (createdCorePlaceholder && !QFile::remove(corePath))
    return 7;
  return result;
}
