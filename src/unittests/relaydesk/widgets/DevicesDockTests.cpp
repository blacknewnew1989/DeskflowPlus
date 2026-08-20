/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/DevicesDock.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"

#include "../FakePairingService.h"

#include <QCoreApplication>
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTimer>

#include <chrono>
#include <utility>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::test;
using namespace deskflow::relaydesk::widgets;

namespace {

DeviceSnapshot peerSnapshot(DevicePresence presence = DevicePresence::Discovered, bool trusted = false)
{
  return {
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .alias = QStringLiteral("Design Mac"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .presence = presence,
      .trusted = trusted,
      .latencyMs = 3,
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
  };
}

PairingSnapshot pairingSnapshot(
    DeviceSnapshot peer, PairingState state, QString sas = {},
    PairingFailureReason failureReason = PairingFailureReason::None
)
{
  peer.presence = DevicePresence::Pairing;
  return {
      .pairingSessionId = QUuid::createUuid(),
      .peer = std::move(peer),
      .state = state,
      .sixDigitSas = std::move(sas),
      .expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(60),
      .attemptsRemaining = 3,
      .failureReason = failureReason,
  };
}

::relaydesk::transfer::IncomingOffer incomingTransfer(bool trusted = true)
{
  using namespace ::relaydesk::transfer;
  return {
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("<b>Studio Mac</b>"),
      .offer =
          {
              .transferId = TransferId::generate(),
              .displayName = QStringLiteral("<img src=x> Project"),
              .totalBytes = 4096,
              .fileCount = 2,
              .directoryCount = 1,
              .manifestSha256 = QByteArray(kSha256Bytes, '\x2a'),
              .manifestPageCount = 1,
              .requestedConflictPolicy = ConflictPolicy::AutoRename,
          },
      .peerTrusted = trusted,
      .mayAutoAccept = false,
  };
}

struct Fixture
{
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingService};
  PermissionStatusModel permissions{PermissionPlatform::Other};
  DevicesDock dock{devices, pairing, permissions};
  QByteArray fingerprint = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
};

} // namespace

class DevicesDockTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void rendersEmptyAndPairableDeviceStates();
  void keepsLocalDeviceInFooterOnly();
  void emitsTypedPairingIntent();
  void emitsInputLayoutIntentForTrustedInputPeer();
  void emitsTrustRevocationIntentForTrustedDevice();
  void rejectsTrustRevocationIntentForUntrustedDevice();
  void activatingCardRunsItsVisiblePrimaryAction();
  void rendersAndDrivesSharedPairingModel();
  void confirmsAndCancelsFromPairingPanel();
  void rendersExpiredPairingState();
  void rendersPermissionGuidanceAndKeyboardAction();
  void rendersMacPermissionDetailsAndIndependentActions();
  void gatesPairingOnMacLocalNetworkPermission();
  void keepsFileActionsEnabledWhenOnlyMacInputPermissionsAreMissing();
  void choosesFilesAndFolderAndPublishesImmutableIntent();
  void rejectsInvalidOrIneligibleSendItems();
  void acceptsLocalUrlDropOnlyForEligiblePeer();
  void rendersNonBlockingIncomingOfferAndKeyboardDecisions();
  void rendersAndResolvesQueuedIncomingConflicts();
  void rendersUntrustedAndExpiredOfferSafely();
  void managesManualAddressesAtCompactSize();
  void retriesManualAddressSaveAfterFailure();
  void cancellingManualAddressChangesDoesNotSaveOrCommit();
  void removingManualAddressEditsWorkingCopyUntilSaved();
  void duplicateManualAddressesAreNormalizedBeforeSave();
  void labelsManualAddressInputsAtCompactSize();
};

void DevicesDockTests::rendersEmptyAndPairableDeviceStates()
{
  Fixture fixture;
  fixture.dock.resize(532, 300);
  fixture.dock.show();
  QCoreApplication::processEvents();

  auto *empty = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskDevicesEmptyLabel"));
  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pair = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  QVERIFY(empty != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(pair != nullptr);
  QVERIFY(fixture.dock.minimumWidth() <= 532);
  QVERIFY(empty->isVisible());
  QVERIFY(!list->isVisible());
  QVERIFY(!pair->isVisible());
  QCOMPARE(empty->text(), QStringLiteral("Nearby devices will appear here"));

  const auto peer = peerSnapshot();
  fixture.devices.upsertRemoteDevice(peer);
  QTRY_VERIFY(list->isVisible());
  QVERIFY(!empty->isVisible());
  list->setCurrentIndex(fixture.devices.index(0, 0));
  QTRY_VERIFY(pair->isEnabled());
  QCOMPARE(pair->text(), QStringLiteral("Pair"));
  const auto rowHeight = list->sizeHintForRow(0);
  QVERIFY2(rowHeight >= 64 && rowHeight <= 72, qPrintable(QStringLiteral("device row height: %1").arg(rowHeight)));

  auto changedPeer = peer;
  changedPeer.presence = DevicePresence::TrustViolation;
  changedPeer.trusted = true;
  fixture.devices.upsertRemoteDevice(changedPeer);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(pair->isEnabled());
  QCOMPARE(pair->text(), QStringLiteral("Pair again"));
}

void DevicesDockTests::managesManualAddressesAtCompactSize()
{
  qRegisterMetaType<ManualAddress>();
  Fixture fixture;
  fixture.dock.resize(280, 380);
  fixture.dock.show();
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);
  QVERIFY(manage->isVisible());
  int saveRequests = 0;
  QList<ManualAddress> savedAddresses;
  connect(
      &fixture.dock, &DevicesDock::manualAddressesSaveRequested, this,
      [&saveRequests, &savedAddresses](
          QList<ManualAddress> addresses, const DevicesDock::ManualAddressesSaveReceipt &receipt
      ) {
        ++saveRequests;
        savedAddresses = std::move(addresses);
        receipt(true);
      }
  );
  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    if (dialog == nullptr) return;
    dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskManualAddressHost"))->setText(QStringLiteral("192.168.1.20"));
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressAddButton"))->click();
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"))->click();
  });
  manage->click();
  QCOMPARE(saveRequests, 1);
  QCOMPARE(savedAddresses.size(), 1);
  fixture.devices.upsertRemoteDevice(peerSnapshot(DevicePresence::Online, true));
  QVERIFY(manage->isVisible());
}

