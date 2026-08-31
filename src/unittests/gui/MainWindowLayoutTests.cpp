/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/MainWindow.h"

#include "common/Constants.h"
#include "common/I18N.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/core/CoreProcess.h"
#include "gui/dialogs/AboutDialog.h"
#include "gui/dialogs/SettingsDialog.h"
#include "gui/widgets/LogDock.h"
#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/discovery/DiscoveryService.h"
#include "relaydesk/app/PairingTrustRuntime.h"
#include "relaydesk/app/TransferRuntimeComposition.h"
#include "relaydesk/discovery/DiscoverySettings.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/transfer/TransferSettings.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/RelayDeskHomeWidget.h"
#include "relaydesk/widgets/TransferCenterDock.h"
#include "relaydesk/widgets/TransferMiniBar.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>
#include <utility>

namespace {

QAction *findMenuRoleAction(MainWindow &window, QAction::MenuRole role)
{
  for (auto *action : window.findChildren<QAction *>()) {
    if (action->menuRole() == role)
      return action;
  }
  return nullptr;
}

class NextDialogInteractor final : public QObject
{
public:
  NextDialogInteractor(QWidget &owner, std::function<void(QDialog *)> interaction)
      : QObject(&owner),
        m_owner(owner),
        m_interaction(std::move(interaction))
  {
    qApp->installEventFilter(this);
  }

  ~NextDialogInteractor() override
  {
    qApp->removeEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    auto *dialog = qobject_cast<QDialog *>(watched);
    if (m_interaction && event->type() == QEvent::Show && dialog != nullptr && dialog->parentWidget() == &m_owner) {
      auto interaction = std::move(m_interaction);
      QTimer::singleShot(0, dialog, [dialog, interaction = std::move(interaction)] { interaction(dialog); });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QWidget &m_owner;
  std::function<void(QDialog *)> m_interaction;
};

} // namespace

class MainWindowLayoutTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void init();
  void cleanupTestCase();

  void freshLaunchUsesCompactSingleHomeSurface();
  void settingsCanConfigureInputRoleAndRemoteHost();
  void settingsRoleImmediatelyUpdatesTlsControlsAndRemoteHostLayout();
  void fileTransferSettingsFitWithinDialog();
  void fileTransferSettingsPersistAndReopen();
  void fileTransferSettingsEntryAppliesToRuntime();
  void transferRuntimeIsDestroyedBeforeReferencedDependencies();
  void hiddenWindowKeepsCurrentSessionGeometry();
  void restoredSmallGeometryIsClampedToMinimumSize();
  void trayIconLoadsEmbeddedWindowsFallback();
  void chineseProductChromeUsesLocalizedText();
  void manualAddressesPersistAndStartTargetedDiscovery();
  void trustedDeviceCardActionsPersistAndRevoke();
  void trustCardPersistenceFailureShowsNonModalFeedback();
  void autoAcceptPrimaryWriteFailureShowsNonModalFeedback();

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
  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
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
  Settings::setValue(Settings::Security::TlsEnabled, false);
  Settings::setValue(Settings::Gui::WindowGeometry, {});
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::None);
  Settings::setValue(Settings::Client::RemoteHost);
  QSettings transferSettings(Settings::settingsFile(), QSettings::IniFormat);
  transferSettings.remove(::relaydesk::transfer::TransferSettingsStore::schemaVersionKey());
  transferSettings.remove(::relaydesk::transfer::TransferSettingsStore::receiveRootKey());
  transferSettings.remove(::relaydesk::transfer::TransferSettingsStore::incomingPolicyKey());
  transferSettings.remove(::relaydesk::transfer::TransferSettingsStore::defaultConflictPolicyKey());
  transferSettings.sync();
  QCOMPARE(transferSettings.status(), QSettings::NoError);
  QDir relayDeskState(m_directory->filePath(QStringLiteral("relaydesk")));
  if (relayDeskState.exists())
    QVERIFY(relayDeskState.removeRecursively());
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
  auto *settingsAction = findMenuRoleAction(window, QAction::PreferencesRole);
  QVERIFY(settingsAction != nullptr);

