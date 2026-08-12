/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryService.h"

#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

using namespace deskflow::relaydesk;

namespace {
DeviceInfo exampleDevice()
{
  return {
      .deviceId = DeviceId::generate(),
      .displayName = QStringLiteral("RelayDesk workstation"),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("1.26.0-relaydesk.1"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .clipboardText = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, '\x33'),
  };
}

DiscoveryInterface networkInterface(
    QString name, QString address, QString broadcast, bool up = true, bool running = true, bool loopback = false
)
{
  return {
      .name = std::move(name),
      .index = 1,
      .address = QHostAddress(std::move(address)),
      .broadcastAddress = QHostAddress(std::move(broadcast)),
      .isUp = up,
      .isRunning = running,
      .isLoopback = loopback,
  };
}
} // namespace

class DiscoveryServiceTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void broadcastsOnEveryUsableIpv4Interface();
  void unusableAndDuplicateInterfacesAreSkipped();
  void sendFailureIsDiagnosable();
  void invalidSettingsAndBindFailureAreDiagnosable();
  void startStopAndLoopbackReceive();
  void invalidLoopbackDatagramIsReported();
};

void DiscoveryServiceTests::broadcastsOnEveryUsableIpv4Interface()
{
  struct SentDatagram
  {
    QByteArray payload;
    QHostAddress destination;
    quint16 port = 0;
    QHostAddress interfaceAddress;
  };
  QList<SentDatagram> sent;
  const auto interfaces = QList{
      networkInterface(QStringLiteral("ethernet"), QStringLiteral("192.168.1.10"), QStringLiteral("192.168.1.255")),
      networkInterface(QStringLiteral("wifi"), QStringLiteral("10.20.0.7"), QStringLiteral("10.20.0.255")),
  };
  DiscoveryService service(
      exampleDevice(), {.port = 31415, .announcementIntervalMs = 4000}, [interfaces]() { return interfaces; },
      [&sent](const QByteArray &payload, const QHostAddress &destination, quint16 port,
              const QHostAddress &interfaceAddress, QString *) {
        sent.append({payload, destination, port, interfaceAddress});
        return payload.size();
      }
  );

  QString errorMessage;
  QVERIFY2(service.announceNow(&errorMessage), qPrintable(errorMessage));
  QCOMPARE(sent.size(), 2);
  QCOMPARE(sent.at(0).destination, QHostAddress(QStringLiteral("192.168.1.255")));
  QCOMPARE(sent.at(0).interfaceAddress, QHostAddress(QStringLiteral("192.168.1.10")));
  QCOMPARE(sent.at(0).port, quint16(31415));
  QCOMPARE(sent.at(1).destination, QHostAddress(QStringLiteral("10.20.0.255")));
  QCOMPARE(sent.at(1).interfaceAddress, QHostAddress(QStringLiteral("10.20.0.7")));
  QVERIFY(DiscoveryCodec::decode(sent.at(0).payload).isSuccess());
}

void DiscoveryServiceTests::unusableAndDuplicateInterfacesAreSkipped()
{
  const auto valid =
      networkInterface(QStringLiteral("ethernet"), QStringLiteral("192.168.2.4"), QStringLiteral("192.168.2.255"));
  const auto interfaces = QList{
      valid,
      valid,
      networkInterface(
          QStringLiteral("loopback"), QStringLiteral("127.0.0.1"), QStringLiteral("127.255.255.255"), true, true,
          true
      ),
      networkInterface(
          QStringLiteral("down"), QStringLiteral("10.0.0.2"), QStringLiteral("10.0.0.255"), false, false
      ),
      networkInterface(QStringLiteral("no-broadcast"), QStringLiteral("172.16.0.4"), QString()),
  };
  int sends = 0;
  DiscoveryService service(
      exampleDevice(), {}, [interfaces]() { return interfaces; },
      [&sends](const QByteArray &payload, const QHostAddress &, quint16, const QHostAddress &, QString *) {
        ++sends;
        return payload.size();
      }
  );

  QVERIFY(service.announceNow());
  QCOMPARE(sends, 1);

  QSignalSpy errors(&service, &DiscoveryService::errorOccurred);
  DiscoveryService noInterfaces(
      exampleDevice(), {}, []() { return QList<DiscoveryInterface>{}; },
      [](const QByteArray &, const QHostAddress &, quint16, const QHostAddress &, QString *) { return qint64(-1); }
  );
  QSignalSpy noInterfaceErrors(&noInterfaces, &DiscoveryService::errorOccurred);
  QString diagnostic;
  QVERIFY(!noInterfaces.announceNow(&diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("No active")));
  QCOMPARE(noInterfaceErrors.size(), 1);
  QCOMPARE(
      noInterfaceErrors.first().at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::NoUsableInterfaces
  );
  QCOMPARE(errors.size(), 0);
}