void DevicesDockTests::retriesManualAddressSaveAfterFailure()
{
  Fixture fixture;
  fixture.dock.resize(520, 380);
  fixture.dock.show();
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);

  int saveRequests = 0;
  connect(
      &fixture.dock, &DevicesDock::manualAddressesSaveRequested, this,
      [&saveRequests](QList<ManualAddress>, const DevicesDock::ManualAddressesSaveReceipt &receipt) {
        ++saveRequests;
        receipt(saveRequests == 2);
      }
  );
  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskManualAddressHost"))->setText(QStringLiteral("retry.local"));
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressAddButton"))->click();
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"))->click();
    QVERIFY(dialog->isVisible());
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    QVERIFY(addresses != nullptr);
    QCOMPARE(addresses->count(), 1);
    QCOMPARE(
        dialog->findChild<QLabel *>(QStringLiteral("relaydeskManualAddressError"))->text(),
        QStringLiteral("Could not save manual addresses. Try again.")
    );
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"))->click();
  });
  manage->click();
  QCOMPARE(saveRequests, 2);
}

void DevicesDockTests::cancellingManualAddressChangesDoesNotSaveOrCommit()
{
  Fixture fixture;
  fixture.dock.setManualAddresses({*parseManualAddress(QStringLiteral("kept.local"))});
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);

  int saveRequests = 0;
  connect(
      &fixture.dock, &DevicesDock::manualAddressesSaveRequested, this,
      [&saveRequests](QList<ManualAddress>, const DevicesDock::ManualAddressesSaveReceipt &) { ++saveRequests; }
  );
  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskManualAddressHost"))->setText(QStringLiteral("discarded.local"));
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressAddButton"))->click();
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressCancelButton"))->click();
  });
  manage->click();
  QCOMPARE(saveRequests, 0);

  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    QVERIFY(addresses != nullptr);
    QCOMPARE(addresses->count(), 1);
    QCOMPARE(addresses->item(0)->text(), QStringLiteral("kept.local  24800 / 24801"));
    dialog->reject();
  });
  manage->click();
}

void DevicesDockTests::removingManualAddressEditsWorkingCopyUntilSaved()
{
  Fixture fixture;
  fixture.dock.setManualAddresses(
      {*parseManualAddress(QStringLiteral("first.local")), *parseManualAddress(QStringLiteral("second.local"))}
  );
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);
  int saveRequests = 0;
  QList<ManualAddress> savedAddresses;
  connect(
      &fixture.dock, &DevicesDock::manualAddressesSaveRequested, this,
      [&saveRequests, &savedAddresses](
          QList<ManualAddress> addresses, const DevicesDock::ManualAddressesSaveReceipt &receipt
      ) {
        ++saveRequests;
        savedAddresses = std::move(addresses);
        receipt(true);
      }
  );

  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    addresses->setCurrentRow(0);
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressRemoveButton"))->click();
    dialog->reject();
  });
  manage->click();

  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    QCOMPARE(addresses->count(), 2);
    addresses->setCurrentRow(0);
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressRemoveButton"))->click();
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"))->click();
  });
  manage->click();
  QCOMPARE(saveRequests, 1);
  QCOMPARE(savedAddresses, QList<ManualAddress>{*parseManualAddress(QStringLiteral("second.local"))});

  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    auto *addresses = dialog->findChild<QListWidget *>(QStringLiteral("relaydeskManualAddressesList"));
    QCOMPARE(addresses->count(), 1);
    QCOMPARE(addresses->item(0)->text(), QStringLiteral("second.local  24800 / 24801"));
    dialog->reject();
  });
  manage->click();
}

void DevicesDockTests::duplicateManualAddressesAreNormalizedBeforeSave()
{
  Fixture fixture;
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);
  QList<ManualAddress> savedAddresses;
  connect(
      &fixture.dock, &DevicesDock::manualAddressesSaveRequested, this,
      [&savedAddresses](QList<ManualAddress> addresses, const DevicesDock::ManualAddressesSaveReceipt &receipt) {
        savedAddresses = std::move(addresses);
        receipt(true);
      }
  );
  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    auto *host = dialog->findChild<QLineEdit *>(QStringLiteral("relaydeskManualAddressHost"));
    auto *add = dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressAddButton"));
    host->setText(QStringLiteral("HOST.local"));
    add->click();
    host->setText(QStringLiteral("host.local."));
    add->click();
    dialog->findChild<QPushButton *>(QStringLiteral("relaydeskManualAddressSaveButton"))->click();
  });
  manage->click();
  QCOMPARE(savedAddresses, QList<ManualAddress>{*parseManualAddress(QStringLiteral("host.local"))});
}

void DevicesDockTests::labelsManualAddressInputsAtCompactSize()
{
  Fixture fixture;
  fixture.dock.resize(520, 380);
  fixture.dock.show();
  auto *manage = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskManageManualAddressesButton"));
  QVERIFY(manage != nullptr);
  QTimer::singleShot(0, [&fixture]() {
    auto *dialog = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskManualAddressesDialog"));
    QVERIFY(dialog != nullptr);
    dialog->resize(520, 380);
    QCoreApplication::processEvents();
    const auto labeledControl = [dialog](const QString &labelName, const QString &controlName, const QString &text) {
      auto *label = dialog->findChild<QLabel *>(labelName);
      auto *control = dialog->findChild<QWidget *>(controlName);
      QVERIFY(label != nullptr);
      QVERIFY(control != nullptr);
      QVERIFY(label->isVisible());
      QVERIFY(control->isVisible());
      QCOMPARE(label->text(), text);
      QCOMPARE(label->buddy(), control);
      QCOMPARE(control->accessibleName(), text);
      QVERIFY(dialog->rect().contains(label->geometry()));
      QVERIFY(dialog->rect().contains(control->geometry()));
      QVERIFY(label->geometry().bottom() < control->geometry().top());
    };
    labeledControl(
        QStringLiteral("relaydeskManualAddressHostLabel"), QStringLiteral("relaydeskManualAddressHost"),
        QStringLiteral("Host")
    );
    labeledControl(
        QStringLiteral("relaydeskManualAddressInputPortLabel"),
        QStringLiteral("relaydeskManualAddressInputPort"), QStringLiteral("Input port")
    );
    labeledControl(
        QStringLiteral("relaydeskManualAddressFilePortLabel"),
        QStringLiteral("relaydeskManualAddressFilePort"), QStringLiteral("File port")
    );
    auto *host = dialog->findChild<QWidget *>(QStringLiteral("relaydeskManualAddressHost"));
    auto *inputPort = dialog->findChild<QWidget *>(QStringLiteral("relaydeskManualAddressInputPort"));
    auto *filePort = dialog->findChild<QWidget *>(QStringLiteral("relaydeskManualAddressFilePort"));
    QVERIFY(!host->geometry().intersects(inputPort->geometry()));
    QVERIFY(!host->geometry().intersects(filePort->geometry()));
    QVERIFY(!inputPort->geometry().intersects(filePort->geometry()));
    dialog->reject();
  });
  manage->click();
}