  bool serverControlsFound = false;
  bool serverVisibilityCorrect = false;
  bool serverAccepted = false;
  NextDialogInteractor serverDialog(window, [&](QDialog *dialog) {
    auto *server = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleServer"));
    auto *client = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleClient"));
    auto *remoteHostRow = dialog->findChild<QWidget *>(QStringLiteral("widgetInputRoleRemoteHost"));
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    auto *save = buttons == nullptr ? nullptr : buttons->button(QDialogButtonBox::Save);
    serverControlsFound = server != nullptr && client != nullptr && remoteHostRow != nullptr && save != nullptr;
    if (!serverControlsFound) {
      dialog->reject();
      return;
    }
    server->setChecked(true);
    serverVisibilityCorrect = remoteHostRow->isHidden();
    if (!serverVisibilityCorrect) {
      dialog->reject();
      return;
    }
    save->click();
    serverAccepted = dialog->result() == QDialog::Accepted;
    if (!serverAccepted)
      dialog->reject();
  });
  settingsAction->trigger();
  QVERIFY(serverControlsFound);
  QVERIFY(serverVisibilityCorrect);
  QVERIFY(serverAccepted);
  QCOMPARE(window.coreMode(), Settings::CoreMode::Server);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::CoreMode::Server);

  bool clientControlsFound = false;
  bool clientVisibilityCorrect = false;
  bool clientAccepted = false;
  NextDialogInteractor clientDialog(window, [&](QDialog *dialog) {
    auto *client = dialog->findChild<QRadioButton *>(QStringLiteral("rbInputRoleClient"));
    auto *remoteHost = dialog->findChild<QLineEdit *>(QStringLiteral("lineInputRoleRemoteHost"));
    auto *remoteHostRow = dialog->findChild<QWidget *>(QStringLiteral("widgetInputRoleRemoteHost"));
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    auto *save = buttons == nullptr ? nullptr : buttons->button(QDialogButtonBox::Save);
    clientControlsFound = client != nullptr && remoteHost != nullptr && remoteHostRow != nullptr && save != nullptr;
    if (!clientControlsFound) {
      dialog->reject();
      return;
    }
    client->setChecked(true);
    clientVisibilityCorrect = !remoteHostRow->isHidden();
    if (!clientVisibilityCorrect) {
      dialog->reject();
      return;
    }
    remoteHost->setText(QStringLiteral("  192.168.1.20  "));
    save->click();
    clientAccepted = dialog->result() == QDialog::Accepted;
    if (!clientAccepted)
      dialog->reject();
  });
  settingsAction->trigger();
  QVERIFY(clientControlsFound);
  QVERIFY(clientVisibilityCorrect);
  QVERIFY(clientAccepted);
  QCOMPARE(window.coreMode(), Settings::CoreMode::Client);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::CoreMode::Client);
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("192.168.1.20"));
}

void MainWindowLayoutTests::settingsRoleImmediatelyUpdatesTlsControlsAndRemoteHostLayout()
{
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);

  ServerConfig config;
  deskflow::gui::CoreProcess core(config);
  core.setMode(Settings::CoreMode::Client);
  SettingsDialog dialog(nullptr, config, core);
  dialog.show();
  QTRY_VERIFY(dialog.isVisible());

  auto *tabs = dialog.findChild<QTabWidget *>();
  auto *advanced = dialog.findChild<QWidget *>(QStringLiteral("tabAdvanced"));
  auto *server = dialog.findChild<QRadioButton *>(QStringLiteral("rbInputRoleServer"));
  auto *client = dialog.findChild<QRadioButton *>(QStringLiteral("rbInputRoleClient"));
  auto *remoteHostRow = dialog.findChild<QWidget *>(QStringLiteral("widgetInputRoleRemoteHost"));
  auto *requireClientCert = dialog.findChild<QCheckBox *>(QStringLiteral("cbRequireClientCert"));
  QVERIFY(tabs != nullptr);
  QVERIFY(advanced != nullptr);
  QVERIFY(server != nullptr);
  QVERIFY(client != nullptr);
  QVERIFY(remoteHostRow != nullptr);
  QVERIFY(requireClientCert != nullptr);
  tabs->setCurrentWidget(advanced);

  QVERIFY(server->isChecked());
  QVERIFY(requireClientCert->isEnabled());
  const int serverHeight = dialog.height();

  client->setChecked(true);
  QTRY_VERIFY(client->isChecked());
  QTRY_VERIFY(!requireClientCert->isEnabled());
  QTRY_VERIFY(remoteHostRow->isVisible());
  QVERIFY(remoteHostRow->mapTo(&dialog, remoteHostRow->rect().bottomRight()).y() <= dialog.rect().bottom());
  QVERIFY(dialog.height() >= serverHeight);

  server->setChecked(true);
  QTRY_VERIFY(server->isChecked());
  QTRY_VERIFY(requireClientCert->isEnabled());
  QTRY_VERIFY(!remoteHostRow->isVisible());
  QTRY_VERIFY2(
      dialog.height() == serverHeight,
      qPrintable(QStringLiteral("height=%1 sizeHint=%2 minimumHeight=%3 maximumHeight=%4 serverHeight=%5")
                     .arg(dialog.height())
                     .arg(dialog.sizeHint().height())
                     .arg(dialog.minimumHeight())
                     .arg(dialog.maximumHeight())
                     .arg(serverHeight))
  );
}

