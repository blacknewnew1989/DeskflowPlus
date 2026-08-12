/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUdpSocket>

#include <utility>

using namespace deskflow::relaydesk;

namespace {

DeviceInfo deviceInfo(const DeviceId &id, QString name, char fingerprintByte)
{
  return {
      .deviceId = id,
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, fingerprintByte),
  };
}

DeviceSnapshot snapshotFor(const DeviceInfo &device)
{
  return {
      .id = device.deviceId,
      .displayName = device.displayName,
      .platform = device.platform,
      .architecture = device.architecture,
      .presence = DevicePresence::Discovered,
      .capabilities = device.capabilities,
  };
}

} // namespace

class PairingServiceTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void completesRealUdpLoopbackPairing();
  void reportsBindAndMalformedDatagramErrors();
};

void PairingServiceTests::completesRealUdpLoopbackPairing()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  const auto firstInfo = deviceInfo(firstId, QStringLiteral("First"), '\x11');
  const auto secondInfo = deviceInfo(secondId, QStringLiteral("Second"), '\x22');
  TrustedDeviceStore firstStore(directory.filePath(QStringLiteral("first/trusted.json")));
  TrustedDeviceStore secondStore(directory.filePath(QStringLiteral("second/trusted.json")));
  const auto now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  PairingService first(
      firstInfo, firstStore, {}, [now]() { return now; }, []() { return 123456U; }
  );
  PairingService second(
      secondInfo, secondStore, {}, [now]() { return now; }, []() { return 654321U; }
  );
  QVERIFY2(first.listen(QHostAddress::LocalHost, 0).ok(), "first loopback listener should bind");
  QVERIFY2(second.listen(QHostAddress::LocalHost, 0).ok(), "second loopback listener should bind");
  QVERIFY(first.localPort() != 0);
  QVERIFY(second.localPort() != 0);

  const auto started = first.startPairing(
      snapshotFor(secondInfo), secondInfo.certificateFingerprintSha256,
      {.address = QHostAddress::LocalHost, .port = second.localPort()}
  );
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(second.snapshot().has_value(), 3000);
  const auto sessionId = first.snapshot()->pairingSessionId;
  const auto sas = first.snapshot()->sixDigitSas;
  QCOMPARE(second.snapshot()->pairingSessionId, sessionId);

  QVERIFY(first.confirmMatchingSas(sessionId).ok());
  QVERIFY(second.submitDisplayedSas(sessionId, sas).ok());

  QTRY_COMPARE_WITH_TIMEOUT(first.snapshot()->state, PairingState::Completed, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(second.snapshot()->state, PairingState::Completed, 3000);
  QVERIFY(firstStore.find(secondId).has_value());
  QVERIFY(secondStore.find(firstId).has_value());
  QCOMPARE(firstStore.find(secondId)->lastAddresses, QStringList{QStringLiteral("127.0.0.1")});
  QCOMPARE(secondStore.find(firstId)->lastAddresses, QStringList{QStringLiteral("127.0.0.1")});
}

void PairingServiceTests::reportsBindAndMalformedDatagramErrors()
{
  QTemporaryDir directory;
  const auto info = deviceInfo(DeviceId::generate(), QStringLiteral("Listener"), '\x31');
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  PairingService service(info, store);
  QSignalSpy failed(&service, &IPairingService::operationFailed);
  QVERIFY(service.listen(QHostAddress::LocalHost, 0).ok());

  QUdpSocket occupied;
  QVERIFY(occupied.bind(QHostAddress::LocalHost, 0));
  PairingService conflicting(info, store);
  const auto bindResult = conflicting.listen(QHostAddress::LocalHost, occupied.localPort());
  QCOMPARE(bindResult.error, PairingOperationError::InvalidEndpoint);

  QUdpSocket sender;
  QCOMPARE(
      sender.writeDatagram(QByteArray("not-cbor"), QHostAddress::LocalHost, service.localPort()),
      qint64(8)
  );
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
  const auto result = failed.first().first().value<PairingOperationResult>();
  QCOMPARE(result.error, PairingOperationError::DecodeFailed);
  QCOMPARE(result.messageError, PairingMessageError::MalformedCbor);
}

QTEST_MAIN(PairingServiceTests)

#include "PairingServiceTests.moc"