void DevicesDockTests::keepsLocalDeviceInFooterOnly()
{
  Fixture fixture;
  auto local = peerSnapshot(DevicePresence::Online, true);
  local.displayName = QStringLiteral("This PC");
  fixture.devices.setLocalDevice(local);
  fixture.dock.show();
  QCoreApplication::processEvents();

  auto *empty = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskDevicesEmptyLabel"));
  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  QVERIFY(empty != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(empty->isVisible());
  QVERIFY(!list->isVisible());
  QVERIFY(list->isRowHidden(fixture.devices.indexOf(local.id)));

  const auto peer = peerSnapshot(DevicePresence::Online, true);
  fixture.devices.upsertRemoteDevice(peer);
  QTRY_VERIFY(list->isVisible());
  QVERIFY(list->isRowHidden(fixture.devices.indexOf(local.id)));
  QVERIFY(!list->isRowHidden(fixture.devices.indexOf(peer.id)));
}

void DevicesDockTests::emitsTypedPairingIntent()
{
  qRegisterMetaType<DeviceId>();
  Fixture fixture;
  const auto peer = peerSnapshot();
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pair = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  QVERIFY(list != nullptr);
  QVERIFY(pair != nullptr);
  list->setCurrentIndex(fixture.devices.index(0, 0));

  QSignalSpy requested(&fixture.dock, &DevicesDock::pairingRequested);
  QTest::mouseClick(pair, Qt::LeftButton);
  QCOMPARE(requested.count(), 1);
  const auto arguments = requested.takeFirst();
  QCOMPARE(arguments.at(0).metaType(), QMetaType::fromType<DeviceId>());
  QCOMPARE(*static_cast<const DeviceId *>(arguments.at(0).constData()), peer.id);
  const auto storedPeer = fixture.devices.snapshot(peer.id);
  QVERIFY(storedPeer.has_value());
  QCOMPARE(storedPeer->id, peer.id);
  QCOMPARE(storedPeer->displayName, peer.displayName);
}

void DevicesDockTests::emitsInputLayoutIntentForTrustedInputPeer()
{
  qRegisterMetaType<DeviceId>();
  Fixture fixture;
  const auto peer = peerSnapshot(DevicePresence::Online, true);
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.resize(520, 380);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *configure = fixture.dock.findChild<QAction *>(QStringLiteral("relaydeskConfigureInputMenuAction"));
  auto *more = fixture.dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  QVERIFY(list != nullptr);
  QVERIFY(configure != nullptr);
  QVERIFY(more != nullptr);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(configure->isVisible());
  QVERIFY(configure->isEnabled());
  QVERIFY(more->isVisible());
  QVERIFY(more->geometry().right() <= fixture.dock.contentsRect().right());
  QCOMPARE(configure->text(), QStringLiteral("Arrange input"));

  QSignalSpy requested(&fixture.dock, &DevicesDock::inputLayoutRequested);
  configure->trigger();
  QCOMPARE(requested.count(), 1);
  QCOMPARE(*static_cast<const DeviceId *>(requested.takeFirst().at(0).constData()), peer.id);
}

void DevicesDockTests::emitsTrustRevocationIntentForTrustedDevice()
{
  qRegisterMetaType<DeviceId>();
  Fixture fixture;
  auto peer = peerSnapshot(DevicePresence::Online, true);
  peer.displayName = QStringLiteral("Studio Mac for the Long Device Name Layout Regression");
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.resize(520, 380);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *more = fixture.dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  auto *revoke = fixture.dock.findChild<QAction *>(QStringLiteral("relaydeskRevokeTrustMenuAction"));
  QVERIFY(list != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(revoke != nullptr);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(more->isVisible());
  QVERIFY(more->isEnabled());
  QVERIFY(more->width() <= 40);
  QCOMPARE(more->accessibleName(), QStringLiteral("More"));
  QCOMPARE(more->toolTip(), QStringLiteral("More"));
  QCOMPARE(revoke->text(), QStringLiteral("Revoke trust"));
  QVERIFY(revoke->isEnabled());

  QSignalSpy requested(&fixture.dock, &DevicesDock::trustRevocationRequested);
  bool confirmationSeen = false;
  QTimer::singleShot(0, [&fixture, &confirmationSeen]() {
    auto *confirmation = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskRevokeTrustConfirmation"));
    if (confirmation == nullptr)
      return;
    confirmationSeen = confirmation
                           ->findChild<QLabel *>(QStringLiteral("relaydeskRevokeTrustConfirmationMessage")) != nullptr;
    confirmation->reject();
  });
  revoke->trigger();
  QVERIFY(confirmationSeen);
  QCOMPARE(requested.count(), 0);

  QTimer::singleShot(0, [&fixture]() {
    auto *confirmation = fixture.dock.findChild<QDialog *>(QStringLiteral("relaydeskRevokeTrustConfirmation"));
    if (confirmation == nullptr)
      return;
    auto *confirm = confirmation->findChild<QPushButton *>(QStringLiteral("relaydeskRevokeTrustConfirmButton"));
    if (confirm != nullptr)
      confirm->click();
  });
  revoke->trigger();
  QCOMPARE(requested.count(), 1);
  QCOMPARE(*static_cast<const DeviceId *>(requested.first().at(0).constData()), peer.id);
}

void DevicesDockTests::rejectsTrustRevocationIntentForUntrustedDevice()
{
  Fixture fixture;
  const auto peer = peerSnapshot(DevicePresence::Online, false);
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *more = fixture.dock.findChild<QToolButton *>(QStringLiteral("relaydeskDeviceMoreButton"));
  auto *revoke = fixture.dock.findChild<QAction *>(QStringLiteral("relaydeskRevokeTrustMenuAction"));
  QVERIFY(list != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(revoke != nullptr);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(!more->isVisible());
  QVERIFY(!revoke->isEnabled());

  QSignalSpy requested(&fixture.dock, &DevicesDock::trustRevocationRequested);
  revoke->trigger();
  QCOMPARE(requested.count(), 0);
}

void DevicesDockTests::activatingCardRunsItsVisiblePrimaryAction()
{
  qRegisterMetaType<DeviceId>();
  Fixture fixture;
  auto peer = peerSnapshot();
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.resize(420, 520);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  QVERIFY(list != nullptr);
  auto index = fixture.devices.index(fixture.devices.indexOf(peer.id), 0);
  QVERIFY(index.isValid());
  QSignalSpy pairingRequested(&fixture.dock, &DevicesDock::pairingRequested);
  QVERIFY(QMetaObject::invokeMethod(list, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, index)));
  QCOMPARE(pairingRequested.count(), 1);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto filePath = directory.filePath(QStringLiteral("card-action.txt"));
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();
  const QList<QUrl> files{QUrl::fromLocalFile(filePath)};
  fixture.dock.setFileChooser([files](QWidget &) { return files; });

  peer.presence = DevicePresence::Online;
  peer.trusted = true;
  fixture.devices.upsertRemoteDevice(peer);
  index = fixture.devices.index(fixture.devices.indexOf(peer.id), 0);
  QSignalSpy sendRequested(&fixture.dock, &DevicesDock::sendItemsRequested);
  QVERIFY(QMetaObject::invokeMethod(list, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, index)));
  QCOMPARE(sendRequested.count(), 1);
  QCOMPARE(sendRequested.first().at(1).value<QList<QUrl>>(), files);
}

void DevicesDockTests::rendersAndDrivesSharedPairingModel()
{
  Fixture fixture;
  const auto peer = peerSnapshot();
  fixture.dock.show();
  const auto pairing = pairingSnapshot(peer, PairingState::AwaitingUserComparison, QStringLiteral("123456"));
  fixture.pairingService.publish(pairing, fixture.fingerprint);

  auto *panel = fixture.dock.findChild<QFrame *>(QStringLiteral("relaydeskPairingPanel"));
  auto *state = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingStateLabel"));
  auto *sas = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingSasLabel"));
  auto *entry = fixture.dock.findChild<QLineEdit *>(QStringLiteral("relaydeskPairingCodeEntry"));
  auto *submit = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSubmitPairingCodeButton"));
  auto *toggle = fixture.dock.findChild<QToolButton *>(QStringLiteral("relaydeskFingerprintToggle"));
  auto *details = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskFingerprintDetails"));
  QVERIFY(panel != nullptr);
  QVERIFY(state != nullptr);
  QVERIFY(sas != nullptr);
  QVERIFY(entry != nullptr);
  QVERIFY(submit != nullptr);
  QVERIFY(toggle != nullptr);
  QVERIFY(details != nullptr);
  QTRY_VERIFY(panel->isVisible());
  QCOMPARE(state->text(), QStringLiteral("Compare the code on both devices"));
  QCOMPARE(sas->text(), QStringLiteral("123 456"));
  QVERIFY(!details->isVisible());

  QTest::mouseClick(toggle, Qt::LeftButton);
  QVERIFY(details->isVisible());
  QCOMPARE(details->text(), fixture.pairing.fullFingerprint());

  entry->setText(QStringLiteral("123456"));
  QTRY_VERIFY(submit->isEnabled());
  QTest::mouseClick(submit, Qt::LeftButton);
  QCOMPARE(fixture.pairingService.lastSubmittedSession, pairing.pairingSessionId);
  QCOMPARE(fixture.pairingService.lastSubmittedSas, QStringLiteral("123456"));
  QCOMPARE(fixture.pairingService.snapshot()->state, PairingState::Confirming);
  QCOMPARE(state->text(), QStringLiteral("Confirming pairing"));
  QVERIFY(!entry->isVisible());
}

void DevicesDockTests::confirmsAndCancelsFromPairingPanel()
{
  Fixture fixture;
  fixture.dock.show();
  const auto pairing = pairingSnapshot(peerSnapshot(), PairingState::AwaitingUserComparison, QStringLiteral("123456"));
  fixture.pairingService.publish(pairing, fixture.fingerprint);

  auto *confirm = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskConfirmMatchingSasButton"));
  auto *cancel = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskCancelPairingButton"));
  QVERIFY(confirm != nullptr);
  QVERIFY(cancel != nullptr);
  QTRY_VERIFY(confirm->isVisible());
  QTest::mouseClick(confirm, Qt::LeftButton);
  QCOMPARE(fixture.pairingService.lastConfirmedSession, pairing.pairingSessionId);
  QCOMPARE(fixture.pairingService.snapshot()->state, PairingState::Confirming);
  QTRY_VERIFY(cancel->isVisible());
  QTest::mouseClick(cancel, Qt::LeftButton);
  QCOMPARE(fixture.pairingService.lastCancelledSession, pairing.pairingSessionId);
  QCOMPARE(fixture.pairingService.snapshot()->state, PairingState::Rejected);
  QVERIFY(!cancel->isVisible());
}

void DevicesDockTests::rendersExpiredPairingState()
{
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingService);
  PermissionStatusModel permissions(PermissionPlatform::Other);
  DevicesDock dock(devices, pairing, permissions);
  const auto fingerprint = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

  dock.show();
  auto snapshot = pairingSnapshot(peerSnapshot(), PairingState::AwaitingUserComparison, QStringLiteral("123456"));
  pairingService.publish(snapshot, fingerprint);
  auto *state = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingStateLabel"));
  auto *error = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingErrorLabel"));
  QVERIFY(state != nullptr);
  QVERIFY(error != nullptr);
  snapshot.state = PairingState::Expired;
  snapshot.failureReason = PairingFailureReason::Expired;
  pairingService.publish(snapshot, fingerprint);
  QCOMPARE(state->text(), QStringLiteral("The pairing code expired. Generate a new code."));
  QCOMPARE(error->text(), QStringLiteral("The pairing code expired. Generate a new code."));
  QVERIFY(error->isVisible());
}