void MainWindowLayoutTests::fileTransferSettingsFitWithinDialog()
{
  ServerConfig config;
  deskflow::gui::CoreProcess core(config);
  SettingsDialog dialog(nullptr, config, core);
  dialog.show();
  QTRY_VERIFY(dialog.isVisible());

  auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("relaydeskFileTransferSettingsGroup"));
  auto *folder = dialog.findChild<QLineEdit *>(QStringLiteral("relaydeskReceiveFolder"));
  auto *incoming = dialog.findChild<QComboBox *>(QStringLiteral("relaydeskIncomingTransferPolicy"));
  auto *conflict = dialog.findChild<QComboBox *>(QStringLiteral("relaydeskDefaultConflictPolicy"));
  QVERIFY(group != nullptr);
  QVERIFY(folder != nullptr);
  QVERIFY(incoming != nullptr);
  QVERIFY(conflict != nullptr);
  QVERIFY(group->isVisible());
  QCOMPARE(conflict->count(), 4);
  for (const auto &language :
       {QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("it"), QStringLiteral("ja"), QStringLiteral("ko"),
        QStringLiteral("ru"), QStringLiteral("zh_CN")}) {
    I18N::setLanguage(language);
    QCoreApplication::processEvents();
    QTRY_VERIFY2_WITH_TIMEOUT(
        dialog.sizeHint().height() <= dialog.height(),
        qPrintable(QStringLiteral("language=%1 sizeHint=%2x%3 actual=%4x%5")
                       .arg(language)
                       .arg(dialog.sizeHint().width())
                       .arg(dialog.sizeHint().height())
                       .arg(dialog.width())
                       .arg(dialog.height())),
        1000
    );
    QTRY_VERIFY2_WITH_TIMEOUT(
        dialog.sizeHint().width() <= dialog.width(),
        qPrintable(QStringLiteral("language=%1 sizeHint=%2x%3 actual=%4x%5")
                       .arg(language)
                       .arg(dialog.sizeHint().width())
                       .arg(dialog.sizeHint().height())
                       .arg(dialog.width())
                       .arg(dialog.height())),
        1000
    );
    for (QWidget *widget :
         {static_cast<QWidget *>(group), static_cast<QWidget *>(folder), static_cast<QWidget *>(incoming),
          static_cast<QWidget *>(conflict)}) {
      const QRect rect(dialog.mapFromGlobal(widget->mapToGlobal(QPoint{})), widget->size());
      QVERIFY(dialog.contentsRect().contains(rect));
    }
  }
  dialog.focusFileTransferSettings();
  QCoreApplication::processEvents();
  auto *tabs = dialog.findChild<QTabWidget *>();
  QVERIFY(tabs != nullptr);
  for (int index = 0; index < tabs->count(); ++index)
    QCOMPARE(tabs->isTabVisible(index), tabs->widget(index)->objectName() == QStringLiteral("tabRegular"));
  QVERIFY(!dialog.findChild<QGroupBox *>(QStringLiteral("groupApp"))->isVisible());
  QVERIFY(!dialog.findChild<QGroupBox *>(QStringLiteral("groupSecurity"))->isVisible());
  QVERIFY(group->isVisible());
  QVERIFY(dialog.sizeHint().height() <= dialog.height());
  I18N::setLanguage(QStringLiteral("en"));
}

void MainWindowLayoutTests::fileTransferSettingsPersistAndReopen()
{
  ServerConfig config;
  deskflow::gui::CoreProcess core(config);
  const auto receiveRoot = m_directory->filePath(QStringLiteral("incoming"));

  SettingsDialog dialog(nullptr, config, core);
  auto *folder = dialog.findChild<QLineEdit *>(QStringLiteral("relaydeskReceiveFolder"));
  auto *incoming = dialog.findChild<QComboBox *>(QStringLiteral("relaydeskIncomingTransferPolicy"));
  auto *conflict = dialog.findChild<QComboBox *>(QStringLiteral("relaydeskDefaultConflictPolicy"));
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(folder != nullptr);
  QVERIFY(incoming != nullptr);
  QVERIFY(conflict != nullptr);
  QVERIFY(buttons != nullptr);
  folder->setText(receiveRoot);
  incoming->setCurrentIndex(
      incoming->findData(static_cast<int>(::relaydesk::transfer::IncomingTransferPolicy::AutoAcceptTrusted))
  );
  conflict->setCurrentIndex(conflict->findData(static_cast<int>(::relaydesk::transfer::ConflictPolicy::Skip)));
  QSignalSpy saved(&dialog, &SettingsDialog::transferSettingsSaved);
  buttons->button(QDialogButtonBox::Save)->click();
  QCOMPARE(dialog.result(), QDialog::Accepted);
  QCOMPARE(saved.count(), 1);
  const auto emitted = saved.constFirst().constFirst().value<::relaydesk::transfer::TransferSettings>();
  QCOMPARE(emitted.receiveRoot, QDir::cleanPath(receiveRoot));
  QCOMPARE(emitted.incomingPolicy, ::relaydesk::transfer::IncomingTransferPolicy::AutoAcceptTrusted);
  QCOMPARE(emitted.defaultConflictPolicy, ::relaydesk::transfer::ConflictPolicy::Skip);

  SettingsDialog reopened(nullptr, config, core);
  QCOMPARE(
      reopened.findChild<QLineEdit *>(QStringLiteral("relaydeskReceiveFolder"))->text(), QDir::cleanPath(receiveRoot)
  );
  QCOMPARE(
      reopened.findChild<QComboBox *>(QStringLiteral("relaydeskIncomingTransferPolicy"))->currentData().toInt(),
      static_cast<int>(::relaydesk::transfer::IncomingTransferPolicy::AutoAcceptTrusted)
  );
  QCOMPARE(
      reopened.findChild<QComboBox *>(QStringLiteral("relaydeskDefaultConflictPolicy"))->currentData().toInt(),
      static_cast<int>(::relaydesk::transfer::ConflictPolicy::Skip)
  );
}