void DiscoveryServiceTests::sendFailureIsDiagnosable()
{
  const auto interfaces = QList{
      networkInterface(QStringLiteral("ethernet"), QStringLiteral("192.168.1.9"), QStringLiteral("192.168.1.255")),
  };
  DiscoveryService service(
      exampleDevice(), {}, [interfaces]() { return interfaces; },
      [](const QByteArray &, const QHostAddress &, quint16, const QHostAddress &, QString *errorMessage) {
        *errorMessage = QStringLiteral("simulated network failure");
        return qint64(-1);
      }
  );
  QSignalSpy errors(&service, &DiscoveryService::errorOccurred);

  QString diagnostic;
  QVERIFY(!service.announceNow(&diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("ethernet")));
  QVERIFY(diagnostic.contains(QStringLiteral("simulated network failure")));
  QCOMPARE(errors.size(), 1);
  QCOMPARE(errors.first().at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::SendFailed);
}

void DiscoveryServiceTests::invalidSettingsAndBindFailureAreDiagnosable()
{
  DiscoveryService invalidInterval(
      exampleDevice(), {.port = 24802, .announcementIntervalMs = 0},
      []() { return QList<DiscoveryInterface>{}; }
  );
  QSignalSpy invalidErrors(&invalidInterval, &DiscoveryService::errorOccurred);
  QString diagnostic;
  QVERIFY(!invalidInterval.start(&diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("interval")));
  QCOMPARE(invalidErrors.first().at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::InvalidSettings);

  DiscoveryService ephemeralNotStarted(
      exampleDevice(), {.port = 0, .announcementIntervalMs = 4000},
      []() {
        return QList{
            networkInterface(
                QStringLiteral("ethernet"), QStringLiteral("192.168.1.9"), QStringLiteral("192.168.1.255")
            ),
        };
      },
      [](const QByteArray &payload, const QHostAddress &, quint16, const QHostAddress &, QString *) {
        return payload.size();
      }
  );
  QVERIFY(!ephemeralNotStarted.announceNow(&diagnostic));
  QVERIFY(diagnostic.contains(QStringLiteral("destination port")));

  QUdpSocket occupiedSocket;
  QVERIFY(occupiedSocket.bind(QHostAddress::AnyIPv4, 0));
  DiscoveryService occupiedPort(
      exampleDevice(), {.port = occupiedSocket.localPort(), .announcementIntervalMs = 4000},
      []() { return QList<DiscoveryInterface>{}; }
  );
  QSignalSpy bindErrors(&occupiedPort, &DiscoveryService::errorOccurred);
  QVERIFY(!occupiedPort.start(&diagnostic));
  QVERIFY(diagnostic.contains(QString::number(occupiedSocket.localPort())));
  QCOMPARE(bindErrors.first().at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::BindFailed);
}

void DiscoveryServiceTests::startStopAndLoopbackReceive()
{
  DiscoveryService service(
      exampleDevice(), {.port = 0, .announcementIntervalMs = 60000}, []() { return QList<DiscoveryInterface>{}; }
  );
  QSignalSpy started(&service, &DiscoveryService::started);
  QSignalSpy stopped(&service, &DiscoveryService::stopped);
  DeviceInfo received = exampleDevice();
  bool receivedAdvertisement = false;
  QHostAddress sender;
  connect(
      &service, &DiscoveryService::advertisementReceived, this,
      [&received, &receivedAdvertisement, &sender](const DeviceInfo &device, const QHostAddress &senderAddress) {
        received = device;
        receivedAdvertisement = true;
        sender = senderAddress;
      }
  );

  QString errorMessage;
  QVERIFY2(service.start(&errorMessage), qPrintable(errorMessage));
  QVERIFY(service.isRunning());
  QVERIFY(service.boundPort() != 0);
  QCOMPARE(started.size(), 1);

  const auto peer = exampleDevice();
  const auto encoded = DiscoveryCodec::encodeAdvertisement(peer);
  QUdpSocket senderSocket;
  QCOMPARE(senderSocket.writeDatagram(encoded, QHostAddress::LocalHost, service.boundPort()), qint64(encoded.size()));

  QTRY_VERIFY_WITH_TIMEOUT(receivedAdvertisement, 2000);
  QCOMPARE(received, peer);
  QCOMPARE(sender, QHostAddress(QHostAddress::LocalHost));

  service.stop();
  QVERIFY(!service.isRunning());
  QCOMPARE(service.boundPort(), quint16(0));
  QCOMPARE(stopped.size(), 1);
}

void DiscoveryServiceTests::invalidLoopbackDatagramIsReported()
{
  DiscoveryService service(
      exampleDevice(), {.port = 0, .announcementIntervalMs = 60000}, []() { return QList<DiscoveryInterface>{}; }
  );
  QSignalSpy errors(&service, &DiscoveryService::errorOccurred);
  QVERIFY(service.start());
  errors.clear();

  QUdpSocket senderSocket;
  const QByteArray invalid("not a RelayDesk discovery packet");
  QCOMPARE(senderSocket.writeDatagram(invalid, QHostAddress::LocalHost, service.boundPort()), qint64(invalid.size()));

  QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 2000);
  QCOMPARE(errors.first().at(0).value<DiscoveryServiceError>(), DiscoveryServiceError::InvalidDatagram);
}

QTEST_MAIN(DiscoveryServiceTests)

#include "DiscoveryServiceTests.moc"