void DevicesDockTests::rendersPermissionGuidanceAndKeyboardAction()
{
  qRegisterMetaType<PermissionKind>();
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingService);
  PermissionStatusModel permissions(PermissionPlatform::Windows);
  DevicesDock dock(devices, pairing, permissions);
  dock.resize(532, 300);
  dock.show();
  QCoreApplication::processEvents();

  auto *banner = dock.findChild<QFrame *>(QStringLiteral("relaydeskPermissionBanner"));
  auto *title = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionTitle"));
  auto *message = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionMessage"));
  auto *openSettings = dock.findChild<QPushButton *>(QStringLiteral("relaydeskOpenPermissionSettingsButton"));
  QVERIFY(banner != nullptr);
  QVERIFY(title != nullptr);
  QVERIFY(message != nullptr);
  QVERIFY(openSettings != nullptr);
  QVERIFY(banner->isVisible());
  QCOMPARE(title->text(), QStringLiteral("Permission status not checked"));
  QVERIFY(!title->wordWrap());
  QVERIFY(!message->wordWrap());
  QVERIFY(qobject_cast<QHBoxLayout *>(banner->layout()) != nullptr);
  QVERIFY(!message->isVisible());
  QVERIFY(!openSettings->isVisible());

  QVERIFY(permissions.setSnapshot({
      .platform = PermissionPlatform::Windows,
      .entries = {
          {
              .kind = PermissionKind::WindowsFirewall,
              .state = PermissionState::Denied,
              .errorCode = PermissionErrorCode::WindowsFirewallBlocked,
              .canOpenSettings = true,
              .diagnostic = QStringLiteral("<b>remote detail must stay hidden</b>"),
          },
          {
              .kind = PermissionKind::WindowsListeningPort,
              .state = PermissionState::Granted,
          },
      },
  }));
  QTRY_VERIFY(openSettings->isVisible());
  QCOMPARE(title->text(), QStringLiteral("Permission needed"));
  QCOMPARE(message->text(), QStringLiteral("Allow RelayDesk through Windows Firewall on private networks."));
  QVERIFY(!message->isVisible());
  QVERIFY(!message->text().contains(QStringLiteral("remote detail")));
  QCOMPARE(openSettings->text(), QStringLiteral("Open settings"));
  QCOMPARE(openSettings->accessibleName(), QStringLiteral("Open settings"));
  QVERIFY(openSettings->focusPolicy() != Qt::NoFocus);

  QSignalSpy requested(&permissions, &PermissionStatusModel::openSettingsRequested);
  openSettings->setFocus();
  QTest::keyClick(openSettings, Qt::Key_Space);
  QCOMPARE(requested.count(), 1);
  QCOMPARE(requested.takeFirst().constFirst().value<PermissionKind>(), PermissionKind::WindowsFirewall);

  QVERIFY(permissions.setSnapshot({
      .platform = PermissionPlatform::Windows,
      .entries = {
          {.kind = PermissionKind::WindowsFirewall, .state = PermissionState::Granted},
          {.kind = PermissionKind::WindowsListeningPort, .state = PermissionState::Granted},
      },
  }));
  QVERIFY(banner->isVisible());
  QCOMPARE(title->text(), QStringLiteral("Permissions ready"));
  QCOMPARE(message->text(), QStringLiteral("All required system permissions are ready."));
  QVERIFY(!message->isVisible());
  QVERIFY(!openSettings->isVisible());
}

