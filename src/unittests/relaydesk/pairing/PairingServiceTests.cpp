/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingService.h"

#include <QNetworkDatagram>
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

PairingTransportResult sendFrom(QUdpSocket &socket, QByteArray bytes, const PairingEndpoint &endpoint)
{
  const auto written = socket.writeDatagram(bytes, endpoint.address, endpoint.port);
  return {
      .ok = written == bytes.size(),
      .diagnostic = written == bytes.size() ? QString() : socket.errorString(),
  };
}

void forwardPending(QUdpSocket &socket, PairingService &service)
{
  while (socket.hasPendingDatagrams()) {
    const auto datagram = socket.receiveDatagram();
    const auto senderPort = datagram.senderPort();
    (void)service.receiveDatagram(
        datagram.data(),
        {
            .address = datagram.senderAddress(),
            .port = senderPort > 0 && senderPort <= 65535 ? quint16(senderPort) : quint16(0),
        }
    );
  }
}

} // namespace

class PairingServiceTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void completesRealUdpLoopbackPairing();
  void completesOverInjectedSharedDatagramTransport();
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

void PairingServiceTests::completesOverInjectedSharedDatagramTransport()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto firstInfo = deviceInfo(DeviceId::generate(), QStringLiteral("First"), '\x31');
  const auto secondInfo = deviceInfo(DeviceId::generate(), QStringLiteral("Second"), '\x32');
  TrustedDeviceStore firstStore(directory.filePath(QStringLiteral("first/trusted.json")));
  TrustedDeviceStore secondStore(directory.filePath(QStringLiteral("second/trusted.json")));
  QUdpSocket firstTransport;
  QUdpSocket secondTransport;
  QVERIFY(firstTransport.bind(QHostAddress::LocalHost, 0));
  QVERIFY(secondTransport.bind(QHostAddress::LocalHost, 0));

  const auto now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  PairingService first(
      firstInfo, firstStore, {}, [now]() { return now; }, []() { return 123456U; },
      [&firstTransport](QByteArray bytes, PairingEndpoint endpoint) {
        return sendFrom(firstTransport, std::move(bytes), endpoint);
      }
  );
  PairingService second(
      secondInfo, secondStore, {}, [now]() { return now; }, []() { return 654321U; },
      [&secondTransport](QByteArray bytes, PairingEndpoint endpoint) {
        return sendFrom(secondTransport, std::move(bytes), endpoint);
      }
  );
  connect(&firstTransport, &QUdpSocket::readyRead, &first, [&]() { forwardPending(firstTransport, first); });
  connect(&secondTransport, &QUdpSocket::readyRead, &second, [&]() { forwardPending(secondTransport, second); });

  const auto started = first.startPairing(
      snapshotFor(secondInfo), secondInfo.certificateFingerprintSha256,
      {.address = QHostAddress::LocalHost, .port = secondTransport.localPort()}
  );
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(second.snapshot().has_value(), 3000);
  const auto sessionId = first.snapshot()->pairingSessionId;
  const auto sas = first.snapshot()->sixDigitSas;
  QVERIFY(first.confirmMatchingSas(sessionId).ok());
  QVERIFY(second.submitDisplayedSas(sessionId, sas).ok());

  QTRY_COMPARE_WITH_TIMEOUT(first.snapshot()->state, PairingState::Completed, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(second.snapshot()->state, PairingState::Completed, 3000);
  QCOMPARE(
      firstStore.trustStatus(secondInfo.deviceId, secondInfo.certificateFingerprintSha256),
      TrustStatus::Trusted
  );
  QCOMPARE(
      secondStore.trustStatus(firstInfo.deviceId, firstInfo.certificateFingerprintSha256),
      TrustStatus::Trusted
  );
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