void MainWindowLayoutTests::fileTransferSettingsEntryAppliesToRuntime()
{
  MainWindow window;
  auto *runtime = window.findChild<deskflow::relaydesk::TransferRuntimeComposition *>();
  QVERIFY(runtime != nullptr);
  const auto receiveRoot = m_directory->filePath(QStringLiteral("runtime-incoming"));
  bool dedicatedDialog = false;
  bool accepted = false;

  NextDialogInteractor fileSettings(window, [&](QDialog *dialog) {
    auto *folder = dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskReceiveFolder"));
    auto *incoming = dialog->findChild<QComboBox *>(QStringLiteral("relaydeskIncomingTransferPolicy"));
    auto *conflict = dialog->findChild<QComboBox *>(QStringLiteral("relaydeskDefaultConflictPolicy"));
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    auto *app = dialog->findChild<QGroupBox *>(QStringLiteral("groupApp"));
    auto *security = dialog->findChild<QGroupBox *>(QStringLiteral("groupSecurity"));
    dedicatedDialog = folder != nullptr && incoming != nullptr && conflict != nullptr && buttons != nullptr &&
                      app != nullptr && security != nullptr && !app->isVisible() && !security->isVisible();
    if (!dedicatedDialog) {
      dialog->reject();
      return;
    }
    folder->setText(receiveRoot);
    incoming->setCurrentIndex(
        incoming->findData(static_cast<int>(::relaydesk::transfer::IncomingTransferPolicy::AutoAcceptTrusted))
    );
    conflict->setCurrentIndex(conflict->findData(static_cast<int>(::relaydesk::transfer::ConflictPolicy::Overwrite)));
    buttons->button(QDialogButtonBox::Save)->click();
    accepted = dialog->result() == QDialog::Accepted;
    if (!accepted)
      dialog->reject();
  });
  Q_EMIT window.relayDeskDevicesDock().incomingOfferSettingsRequested();

  QVERIFY(dedicatedDialog);
  QVERIFY(accepted);
  const auto applied = runtime->incomingOffers().settings();
  QCOMPARE(applied.destinationRoot, QDir::cleanPath(receiveRoot));
  QVERIFY(applied.autoAcceptTrustedDevices);
  QCOMPARE(applied.defaultConflictPolicy, ::relaydesk::transfer::ConflictPolicy::Overwrite);

  QSettings settings(Settings::settingsFile(), QSettings::IniFormat);
  const auto persisted = ::relaydesk::transfer::TransferSettingsStore(settings).load();
  QVERIFY2(persisted.ok, qPrintable(persisted.diagnostic));
  QCOMPARE(persisted.settings.receiveRoot, QDir::cleanPath(receiveRoot));
  QCOMPARE(persisted.settings.incomingPolicy, ::relaydesk::transfer::IncomingTransferPolicy::AutoAcceptTrusted);
  QCOMPARE(persisted.settings.defaultConflictPolicy, ::relaydesk::transfer::ConflictPolicy::Overwrite);
}

