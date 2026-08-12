/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/DevicesDock.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <chrono>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
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

struct Fixture
{
  PairingStateMachine pairingState{{}, {}, []() { return 42U; }};
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingState};
  DevicesDock dock{devices, pairing};
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
  QVERIFY(fixture.pairing.start(peer, fixture.fingerprint, QStringLiteral("123456")));
  const auto sessionId = fixture.pairingState.snapshot()->pairingSessionId;
  QVERIFY(fixture.pairingState.markTransportReady(sessionId).ok());
  QVERIFY(fixture.pairingState.markTranscriptExchanged(sessionId).ok());

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
  QCOMPARE(fixture.pairingState.snapshot()->state, PairingState::Confirming);
  QCOMPARE(state->text(), QStringLiteral("Confirming pairing"));
  QVERIFY(!entry->isVisible());
}

void DevicesDockTests::confirmsAndCancelsFromPairingPanel()
{
  Fixture fixture;
  fixture.dock.show();
  QVERIFY(fixture.pairing.start(peerSnapshot(), fixture.fingerprint, QStringLiteral("123456")));
  const auto sessionId = fixture.pairingState.snapshot()->pairingSessionId;
  QVERIFY(fixture.pairingState.markTransportReady(sessionId).ok());
  QVERIFY(fixture.pairingState.markTranscriptExchanged(sessionId).ok());

  auto *confirm = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskConfirmMatchingSasButton"));
  auto *cancel = fixture.dock.findChild<QPushButton *>(QStringLiteral("relaydeskCancelPairingButton"));
  QVERIFY(confirm != nullptr);
  QVERIFY(cancel != nullptr);
  QTRY_VERIFY(confirm->isVisible());
  QTest::mouseClick(confirm, Qt::LeftButton);
  QCOMPARE(fixture.pairingState.snapshot()->state, PairingState::Confirming);
  QTRY_VERIFY(cancel->isVisible());
  QTest::mouseClick(cancel, Qt::LeftButton);
  QCOMPARE(fixture.pairingState.snapshot()->state, PairingState::Rejected);
  QVERIFY(!cancel->isVisible());
}

void DevicesDockTests::rendersExpiredPairingState()
{
  using namespace std::chrono_literals;
  auto now = QDateTime::fromString(QStringLiteral("2026-08-12T04:00:00Z"), Qt::ISODate);
  PairingStateMachine pairingState({.validity = 5s}, [&now]() { return now; }, []() { return 42U; });
  DeviceHomeModel devices;
  PairingWizardModel pairing(pairingState);
  DevicesDock dock(devices, pairing);
  const auto fingerprint = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

  dock.show();
  QVERIFY(pairing.start(peerSnapshot(), fingerprint, QStringLiteral("123456")));
  auto *state = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingStateLabel"));
  auto *error = dock.findChild<QLabel *>(QStringLiteral("relaydeskPairingErrorLabel"));
  QVERIFY(state != nullptr);
  QVERIFY(error != nullptr);
  now = now.addSecs(6);
  QVERIFY(pairing.expireIfNeeded());
  QCOMPARE(state->text(), QStringLiteral("The pairing code expired. Generate a new code."));
  QCOMPARE(error->text(), QStringLiteral("The pairing code expired. Generate a new code."));
  QVERIFY(error->isVisible());
}

QTEST_MAIN(DevicesDockTests)

#include "DevicesDockTests.moc"