void DevicesDockTests::rendersMacPermissionDetailsAndIndependentActions()
{
  qRegisterMetaType<PermissionKind>();
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingService);
  PermissionStatusModel permissions(PermissionPlatform::MacOS);
  DevicesDock dock(devices, pairing, permissions);
  QVERIFY(permissions.setSnapshot({
      .platform = PermissionPlatform::MacOS,
      .entries = {
          {
              .kind = PermissionKind::MacLocalNetwork,
              .state = PermissionState::Denied,
              .errorCode = PermissionErrorCode::MacLocalNetworkDenied,
              .canOpenSettings = true,
          },
          {
              .kind = PermissionKind::MacAccessibility,
              .state = PermissionState::Denied,
              .errorCode = PermissionErrorCode::MacAccessibilityDenied,
              .canOpenSettings = true,
          },
          {
              .kind = PermissionKind::MacInputMonitoring,
              .state = PermissionState::NeedsAction,
              .errorCode = PermissionErrorCode::MacInputMonitoringDenied,
              .canOpenSettings = true,
          },
      },
  }));
  dock.resize(560, 700);
  dock.show();

  auto *detailsToggle = dock.findChild<QToolButton *>(QStringLiteral("relaydeskPermissionDetailsButton"));
  auto *detailsPanel = dock.findChild<QDialog *>(QStringLiteral("relaydeskPermissionDetailsPanel"));
  QVERIFY(detailsToggle != nullptr);
  QVERIFY(detailsPanel != nullptr);
  QVERIFY(!detailsPanel->isVisible());
  QVERIFY(detailsPanel->isWindow());
  QCOMPARE(detailsToggle->accessibleName(), QStringLiteral("Details"));
  auto *summary = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionTitle"));
  QVERIFY(summary != nullptr);
  QTest::mouseClick(summary, Qt::LeftButton);
  QTRY_VERIFY(detailsPanel->isVisible());

  const QStringList titles{QStringLiteral("Local Network"), QStringLiteral("Accessibility"),
                           QStringLiteral("Input Monitoring")};
  const QStringList purposes{
      QStringLiteral("Find and connect to nearby devices on your local network."),
      QStringLiteral("Control keyboard and pointer input on this Mac."),
      QStringLiteral("Read global keyboard and pointer input to share with another device."),
  };
  const QStringList statuses{QStringLiteral("Blocked"), QStringLiteral("Blocked"), QStringLiteral("Action needed")};
  const QStringList capabilities{
      QStringLiteral("Nearby discovery and direct local connections"),
      QStringLiteral("Input control on this Mac"),
      QStringLiteral("Sharing input from this Mac"),
  };
  QList<QPushButton *> settingsButtons;
  for (int row = 0; row < 3; ++row) {
    auto *title = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionDetailTitle%1").arg(row));
    auto *purpose = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionDetailPurpose%1").arg(row));
    auto *status = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionDetailStatus%1").arg(row));
    auto *capability = dock.findChild<QLabel *>(QStringLiteral("relaydeskPermissionDetailCapability%1").arg(row));
    auto *settings = dock.findChild<QPushButton *>(QStringLiteral("relaydeskPermissionSettingsButton%1").arg(row));
    QVERIFY(title != nullptr);
    QVERIFY(purpose != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(capability != nullptr);
    QVERIFY(settings != nullptr);
    QCOMPARE(title->text(), titles.at(row));
    QCOMPARE(purpose->text(), purposes.at(row));
    QCOMPARE(status->text(), statuses.at(row));
    QCOMPARE(capability->text(), capabilities.at(row));
    QVERIFY(settings->isVisible());
    QCOMPARE(settings->text(), QStringLiteral("Open settings"));
    QVERIFY(settings->accessibleName().startsWith(titles.at(row)));
    settingsButtons.append(settings);
  }

  QSignalSpy requested(&permissions, &PermissionStatusModel::openSettingsRequested);
  for (auto *settings : std::as_const(settingsButtons))
    QTest::mouseClick(settings, Qt::LeftButton);
  QCOMPARE(requested.count(), 3);
  QCOMPARE(requested.at(0).constFirst().value<PermissionKind>(), PermissionKind::MacLocalNetwork);
  QCOMPARE(requested.at(1).constFirst().value<PermissionKind>(), PermissionKind::MacAccessibility);
  QCOMPARE(requested.at(2).constFirst().value<PermissionKind>(), PermissionKind::MacInputMonitoring);
}