void MainWindowLayoutTests::transferRuntimeIsDestroyedBeforeReferencedDependencies()
{
  auto window = std::make_unique<MainWindow>();
  auto *transfer = window->findChild<deskflow::relaydesk::TransferRuntimeComposition *>();
  auto *pairing = window->findChild<deskflow::relaydesk::PairingTrustRuntime *>();
  auto *discovery = window->findChild<deskflow::relaydesk::DeviceDiscoveryRuntime *>();
  QVERIFY(transfer != nullptr);
  QVERIFY(pairing != nullptr);
  QVERIFY(discovery != nullptr);

  const QPointer<deskflow::relaydesk::PairingTrustRuntime> pairingGuard(pairing);
  const QPointer<deskflow::relaydesk::DeviceDiscoveryRuntime> discoveryGuard(discovery);
  bool dependenciesAliveWhenTransferDestroyed = false;
  connect(transfer, &QObject::destroyed, this, [&] {
    dependenciesAliveWhenTransferDestroyed = !pairingGuard.isNull() && !discoveryGuard.isNull();
  });

  window.reset();
  QVERIFY(dependenciesAliveWhenTransferDestroyed);
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

void MainWindowLayoutTests::trayIconLoadsEmbeddedWindowsFallback()
{
  MainWindow window;
  auto *tray = window.findChild<QSystemTrayIcon *>();
  QVERIFY(tray != nullptr);
  QVERIFY(!tray->icon().isNull());
}

void MainWindowLayoutTests::chineseProductChromeUsesLocalizedText()
{
  I18N::reDetectLanguages();
  I18N::setLanguage(QStringLiteral("zh_CN"));
  QCOMPARE(I18N::currentLanguage(), QStringLiteral("zh_CN"));

  MainWindow window;
  auto *aboutAction = findMenuRoleAction(window, QAction::AboutRole);
  QVERIFY(aboutAction != nullptr);
  auto *tray = window.findChild<QSystemTrayIcon *>();
  QVERIFY(tray != nullptr);
  QVERIFY(tray->contextMenu() != nullptr);
  QVERIFY(!tray->contextMenu()->actions().isEmpty());
  QCOMPARE(tray->contextMenu()->actions().constFirst()->text(), QStringLiteral("打开 RelayDesk(&O)"));

  bool inspected = false;
  QString actualTitle;
  QString actualDescription;
  NextDialogInteractor aboutDialog(window, [&](QDialog *baseDialog) {
    auto *dialog = qobject_cast<AboutDialog *>(baseDialog);
    if (dialog == nullptr) {
      baseDialog->reject();
      return;
    }
    auto *description = dialog->findChild<QLabel *>(QStringLiteral("lblDescription"));
    inspected = description != nullptr;
    actualTitle = dialog->windowTitle();
    actualDescription = description == nullptr ? QString{} : description->text();
    baseDialog->reject();
  });
  aboutAction->trigger();
  QVERIFY(inspected);
  QCOMPARE(actualTitle, QStringLiteral("关于 RelayDesk"));
  QCOMPARE(actualDescription, QStringLiteral("局域网键盘、鼠标、剪贴板和文件共享"));

  I18N::setLanguage(QStringLiteral("en"));
}

void MainWindowLayoutTests::manualAddressesPersistAndStartTargetedDiscovery()
{
  QSettings initialSettings(Settings::settingsFile(), QSettings::IniFormat);
  deskflow::relaydesk::DiscoverySettingsStore initialStore(initialSettings);
  QString diagnostic;
  QVERIFY2(initialStore.save({.enabled = false}, &diagnostic), qPrintable(diagnostic));

  const auto portableSettings = Settings::portableSettingsFile();
  QVERIFY(QDir().mkpath(QFileInfo(portableSettings).absolutePath()));
  QFile portableMarker(portableSettings);
  QVERIFY(portableMarker.open(QIODevice::WriteOnly));
  portableMarker.close();
  const auto removePortableMarker = qScopeGuard([portableSettings] { QFile::remove(portableSettings); });

  MainWindow window;
  auto *discovery = window.findChild<deskflow::relaydesk::DeviceDiscoveryRuntime *>();
  QVERIFY(discovery != nullptr);
  QVERIFY(!discovery->isRunning());
  QSignalSpy started(&discovery->service(), &deskflow::relaydesk::DiscoveryService::started);
  window.open(false);

  auto &dock = window.relayDeskDevicesDock();
  auto *manage = dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);
  bool added = false;
  NextDialogInteractor addAddress(dock, [&](QDialog *dialog) {
    if (dialog->objectName() != QStringLiteral("relaydeskManualAddressesDialog")) {
      dialog->reject();
      return;
    }
    auto *host = dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskManualAddressHost"));
    auto *inputPort = dialog->findChild<QSpinBox *>(QStringLiteral("relaydeskManualAddressInputPort"));
    auto *filePort = dialog->findChild<QSpinBox *>(QStringLiteral("relaydeskManualAddressFilePort"));
    auto *add = dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressAddButton"));
    auto *save = dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"));
    if (host == nullptr || inputPort == nullptr || filePort == nullptr || add == nullptr || save == nullptr) {
      dialog->reject();
      return;
    }
    host->setText(QStringLiteral("127.0.0.1"));
    inputPort->setValue(24910);
    filePort->setValue(24911);
    QTest::mouseClick(add, Qt::LeftButton);
    QTest::mouseClick(save, Qt::LeftButton);
    added = dialog->result() == QDialog::Accepted;
  });
  QTest::mouseClick(manage, Qt::LeftButton);
  QTRY_VERIFY(added);
  QTRY_VERIFY(discovery->isRunning());
  QCOMPARE(started.count(), 1);

  QSettings savedSettings(Settings::settingsFile(), QSettings::IniFormat);
  deskflow::relaydesk::DiscoverySettingsStore savedStore(savedSettings);
  const auto saved = savedStore.load();
  QVERIFY2(saved.ok, qPrintable(saved.diagnostic));
  QCOMPARE(saved.settings.enabled, false);
  const QList<deskflow::relaydesk::ManualAddress> expectedAddresses{
      {.host = QStringLiteral("127.0.0.1"), .inputPort = 24910, .filePort = 24911},
  };
  QCOMPARE(saved.settings.manualAddresses, expectedAddresses);

  bool removed = false;
  NextDialogInteractor removeAddress(dock, [&](QDialog *dialog) {
    if (dialog->objectName() != QStringLiteral("relaydeskManualAddressesDialog")) {
      dialog->reject();
      return;
    }
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    auto *remove = dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressRemoveButton"));
    auto *save = dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"));
    if (addresses == nullptr || remove == nullptr || save == nullptr || addresses->count() != 1) {
      dialog->reject();
      return;
    }
    QCOMPARE(addresses->item(0)->text(), QStringLiteral("127.0.0.1  24910 / 24911"));
    addresses->setCurrentRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    QTest::mouseClick(save, Qt::LeftButton);
    removed = dialog->result() == QDialog::Accepted;
  });
  QTest::mouseClick(manage, Qt::LeftButton);
  QTRY_VERIFY(removed);
  QVERIFY(discovery->isRunning());
  QCOMPARE(started.count(), 1);

  const auto cleared = savedStore.load();
  QVERIFY2(cleared.ok, qPrintable(cleared.diagnostic));
  QVERIFY(cleared.settings.manualAddresses.isEmpty());
}

