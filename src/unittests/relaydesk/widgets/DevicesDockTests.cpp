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
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <chrono>

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
    DeviceSnapshot peer, PairingState state, QString sas = {}, QString errorMessageKey = {}
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
      .errorMessageKey = std::move(errorMessageKey),
  };
}

::relaydesk::transfer::NegotiatedCapabilities transferCapabilities()
{
  using namespace ::relaydesk::transfer;
  return {
      .protocolMajorVersion = kProtocolMajorVersion,
      .features = {QStringLiteral("file.v1"), QStringLiteral("folder.v1")},
      .chunkBytes = 1024,
      .maxPayloadBytes = 4096,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 1000,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask},
  };
}

::relaydesk::transfer::IncomingOffer incomingTransfer(bool trusted = true)
{
  using namespace ::relaydesk::transfer;
  return {
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("<b>Studio Mac</b>"),
      .offer = {
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
  void emitsImmutablePairingRequest();
  void rendersAndDrivesSharedPairingModel();
  void confirmsAndCancelsFromPairingPanel();
  void rendersExpiredPairingState();
  void rendersPermissionGuidanceAndKeyboardAction();
  void choosesFilesAndFolderAndPublishesImmutableIntent();
  void rejectsInvalidOrIneligibleSendItems();
  void acceptsLocalUrlDropOnlyForEligiblePeer();
  void rendersNonBlockingIncomingOfferAndKeyboardDecisions();
  void rendersUntrustedAndExpiredOfferSafely();
};

void DevicesDockTests::rendersEmptyAndPairableDeviceStates()
{
  Fixture fixture;
  fixture.dock.show();
  QCoreApplication::processEvents();

  auto *empty = fixture.dock.findChild<QLabel *>(QStringLiteral("relaydeskDevicesEmptyLabel"));
  auto *list = fixture.dock.findChild<QListView *>(QStringLiteral("relaydeskDevicesView"));
  auto *pair = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskPairSelectedButton"));
  QVERIFY(empty != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(pair != nullptr);
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

  auto changedPeer = peer;
  changedPeer.presence = DevicePresence::TrustViolation;
  changedPeer.trusted = true;
  fixture.devices.upsertRemoteDevice(changedPeer);
  list->setCurrentIndex(fixture.devices.index(fixture.devices.indexOf(peer.id), 0));
  QTRY_VERIFY(pair->isEnabled());
  QCOMPARE(pair->text(), QStringLiteral("Pair again"));
}

void DevicesDockTests::emitsImmutablePairingRequest()
{
  qRegisterMetaType<DeviceSnapshot>();
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
  QCOMPARE(arguments.at(0).metaType(), QMetaType::fromType<DeviceSnapshot>());
  const auto *emittedPeer = static_cast<const DeviceSnapshot *>(arguments.at(0).constData());
  QVERIFY(emittedPeer != nullptr);
  QCOMPARE(emittedPeer->id, peer.id);
  QCOMPARE(emittedPeer->displayName, peer.displayName);
  QCOMPARE(emittedPeer->presence, peer.presence);
  const auto storedPeer = fixture.devices.snapshot(peer.id);
  QVERIFY(storedPeer.has_value());
  QCOMPARE(storedPeer->id, peer.id);
  QCOMPARE(storedPeer->displayName, peer.displayName);
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
  const auto pairing = pairingSnapshot(
      peerSnapshot(), PairingState::AwaitingUserComparison, QStringLiteral("123456")
  );
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
  auto snapshot = pairingSnapshot(
      peerSnapshot(), PairingState::AwaitingUserComparison, QStringLiteral("123456")
  );
  pairingService.publish(snapshot, fingerprint);
  auto *state = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingStateLabel"));
  auto *error = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingErrorLabel"));
  QVERIFY(state != nullptr);
  QVERIFY(error != nullptr);
  snapshot.state = PairingState::Expired;
  snapshot.errorMessageKey = QStringLiteral("pairing.code.expired");
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
  QVERIFY(!banner->isVisible());
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
  auto *sendFiles = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFilesButton"));
  auto *sendFolder = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskSendFolderButton"));
  QVERIFY(list != nullptr);
  QVERIFY(sendFiles != nullptr);
  QVERIFY(sendFolder != nullptr);
  list->setCurrentIndex(fixture.devices.index(0, 0));
  QTRY_VERIFY(sendFiles->isEnabled());
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
  fixture.dock.setFolderChooser([path = directory.path()](QWidget &) {
    return QList<QUrl>{QUrl::fromLocalFile(path)};
  });

  QSignalSpy requested(&fixture.dock, &DevicesDock::sendItemsRequested);
  sendFiles->setFocus();
  QTest::keyClick(sendFiles, Qt::Key_Space);
  QCOMPARE(requested.count(), 1);
  auto arguments = requested.takeFirst();
  QCOMPARE(arguments.at(0).metaType(), QMetaType::fromType<DeviceSnapshot>());
  const auto *emittedPeer = static_cast<const DeviceSnapshot *>(arguments.at(0).constData());
  QVERIFY(emittedPeer != nullptr);
  QCOMPARE(emittedPeer->id, peer.id);
  QCOMPARE(emittedPeer->presence, DevicePresence::Online);
  QCOMPARE(arguments.at(1).value<QList<QUrl>>(), files);
  QCOMPARE(
      arguments.at(2).value<::relaydesk::transfer::SendOptions>().conflictPolicy,
      ::relaydesk::transfer::ConflictPolicy::AutoRename
  );

  peer.presence = DevicePresence::Offline;
  fixture.devices.upsertRemoteDevice(peer);
  QCOMPARE(emittedPeer->presence, DevicePresence::Online);
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
  ::relaydesk::transfer::TransferOfferStateMachine stateMachine(transferCapabilities());
  IncomingOfferModel incomingModel(
      stateMachine,
      {
          .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
          .availableBytes = 1'000'000,
          .autoAcceptTrustedDevices = false,
          .decisionTimeoutMs = 5000,
      }
  );
  fixture.dock.setIncomingOfferModel(&incomingModel);
  fixture.dock.resize(460, 760);
  fixture.dock.show();
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

  QSignalSpy accepted(&incomingModel, &IncomingOfferModel::acceptanceReady);
  accept->setFocus();
  QTest::keyClick(accept, Qt::Key_Space);
  QCOMPARE(accepted.count(), 1);
  QVERIFY(!panel->isVisible());
  QVERIFY(incomingModel.acceptance().has_value());
}

void DevicesDockTests::rendersUntrustedAndExpiredOfferSafely()
{
  qint64 now = 1000;
  Fixture fixture;
  ::relaydesk::transfer::TransferOfferStateMachine stateMachine(transferCapabilities());
  IncomingOfferModel incomingModel(
      stateMachine,
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