void DevicesDockTests::keepsFileActionsEnabledWhenOnlyMacInputPermissionsAreMissing()
{
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingService);
  PermissionStatusModel permissions(PermissionPlatform::MacOS);
  DevicesDock dock(devices, pairing, permissions);
  auto peer = peerSnapshot(DevicePresence::Online, true);
  devices.upsertRemoteDevice(peer);
  QVERIFY(permissions.setSnapshot({
      .platform = PermissionPlatform::MacOS,
      .entries = {
          {.kind = PermissionKind::MacLocalNetwork, .state = PermissionState::Granted},
          {
              .kind = PermissionKind::MacAccessibility,
              .state = PermissionState::Denied,
              .errorCode = PermissionErrorCode::MacAccessibilityDenied,
              .canOpenSettings = true,
          },
          {
              .kind = PermissionKind::MacInputMonitoring,
              .state = PermissionState::Denied,
              .errorCode = PermissionErrorCode::MacInputMonitoringDenied,
              .canOpenSettings = true,
          },
      },
  }));
  dock.resize(560, 700);
  dock.show();

  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *sendFiles = dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFilesButton"));
  auto *sendFolder = dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFolderButton"));
  QVERIFY(list != nullptr);
  QVERIFY(sendFiles != nullptr);
  QVERIFY(sendFolder != nullptr);
  list->setCurrentIndex(devices.index(0, 0));
  QTRY_VERIFY(sendFiles->isEnabled());
  QVERIFY(sendFolder->isEnabled());
  QVERIFY(permissions.allowsCapability(PermissionStatusModel::FileTransferCapability));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto filePath = directory.filePath(QStringLiteral("input-permission-independent.txt"));
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(filePath)});
  const auto position = list->visualRect(list->currentIndex()).center();
  QVERIFY(list->visualRect(list->currentIndex()).isValid());
  QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(list->viewport(), &enter);
  QVERIFY(enter.isAccepted());
  QSignalSpy sendRequested(&dock, &DevicesDock::sendItemsRequested);
  QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(list->viewport(), &drop);
  QVERIFY(drop.isAccepted());
  QCOMPARE(sendRequested.count(), 1);
}

void DevicesDockTests::gatesPairingOnMacLocalNetworkPermission()
{
  qRegisterMetaType<DeviceId>();
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingService);
  PermissionStatusModel permissions(PermissionPlatform::MacOS);
  DevicesDock dock(devices, pairing, permissions);
  devices.upsertRemoteDevice(peerSnapshot());
  dock.show();

  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pairButton = dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  QVERIFY(list != nullptr);
  QVERIFY(pairButton != nullptr);
  list->setCurrentIndex(devices.index(0, 0));
  QSignalSpy requested(&dock, &DevicesDock::pairingRequested);

  const auto publishLocalNetworkState = [&permissions](PermissionState state) {
    return permissions.setSnapshot({
        .platform = PermissionPlatform::MacOS,
        .entries = {
            {
                .kind = PermissionKind::MacLocalNetwork,
                .state = state,
                .errorCode = state == PermissionState::Denied ? PermissionErrorCode::MacLocalNetworkDenied
                                                               : PermissionErrorCode::None,
                .canOpenSettings = true,
            },
            {.kind = PermissionKind::MacAccessibility, .state = PermissionState::Granted},
            {.kind = PermissionKind::MacInputMonitoring, .state = PermissionState::Granted},
        },
    });
  };

  for (const auto state : {PermissionState::Unknown, PermissionState::Denied, PermissionState::NeedsAction}) {
    QVERIFY(publishLocalNetworkState(state));
    QTRY_VERIFY(!pairButton->isEnabled());
    QCOMPARE(pairButton->text(), QStringLiteral("Pair"));

    // The request handler repeats the permission guard so programmatic or
    // stale UI activation cannot bypass the disabled state.
    pairButton->setEnabled(true);
    QTest::mouseClick(pairButton, Qt::LeftButton);
    QCOMPARE(requested.count(), 0);
  }

  QVERIFY(publishLocalNetworkState(PermissionState::Granted));
  QTRY_VERIFY(pairButton->isEnabled());
  QTest::mouseClick(pairButton, Qt::LeftButton);
  QCOMPARE(requested.count(), 1);
}

void DevicesDockTests::choosesFilesAndFolderAndPublishesImmutableIntent()
{
  qRegisterMetaType<DeviceSnapshot>();
  qRegisterMetaType<::relaydesk::transfer::SendOptions>();
  Fixture fixture;
  auto peer = peerSnapshot(DevicePresence::Online, true);
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.resize(420, 700);
  fixture.dock.show();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pair = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  auto *sendFiles = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFilesButton"));
  auto *sendFolder = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFolderButton"));
  QVERIFY(list != nullptr);
  QVERIFY(pair != nullptr);
  QVERIFY(sendFiles != nullptr);
  QVERIFY(sendFolder != nullptr);
  list->setCurrentIndex(fixture.devices.index(0, 0));
  QTRY_VERIFY(sendFiles->isEnabled());
  QVERIFY(!pair->isVisible());
  QVERIFY(sendFiles->isVisible());
  QVERIFY(sendFolder->isVisible());
  QCOMPARE(sendFiles->geometry().top(), sendFolder->geometry().top());
  QVERIFY(sendFiles->focusPolicy() != Qt::NoFocus);
  QCOMPARE(sendFiles->accessibleName(), QStringLiteral("Send files"));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto firstPath = directory.filePath(QStringLiteral("first.txt"));
  const auto secondPath = directory.filePath(QStringLiteral("second.bin"));
  QFile first(firstPath);
  QFile second(secondPath);
  QVERIFY(first.open(QIODevice::WriteOnly));
  QVERIFY(second.open(QIODevice::WriteOnly));
  first.close();
  second.close();
  const QList<QUrl> files{QUrl::fromLocalFile(firstPath), QUrl::fromLocalFile(secondPath)};
  fixture.dock.setFileChooser([files](QWidget &) { return files; });
  fixture.dock.setFolderChooser([path = directory.path()](QWidget &) { return QList<QUrl>{QUrl::fromLocalFile(path)}; }
  );

  QSignalSpy requested(&fixture.dock, &DevicesDock::sendItemsRequested);
  sendFiles->setFocus();
  QTest::keyClick(sendFiles, Qt::Key_Space);
  QCOMPARE(requested.count(), 1);
  auto arguments = requested.takeFirst();
  QCOMPARE(arguments.at(0).metaType(), QMetaType::fromType<DeviceId>());
  QCOMPARE(*static_cast<const DeviceId *>(arguments.at(0).constData()), peer.id);
  QCOMPARE(arguments.at(1).value<QList<QUrl>>(), files);
  QCOMPARE(
      arguments.at(2).value<::relaydesk::transfer::SendOptions>().conflictPolicy,
      ::relaydesk::transfer::ConflictPolicy::AutoRename
  );

  peer.presence = DevicePresence::Offline;
  fixture.devices.upsertRemoteDevice(peer);
  QVERIFY(!sendFiles->isEnabled());

  peer.presence = DevicePresence::Online;
  fixture.devices.upsertRemoteDevice(peer);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(sendFolder->isEnabled());
  QTest::mouseClick(sendFolder, Qt::LeftButton);
  QCOMPARE(requested.count(), 1);
  arguments = requested.takeFirst();
  QCOMPARE(arguments.at(1).value<QList<QUrl>>(), QList<QUrl>{QUrl::fromLocalFile(directory.path())});
}