void MainWindowLayoutTests::trustedDeviceCardActionsPersistAndRevoke()
{
  const auto peerId = deskflow::relaydesk::DeviceId::generate();
  const QByteArray fingerprint(32, '\x6a');
  const auto trustPath = m_directory->filePath(QStringLiteral("relaydesk/trusted-devices.json"));
  deskflow::relaydesk::TrustedDeviceStore seeded(trustPath);
  QVERIFY(seeded.upsert({
      .deviceId = peerId,
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = fingerprint,
  }));
  QVERIFY(seeded.save().ok);

  const auto portableSettings = Settings::portableSettingsFile();
  QVERIFY(QDir().mkpath(QFileInfo(portableSettings).absolutePath()));
  QFile portableMarker(portableSettings);
  QVERIFY(portableMarker.open(QIODevice::WriteOnly));
  portableMarker.close();
  const auto removePortableMarker = qScopeGuard([portableSettings] { QFile::remove(portableSettings); });

  MainWindow window;
  auto *discovery = window.findChild<deskflow::relaydesk::DeviceDiscoveryRuntime *>();
  auto *pairing = window.findChild<deskflow::relaydesk::PairingTrustRuntime *>();
  QVERIFY(discovery != nullptr);
  QVERIFY(pairing != nullptr);
  QVERIFY(discovery->registry().observeAdvertisement(
      {.deviceId = peerId,
       .displayName = QStringLiteral("Loopback peer"),
       .platform = QStringLiteral("windows"),
       .architecture = QStringLiteral("x86_64"),
       .appVersion = QStringLiteral("1.26.0"),
       .inputPort = 24800,
       .filePort = 24801,
       .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
       .certificateFingerprintSha256 = fingerprint},
      QHostAddress::LocalHost
  ));
  window.open(false);

  auto &dock = window.relayDeskDevicesDock();
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *more = dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  auto *menu = dock.findChild<QMenu *>(QStringLiteral("relaydeskDeviceMoreMenu"));
  auto *autoAccept = dock.findChild<QAction *>(QStringLiteral("relaydeskAutoAcceptFilesMenuAction"));
  auto *revoke = dock.findChild<QAction *>(QStringLiteral("relaydeskRevokeTrustMenuAction"));
  QVERIFY(list != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(menu != nullptr);
  QVERIFY(autoAccept != nullptr);
  QVERIFY(revoke != nullptr);
  const auto index = window.relayDeskDeviceModel().index(window.relayDeskDeviceModel().indexOf(peerId), 0);
  QVERIFY(index.isValid());
  list->setCurrentIndex(index);
  QTRY_VERIFY(more->isVisible() && autoAccept->isEnabled() && revoke->isEnabled());

  bool autoAcceptClicked = false;
  QTimer::singleShot(0, menu, [&] {
    autoAcceptClicked = menu->isVisible() && menu->actionGeometry(autoAccept).isValid();
    if (autoAcceptClicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(autoAccept).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(autoAcceptClicked);
  QTRY_VERIFY(pairing->trustedDevices().find(peerId)->autoAcceptFiles);
  QTRY_VERIFY(window.relayDeskDeviceModel().snapshot(peerId)->autoAcceptFiles);
  deskflow::relaydesk::TrustedDeviceStore updated(trustPath);
  QVERIFY(updated.load().ok);
  QVERIFY(updated.find(peerId)->autoAcceptFiles);

  bool revokeConfirmed = false;
  NextDialogInteractor confirmation(dock, [&](QDialog *dialog) {
    auto *confirm = dialog->objectName() == QStringLiteral("relaydeskRevokeTrustConfirmation")
                        ? dialog->findChild<QPushButton *>(QStringLiteral("relaydeskRevokeTrustConfirmButton"))
                        : nullptr;
    if (confirm != nullptr) {
      revokeConfirmed = true;
      QTest::mouseClick(confirm, Qt::LeftButton);
    }
  });
  bool revokeClicked = false;
  QTimer::singleShot(0, menu, [&] {
    revokeClicked = menu->isVisible() && menu->actionGeometry(revoke).isValid();
    if (revokeClicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(revoke).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(revokeClicked && revokeConfirmed);
  QTRY_VERIFY(pairing->trustedDevices().find(peerId)->revoked);
  const auto snapshot = window.relayDeskDeviceModel().snapshot(peerId);
  QVERIFY(snapshot.has_value());
  QCOMPARE(snapshot->presence, deskflow::relaydesk::DevicePresence::TrustViolation);
  QVERIFY(!snapshot->trusted);
  QVERIFY(!snapshot->autoAcceptFiles);
  QTRY_VERIFY(!more->isVisible() && !autoAccept->isEnabled() && !revoke->isEnabled());
  QVERIFY(updated.load().ok);
  QVERIFY(updated.find(peerId)->revoked);
  QVERIFY(!updated.find(peerId)->autoAcceptFiles);

  QFile check(trustPath);
  QVERIFY(check.open(QIODevice::ReadOnly));
  const auto bytesBeforeRepeat = check.readAll();
  check.close();
  QTest::mouseClick(more, Qt::LeftButton);
  QCoreApplication::processEvents();
  QVERIFY(check.open(QIODevice::ReadOnly));
  QCOMPARE(check.readAll(), bytesBeforeRepeat);
}

void MainWindowLayoutTests::trustCardPersistenceFailureShowsNonModalFeedback()
{
  const auto peerId = deskflow::relaydesk::DeviceId::generate();
  const QByteArray fingerprint(32, '\x6b');
  const auto trustPath = m_directory->filePath(QStringLiteral("relaydesk/trusted-devices.json"));
  deskflow::relaydesk::TrustedDeviceStore seeded(trustPath);
  QVERIFY(seeded.upsert({
      .deviceId = peerId,
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = fingerprint,
      .autoAcceptFiles = true,
  }));
  QVERIFY(seeded.save().ok);

  const auto portableSettings = Settings::portableSettingsFile();
  QVERIFY(QDir().mkpath(QFileInfo(portableSettings).absolutePath()));
  QFile portableMarker(portableSettings);
  QVERIFY(portableMarker.open(QIODevice::WriteOnly));
  portableMarker.close();
  const auto removePortableMarker = qScopeGuard([portableSettings] { QFile::remove(portableSettings); });

  MainWindow window;
  auto *discovery = window.findChild<deskflow::relaydesk::DeviceDiscoveryRuntime *>();
  auto *pairing = window.findChild<deskflow::relaydesk::PairingTrustRuntime *>();
  QVERIFY(discovery != nullptr);
  QVERIFY(pairing != nullptr);
  QVERIFY(discovery->registry().observeAdvertisement(
      {.deviceId = peerId,
       .displayName = QStringLiteral("Loopback peer"),
       .platform = QStringLiteral("windows"),
       .architecture = QStringLiteral("x86_64"),
       .appVersion = QStringLiteral("1.26.0"),
       .inputPort = 24800,
       .filePort = 24801,
       .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
       .certificateFingerprintSha256 = fingerprint},
      QHostAddress::LocalHost
  ));
  window.open(false);

  auto &dock = window.relayDeskDevicesDock();
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *more = dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  auto *menu = dock.findChild<QMenu *>(QStringLiteral("relaydeskDeviceMoreMenu"));
  auto *autoAccept = dock.findChild<QAction *>(QStringLiteral("relaydeskAutoAcceptFilesMenuAction"));
  auto *revoke = dock.findChild<QAction *>(QStringLiteral("relaydeskRevokeTrustMenuAction"));
  auto *feedback = dock.findChild<QLabel *>(QStringLiteral("relaydeskSendFeedback"));
  QVERIFY(list != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(menu != nullptr);
  QVERIFY(autoAccept != nullptr);
  QVERIFY(revoke != nullptr);
  QVERIFY(feedback != nullptr);
  const auto index = window.relayDeskDeviceModel().index(window.relayDeskDeviceModel().indexOf(peerId), 0);
  QVERIFY(index.isValid());
  list->setCurrentIndex(index);
  QTRY_VERIFY(more->isVisible() && autoAccept->isEnabled() && autoAccept->isChecked() && revoke->isEnabled());

  QVERIFY(QFile::remove(trustPath));
  QVERIFY(QDir().mkpath(trustPath));

  bool revokeConfirmed = false;
  NextDialogInteractor confirmation(dock, [&](QDialog *dialog) {
    auto *confirm = dialog->objectName() == QStringLiteral("relaydeskRevokeTrustConfirmation")
                        ? dialog->findChild<QPushButton *>(QStringLiteral("relaydeskRevokeTrustConfirmButton"))
                        : nullptr;
    if (confirm != nullptr) {
      revokeConfirmed = true;
      QTest::mouseClick(confirm, Qt::LeftButton);
    }
  });
  bool revokeClicked = false;
  QTimer::singleShot(0, menu, [&] {
    revokeClicked = menu->isVisible() && menu->actionGeometry(revoke).isValid();
    if (revokeClicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(revoke).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(revokeClicked && revokeConfirmed);

  QTRY_VERIFY(feedback->isVisible());
  QCOMPARE(feedback->text(), QStringLiteral("Could not update device trust. Try again."));
  QVERIFY(!feedback->text().contains(trustPath));
  QVERIFY(!feedback->text().contains(QStringLiteral("backup"), Qt::CaseInsensitive));
  QVERIFY(!pairing->trustedDevices().find(peerId)->revoked);
  QVERIFY(pairing->trustedDevices().find(peerId)->autoAcceptFiles);
  const auto snapshot = window.relayDeskDeviceModel().snapshot(peerId);
  QVERIFY(snapshot.has_value());
  QVERIFY(snapshot->trusted);
  QVERIFY(snapshot->autoAcceptFiles);
  QCOMPARE(snapshot->presence, deskflow::relaydesk::DevicePresence::Online);
  deskflow::relaydesk::TrustedDeviceStore reloaded(trustPath);
  QVERIFY(reloaded.load().ok);
  QVERIFY(!reloaded.find(peerId)->revoked);
  QVERIFY(reloaded.find(peerId)->autoAcceptFiles);

  revokeConfirmed = false;
  NextDialogInteractor repeatConfirmation(dock, [&](QDialog *dialog) {
    auto *confirm = dialog->objectName() == QStringLiteral("relaydeskRevokeTrustConfirmation")
                        ? dialog->findChild<QPushButton *>(QStringLiteral("relaydeskRevokeTrustConfirmButton"))
                        : nullptr;
    if (confirm != nullptr) {
      revokeConfirmed = true;
      QTest::mouseClick(confirm, Qt::LeftButton);
    }
  });
  revokeClicked = false;
  QTimer::singleShot(0, menu, [&] {
    revokeClicked = menu->isVisible() && menu->actionGeometry(revoke).isValid();
    if (revokeClicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(revoke).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(revokeClicked && revokeConfirmed);
  QVERIFY(!pairing->trustedDevices().find(peerId)->revoked);

  QDir blockedPrimary(trustPath);
  QVERIFY(blockedPrimary.removeRecursively());
  bool autoAcceptClicked = false;
  QTimer::singleShot(0, menu, [&] {
    autoAcceptClicked = menu->isVisible() && menu->actionGeometry(autoAccept).isValid();
    if (autoAcceptClicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(autoAccept).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(autoAcceptClicked);
  QTRY_VERIFY(!feedback->isVisible());
  QVERIFY(!pairing->trustedDevices().find(peerId)->autoAcceptFiles);
  QVERIFY(reloaded.load().ok);
  QVERIFY(!reloaded.find(peerId)->autoAcceptFiles);
}

void MainWindowLayoutTests::autoAcceptPrimaryWriteFailureShowsNonModalFeedback()
{
  const auto peerId = deskflow::relaydesk::DeviceId::generate();
  const QByteArray fingerprint(32, '\x6c');
  const auto trustPath = m_directory->filePath(QStringLiteral("relaydesk/trusted-devices.json"));
  deskflow::relaydesk::TrustedDeviceStore seeded(trustPath);
  QVERIFY(seeded.upsert(
      {.deviceId = peerId,
       .alias = QStringLiteral("Loopback peer"),
       .platform = QStringLiteral("windows"),
       .fingerprintSha256 = fingerprint,
       .autoAcceptFiles = true}
  ));
  QVERIFY(seeded.save().ok);
  const auto portableSettings = Settings::portableSettingsFile();
  QVERIFY(QDir().mkpath(QFileInfo(portableSettings).absolutePath()));
  QFile portableMarker(portableSettings);
  QVERIFY(portableMarker.open(QIODevice::WriteOnly));
  portableMarker.close();
  const auto removePortableMarker = qScopeGuard([portableSettings] { QFile::remove(portableSettings); });
  MainWindow window;
  auto *discovery = window.findChild<deskflow::relaydesk::DeviceDiscoveryRuntime *>();
  auto *pairing = window.findChild<deskflow::relaydesk::PairingTrustRuntime *>();
  QVERIFY(discovery != nullptr);
  QVERIFY(pairing != nullptr);
  QVERIFY(discovery->registry().observeAdvertisement(
      {.deviceId = peerId,
       .displayName = QStringLiteral("Loopback peer"),
       .platform = QStringLiteral("windows"),
       .architecture = QStringLiteral("x86_64"),
       .appVersion = QStringLiteral("1.26.0"),
       .inputPort = 24800,
       .filePort = 24801,
       .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
       .certificateFingerprintSha256 = fingerprint},
      QHostAddress::LocalHost
  ));
  window.open(false);
  auto &dock = window.relayDeskDevicesDock();
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *more = dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  auto *menu = dock.findChild<QMenu *>(QStringLiteral("relaydeskDeviceMoreMenu"));
  auto *action = dock.findChild<QAction *>(QStringLiteral("relaydeskAutoAcceptFilesMenuAction"));
  auto *feedback = dock.findChild<QLabel *>(QStringLiteral("relaydeskSendFeedback"));
  QVERIFY(list != nullptr && more != nullptr && menu != nullptr && action != nullptr && feedback != nullptr);
  list->setCurrentIndex(window.relayDeskDeviceModel().index(window.relayDeskDeviceModel().indexOf(peerId), 0));
  QTRY_VERIFY(more->isVisible() && action->isChecked());
  QVERIFY(QFile::remove(trustPath));
  QVERIFY(QDir().mkpath(trustPath));
  bool modalSeen = false;
  NextDialogInteractor modalWatcher(window, [&modalSeen](QDialog *dialog) {
    modalSeen = true;
    dialog->reject();
  });
  bool clicked = false;
  QTimer::singleShot(0, menu, [&] {
    clicked = menu->isVisible() && menu->actionGeometry(action).isValid();
    if (clicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(clicked);
  QVERIFY2(
      !modalSeen && feedback->isVisible(), qPrintable(QStringLiteral("modalSeen=%1 feedbackVisible=%2 feedbackText=%3")
                                                          .arg(modalSeen)
                                                          .arg(feedback->isVisible())
                                                          .arg(feedback->text()))
  );
  QCOMPARE(feedback->text(), QStringLiteral("Could not update device trust. Try again."));
  QVERIFY(!feedback->text().contains(trustPath));
  QVERIFY(pairing->trustedDevices().find(peerId)->autoAcceptFiles);
  QVERIFY(window.relayDeskDeviceModel().snapshot(peerId)->autoAcceptFiles);
  deskflow::relaydesk::TrustedDeviceStore reloaded(trustPath);
  QVERIFY(reloaded.load().ok);
  QVERIFY(reloaded.find(peerId)->autoAcceptFiles);
  QVERIFY(QDir(trustPath).removeRecursively());
  clicked = false;
  QTimer::singleShot(0, menu, [&] {
    clicked = menu->isVisible() && menu->actionGeometry(action).isValid();
    if (clicked)
      QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
  });
  QTest::mouseClick(more, Qt::LeftButton);
  QTRY_VERIFY(clicked && !feedback->isVisible());
  QVERIFY(!pairing->trustedDevices().find(peerId)->autoAcceptFiles);
  QVERIFY(reloaded.load().ok);
  QVERIFY(!reloaded.find(peerId)->autoAcceptFiles);
}

QTEST_MAIN(MainWindowLayoutTests)

#include "MainWindowLayoutTests.moc"