void DevicesDockTests::rejectsInvalidOrIneligibleSendItems()
{
  Fixture fixture;
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  auto peer = peerSnapshot(DevicePresence::Online, true);
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.show();
  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *sendFiles = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFilesButton"));
  auto *feedback = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskSendFeedback"));
  QVERIFY(list != nullptr);
  QVERIFY(sendFiles != nullptr);
  QVERIFY(feedback != nullptr);
  list->setCurrentIndex(fixture.devices.index(0, 0));

  QSignalSpy rejected(&fixture.dock, &DevicesDock::sendItemsRejected);
  fixture.dock.setFileChooser([](QWidget &) { return QList<QUrl>{}; });
  QTest::mouseClick(sendFiles, Qt::LeftButton);
  QCOMPARE(rejected.count(), 1);
  QCOMPARE(feedback->text(), QStringLiteral("Choose at least one file or folder"));

  fixture.dock.setFileChooser([](QWidget &) {
    return QList<QUrl>{QUrl(QStringLiteral("https://example.invalid/file.txt"))};
  });
  QTest::mouseClick(sendFiles, Qt::LeftButton);
  QCOMPARE(rejected.count(), 2);
  QCOMPARE(feedback->text(), QStringLiteral("Choose files or folders stored on this device"));

  fixture.dock.setFileChooser([missing = directory.filePath(QStringLiteral("missing-file.txt"))](QWidget &) {
    return QList<QUrl>{QUrl::fromLocalFile(missing)};
  });
  QTest::mouseClick(sendFiles, Qt::LeftButton);
  QCOMPARE(rejected.count(), 3);
  QCOMPARE(feedback->text(), QStringLiteral("One or more selected items cannot be read"));

  peer.trusted = false;
  fixture.devices.upsertRemoteDevice(peer);
  QTRY_VERIFY(!sendFiles->isEnabled());
}

void DevicesDockTests::acceptsLocalUrlDropOnlyForEligiblePeer()
{
  qRegisterMetaType<DeviceSnapshot>();
  qRegisterMetaType<::relaydesk::transfer::SendOptions>();
  Fixture fixture;
  auto peer = peerSnapshot(DevicePresence::Online, true);
  fixture.devices.upsertRemoteDevice(peer);
  fixture.dock.resize(420, 700);
  fixture.dock.show();
  QCoreApplication::processEvents();

  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  QVERIFY(list != nullptr);
  const auto target = fixture.devices.index(fixture.devices.indexOf(peer.id), 0);
  const auto position = list->visualRect(target).center();
  QVERIFY(list->visualRect(target).isValid());

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto filePath = directory.filePath(QStringLiteral("drop.txt"));
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();
  const QList<QUrl> urls{QUrl::fromLocalFile(filePath)};
  QMimeData mime;
  mime.setUrls(urls);
  QSignalSpy requested(&fixture.dock, &DevicesDock::sendItemsRequested);

  QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(list->viewport(), &enter);
  QVERIFY(enter.isAccepted());
  QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(list->viewport(), &drop);
  QVERIFY(drop.isAccepted());
  QCOMPARE(requested.count(), 1);
  QCOMPARE(requested.takeFirst().at(1).value<QList<QUrl>>(), urls);

  peer.capabilities.fileV1 = false;
  fixture.devices.upsertRemoteDevice(peer);
  QDragEnterEvent rejectedEnter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(list->viewport(), &rejectedEnter);
  QVERIFY(!rejectedEnter.isAccepted());
}

void DevicesDockTests::rendersNonBlockingIncomingOfferAndKeyboardDecisions()
{
  Fixture fixture;
  IncomingOfferModel incomingModel({
      .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
      .availableBytes = 1'000'000,
      .autoAcceptTrustedDevices = false,
      .decisionTimeoutMs = 5000,
  });
  fixture.dock.setIncomingOfferModel(&incomingModel);
  fixture.dock.resize(532, 300);
  fixture.dock.show();
  fixture.pairingService.publish(
      pairingSnapshot(peerSnapshot(), PairingState::AwaitingUserComparison, QStringLiteral("123456")),
      fixture.fingerprint
  );
  auto *pairingPanel = fixture.dock.findChild<QFrame *>(QStringLiteral("relaydeskPairingPanel"));
  QVERIFY(pairingPanel != nullptr);
  QTRY_VERIFY(pairingPanel->isVisible());
  QVERIFY(incomingModel.receiveOffer(incomingTransfer()));

  auto *panel = fixture.dock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *heading = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferHeading"));
  auto *name = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferName"));
  auto *summary = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferSummary"));
  auto *destination = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferDestination"));
  auto *conflict = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferConflict"));
  auto *accept = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *reject = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskRejectIncomingOfferButton"));
  auto *changeSettings =
      fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskChangeIncomingOfferSettingsButton"));
  QVERIFY(panel != nullptr);
  QVERIFY(heading != nullptr);
  QVERIFY(name != nullptr);
  QVERIFY(summary != nullptr);
  QVERIFY(destination != nullptr);
  QVERIFY(conflict != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(reject != nullptr);
  QVERIFY(changeSettings != nullptr);
  QVERIFY(panel->isVisible());
  QVERIFY(!pairingPanel->isVisible());
  QVERIFY(!panel->isWindow());
  QVERIFY(fixture.dock.isEnabled());
  QCOMPARE(heading->textFormat(), Qt::PlainText);
  QCOMPARE(name->textFormat(), Qt::PlainText);
  QCOMPARE(heading->text(), QStringLiteral("<b>Studio Mac</b> wants to send"));
  QCOMPARE(name->text(), QStringLiteral("<img src=x> Project"));
  QVERIFY(summary->text().startsWith(QStringLiteral("3 items · ")));
  QCOMPARE(destination->text(), QStringLiteral("Save to: Downloads/RelayDesk"));
  QCOMPARE(conflict->text(), QStringLiteral("Conflict: auto rename"));
  QCOMPARE(accept->accessibleName(), QStringLiteral("Accept"));
  QVERIFY(accept->focusPolicy() != Qt::NoFocus);
  QVERIFY(reject->focusPolicy() != Qt::NoFocus);

  QSignalSpy settingsRequested(&fixture.dock, &DevicesDock::incomingOfferSettingsRequested);
  changeSettings->setFocus();
  QTest::keyClick(changeSettings, Qt::Key_Space);
  QCOMPARE(settingsRequested.count(), 1);

  QSignalSpy accepted(&incomingModel, &IncomingOfferModel::acceptRequested);
  accept->setFocus();
  QTest::keyClick(accept, Qt::Key_Space);
  QCOMPARE(accepted.count(), 1);
  QVERIFY(!panel->isVisible());
  QVERIFY(pairingPanel->isVisible());
  QCOMPARE(
      *static_cast<const ::relaydesk::transfer::TransferId *>(accepted.constFirst().at(0).constData()),
      incomingModel.offer()->offer.transferId
  );
}

void DevicesDockTests::rendersAndResolvesQueuedIncomingConflicts()
{
  using ::relaydesk::transfer::IncomingConflictDecision;
  using ::relaydesk::transfer::IncomingConflictPrompt;

  Fixture fixture;
  fixture.dock.resize(520, 380);
  fixture.dock.show();
  const auto firstTransfer = TransferId::generate();
  const auto secondTransfer = TransferId::generate();
  const IncomingConflictPrompt stale{
      .transferId = firstTransfer,
      .conflictId = QUuid::createUuid(),
      .relativeProtocolPath = QStringLiteral("stale/private.txt"),
  };
  const IncomingConflictPrompt overwrite{
      .transferId = secondTransfer,
      .conflictId = QUuid::createUuid(),
      .relativeProtocolPath = QStringLiteral("Project/conflict.txt"),
  };
  fixture.dock.showIncomingConflictPrompt(stale);
  fixture.dock.showIncomingConflictPrompt(overwrite);
  fixture.dock.clearIncomingConflictPrompts(firstTransfer);

  auto *panel = fixture.dock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingConflictPanel"));
  auto *path = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingConflictPath"));
  auto *overwriteButton =
      fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictOverwriteButton"));
  auto *renameButton =
      fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictAutoRenameButton"));
  auto *skipButton = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictSkipButton"));
  auto *cancelButton = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictCancelButton"));
  QVERIFY(panel != nullptr);
  QVERIFY(path != nullptr);
  QVERIFY(overwriteButton != nullptr);
  QVERIFY(renameButton != nullptr);
  QVERIFY(skipButton != nullptr);
  QVERIFY(cancelButton != nullptr);
  QTRY_VERIFY(panel->isVisible());
  QCOMPARE(path->text(), overwrite.relativeProtocolPath);
  QVERIFY(!path->text().contains(QStringLiteral("C:/")));
  QCOMPARE(overwriteButton->accessibleName(), QStringLiteral("Replace"));
  QVERIFY(overwriteButton->focusPolicy() != Qt::NoFocus);
  QVERIFY(panel->geometry().right() <= fixture.dock.widget()->geometry().right());

  QList<IncomingConflictDecision> decisions;
  QList<TransferId> transferIds;
  connect(
      &fixture.dock, &DevicesDock::incomingConflictDecisionRequested, this,
      [&decisions, &transferIds](TransferId transferId, QUuid, IncomingConflictDecision decision) {
        transferIds.append(transferId);
        decisions.append(decision);
      }
  );
  overwriteButton->setFocus();
  QTest::keyClick(overwriteButton, Qt::Key_Space);
  QCOMPARE(transferIds, QList<TransferId>({secondTransfer}));
  QCOMPARE(decisions, QList<IncomingConflictDecision>({IncomingConflictDecision::Overwrite}));
  QVERIFY(!panel->isVisible());

  const auto queuedTransfer = TransferId::generate();
  for (int index = 0; index < 3; ++index) {
    fixture.dock.showIncomingConflictPrompt(
        {.transferId = queuedTransfer, .conflictId = QUuid::createUuid(), .relativeProtocolPath = QStringLiteral("next.txt")}
    );
  }
  QTRY_VERIFY(panel->isVisible());
  QTest::mouseClick(renameButton, Qt::LeftButton);
  QTest::mouseClick(skipButton, Qt::LeftButton);
  QTest::mouseClick(cancelButton, Qt::LeftButton);
  QCOMPARE(
      decisions,
      QList<IncomingConflictDecision>({
          IncomingConflictDecision::Overwrite,
          IncomingConflictDecision::AutoRename,
          IncomingConflictDecision::Skip,
          IncomingConflictDecision::CancelTransfer,
      })
  );
  QVERIFY(panel->isVisible());
  QTest::mouseClick(cancelButton, Qt::LeftButton);
  QCOMPARE(decisions.last(), IncomingConflictDecision::CancelTransfer);
  QCOMPARE(decisions.size(), 5);
  fixture.dock.clearIncomingConflictPrompts(queuedTransfer);
  QVERIFY(!panel->isVisible());
}

void DevicesDockTests::rendersUntrustedAndExpiredOfferSafely()
{
  qint64 now = 1000;
  Fixture fixture;
  IncomingOfferModel incomingModel(
      {
          .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
          .availableBytes = 1'000'000,
          .autoAcceptTrustedDevices = true,
          .decisionTimeoutMs = 100,
      },
      [&now]() { return now; }
  );
  fixture.dock.setIncomingOfferModel(&incomingModel);
  fixture.dock.resize(460, 760);
  fixture.dock.show();
  const auto incoming = incomingTransfer(false);
  QVERIFY(incomingModel.receiveOffer(incoming));

  auto *panel = fixture.dock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *error = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskIncomingOfferError"));
  auto *accept = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *reject = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskRejectIncomingOfferButton"));
  auto *dismiss = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskDismissIncomingOfferButton"));
  QVERIFY(panel != nullptr);
  QVERIFY(error != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(reject != nullptr);
  QVERIFY(dismiss != nullptr);
  QVERIFY(panel->isVisible());
  QVERIFY(!accept->isVisible());
  QVERIFY(reject->isVisible());
  QCOMPARE(error->text(), QStringLiteral("Pair this device before receiving files"));

  now += 100;
  QVERIFY(incomingModel.expireIfNeeded());
  QVERIFY(panel->isVisible());
  QVERIFY(!reject->isVisible());
  QVERIFY(dismiss->isVisible());
  QCOMPARE(error->text(), QStringLiteral("This transfer request expired"));
  dismiss->setFocus();
  QTest::keyClick(dismiss, Qt::Key_Space);
  QVERIFY(!panel->isVisible());
}

QTEST_MAIN(DevicesDockTests)

#include "DevicesDockTests.moc"
