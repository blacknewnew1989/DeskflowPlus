/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/filetransport/FileTlsFrameSink.h"
#include "relaydesk/filetransport/FileTlsTransport.h"

#include "relaydesk/trust/TlsIdentityAdapter.h"

#include <QFile>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;
using namespace relaydesk::transfer;

namespace {

const auto kCombinedPem = QByteArrayLiteral(
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCPMBaQGIlbqZK5\n"
    "IIKnfUtBH8nK6KniFRKcleDyT7rHgvvPMpNtflXhDudIRaTMAXiWoN7JS4txsZy7\n"
    "iZpE9Vvlrs0GO32fAz/v5qZWChCW1MNbDe9Stvu8hGkC7hR03EzrqeJlwmNdFN6O\n"
    "Z1u9d4p5rjBUwF8Hn861L8/0VHADCEzENcBWAxH6qr51HPNwymE/O8xJahLQoxNe\n"
    "c9WBTUnj7ZEv+JQnwbZtgxSC3xPx8JMNF9RjC2WtJcGtdyOawCoecNPP7U0JuG90\n"
    "16wR4A6l01+bccwx22OnqQ57Phnm4SduQBowF/u7ryBjRg1xcQjl8ZR3V9S4B3vI\n"
    "8BiO0Y0zAgMBAAECggEACTPZ/2DUUyPO3j8Mpp8S2xij63qQkIsyKwYm8uvU4UW1\n"
    "0Vc6ymq4MkK28ponQUVG7sdgCifkymXT4OmzFIOAaH6XhrMEG9gln9F/F0CGWGtM\n"
    "MunuW66O90q7RQjwH6KY/vxoJIodwLm6pARYjRDFwZREXu4OLXtD2bk88EMM/+TM\n"
    "CNt7qZkw7a2ErUG9GP0nDLFzZXQBl5IAHSzbKm0pdkr+b5qy13nJdseNyKbWXXKi\n"
    "XFuwhgpEqwplXj9NqpZ8YG1ph2OxqEi1ZkFptRWOjim8jyxWln4MUi6jPtjuzwsI\n"
    "336/mnAbqPqTKJgCYhutS2jeCmya8kNs01vqIUrxYQKBgQDHbH04Q3KEZSNo36Sc\n"
    "UJEtd2KB+yMtno/2t0GwrGI0LcbX9/87XftGGuEn2OvQlu1ohntoXCNy5Bj/QxeL\n"
    "BVufEKMuVVgrVtquneLu38D5oVSZYxNAwggPhMx26dVvoxXJg5nzBdjKHLbgFImq\n"
    "jivNC/94AJ+PbnfXEUZJ/1WvNQKBgQC3z1zYelKl9t54Lj4XwA5pZZmWteFUz6NM\n"
    "Wo5Dgf8G6iZvo9kbZB1qsluhSScbOWba2GqHow8rG8QLEVpAtEJTsJThuP0fBpjZ\n"
    "dQ2J/WkCzQwaCh94i4B/o8af/JyXTaZrIlhpOF10p5YGQO3NABNueYp0weyzPnoP\n"
    "wwty1TFPxwKBgAtzVEFNxh6J/B2Ccd4z1hIpP7O86sksyJFe9luhmkXqtvchmzsa\n"
    "a1ocIv95uhiRAfK1fhKA79wh8rl9bbWiyh75ApWfet+KLiZGlIgoutjahZQFF07p\n"
    "lTLm6iKNzJ6LW63la4qDtG3udiWpqDntzeAJJ1MJnh/LNQBZUpLfIVldAoGALH8F\n"
    "Ud5izYxylJNVMriqhHc09Bf5gWd2d5BgahU5IHpkbZgzgX795AtjRSsJTXza2lWT\n"
    "jFw72sqw7aD4wTsh51KS6AW5ON6G9/VvHp1641Ox/0e+EJdstvl1ptsnKTWB+ONq\n"
    "laYwcYH0PnVPW9YN3iuMCfG8FDQmplQoHFdhxZcCgYBW3zNDl1kQiWYRhCbBfMNM\n"
    "gOe7cw+T31ra7jUXMqX42ppZYBQWv54ENRRtptSewGNMALKYMYP91Qm9AQMn8dUV\n"
    "oPEH6Aynt46oeLmcXqr5UGK2s9fxAQARnBZgI5HF97WMejy1vZMerxMJp1Amz/U0\n"
    "DVoEoTyMhhPjPpLbwxHeSw==\n"
    "-----END PRIVATE KEY-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIICyzCCAbOgAwIBAgIBATANBgkqhkiG9w0BAQsFADApMScwJQYDVQQDDB5SZWxh\n"
    "eURlc2sgVExTIExvb3BiYWNrIEZpeHR1cmUwHhcNMjYwMTAxMDAwMDAwWhcNMzYw\n"
    "MTAxMDAwMDAwWjApMScwJQYDVQQDDB5SZWxheURlc2sgVExTIExvb3BiYWNrIEZp\n"
    "eHR1cmUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCPMBaQGIlbqZK5\n"
    "IIKnfUtBH8nK6KniFRKcleDyT7rHgvvPMpNtflXhDudIRaTMAXiWoN7JS4txsZy7\n"
    "iZpE9Vvlrs0GO32fAz/v5qZWChCW1MNbDe9Stvu8hGkC7hR03EzrqeJlwmNdFN6O\n"
    "Z1u9d4p5rjBUwF8Hn861L8/0VHADCEzENcBWAxH6qr51HPNwymE/O8xJahLQoxNe\n"
    "c9WBTUnj7ZEv+JQnwbZtgxSC3xPx8JMNF9RjC2WtJcGtdyOawCoecNPP7U0JuG90\n"
    "16wR4A6l01+bccwx22OnqQ57Phnm4SduQBowF/u7ryBjRg1xcQjl8ZR3V9S4B3vI\n"
    "8BiO0Y0zAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAByUZKn5L62l1FxFIdPhEHAx\n"
    "9rgH2nJEscUmhlfeWh298BI8SkKthZqmlOM94UHBmIVmIFRvolfZHoU6GONrdmaj\n"
    "J4oGwrEeXABfrwLRpXE+3gAbsHNVTWjZdXknTzwQUs1Au0tSuXEamiEPZz+VAtmt\n"
    "xL8Ub7Qaggrc0cESMVBxe4GyWfwRahqQ7JaTZTwTegQhnZUySeZzwYJoUMAYy3if\n"
    "ByoB0jkeKSaXDYjUfiZQwnMd0yj2ns7TK6QEa7zyw6mx54bPo7tBAqxspsURQ6o1\n"
    "l3LleIfL3NLZTxQD0wHjbfFGfcJZL8WACMGUDTs9CQcexljJQXqsvNYdAdIXcGE=\n"
    "-----END CERTIFICATE-----\n"
);

QString writeIdentity(const QTemporaryDir &directory)
{
  const QString path = directory.filePath(QStringLiteral("deskflow.pem"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(kCombinedPem) != kCombinedPem.size()) {
    return {};
  }
  return path;
}

TrustedDevice trustedDevice(const DeviceId &id, QByteArray fingerprint, bool revoked = false)
{
  return {
      .deviceId = id,
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = std::move(fingerprint),
      .revoked = revoked,
  };
}

} // namespace

class FileTlsTransportTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void pinnedLoopbackAuthenticatesAndTransfersFrame();
  void untrustedLoopbackFailsClosed_data();
  void untrustedLoopbackFailsClosed();
  void enforcesAuthenticationAndWriteBounds();
  void reportsHandshakeTimeout();
};

void FileTlsTransportTests::pinnedLoopbackAuthenticatesAndTransfersFrame()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString identityPath = writeIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const DeviceId serverId = DeviceId::generate();
  const DeviceId clientId = DeviceId::generate();
  TrustedDeviceStore serverTrust(directory.filePath(QStringLiteral("server-trust.json")));
  TrustedDeviceStore clientTrust(directory.filePath(QStringLiteral("client-trust.json")));
  QVERIFY(serverTrust.upsert(trustedDevice(clientId, identity.fingerprintSha256)));
  QVERIFY(clientTrust.upsert(trustedDevice(serverId, identity.fingerprintSha256)));

  FileTlsSettings settings;
  settings.maxQueuedWriteBytes = 512;
  FileTlsListener listener(serverId, &serverTrust, identityPath, settings);
  FileTlsConnection *serverConnection = nullptr;
  bool serverAuthenticated = false;
  bool clientAuthenticated = false;
  QString authenticationFailure;
  std::optional<Frame> received;
  connect(&listener, &FileTlsListener::connectionCreated, this, [&](FileTlsConnection *connection) {
    serverConnection = connection;
    connect(connection, &FileTlsConnection::authenticated, this, [&]() { serverAuthenticated = true; });
    connect(connection, &FileTlsConnection::failed, this, [&](FileTlsError, const QString &diagnostic) {
      authenticationFailure = diagnostic;
    });
    connect(connection, &FileTlsConnection::frameReceived, this, [&](const Frame &frame) { received = frame; });
  });
  QString diagnostic;
  QCOMPARE(listener.listen(QHostAddress::LocalHost, 0, &diagnostic), FileTlsError::None);
  QVERIFY2(diagnostic.isEmpty(), qPrintable(diagnostic));

  FileTlsClient client(clientId, &clientTrust, identityPath, settings);
  connect(&client, &FileTlsClient::connectionCreated, this, [&](FileTlsConnection *connection) {
    connect(connection, &FileTlsConnection::authenticated, this, [&]() { clientAuthenticated = true; });
    connect(connection, &FileTlsConnection::failed, this, [&](FileTlsError, const QString &diagnostic) {
      authenticationFailure = diagnostic;
    });
  });
  QCOMPARE(client.connectToHost(QHostAddress::LocalHost, listener.serverPort(), &diagnostic), FileTlsError::None);
  QTRY_VERIFY2_WITH_TIMEOUT(serverAuthenticated && clientAuthenticated, qPrintable(authenticationFailure), 5'000);
  QVERIFY(serverConnection != nullptr);
  QCOMPARE(serverConnection->peerDeviceId(), std::optional<DeviceId>(clientId));
  QCOMPARE(client.connection()->peerDeviceId(), std::optional<DeviceId>(serverId));
  QCOMPARE(client.connection()->writeHighWaterBytes(), settings.maxQueuedWriteBytes);

  const Frame heartbeat{.type = MessageType::Heartbeat, .streamId = 17};
  FileTlsFrameSink sink(*client.connection());
  QCOMPARE(sink.queuedBytes(), client.connection()->queuedWriteBytes());
  QCOMPARE(sink.submit(heartbeat).status, SenderFrameSinkStatus::Accepted);
  QTRY_VERIFY_WITH_TIMEOUT(received.has_value(), 2'000);
  QCOMPARE(*received, heartbeat);

  const Frame oversized{.type = MessageType::Heartbeat, .metadata = QByteArray(600, '\x2a')};
  QCOMPARE(sink.submit(oversized).status, SenderFrameSinkStatus::Backpressured);
}

void FileTlsTransportTests::untrustedLoopbackFailsClosed_data()
{
  QTest::addColumn<int>("trustCase");
  QTest::addColumn<FileTlsError>("expectedError");
  QTest::newRow("unknown") << 0 << FileTlsError::UnknownPeer;
  QTest::newRow("changed") << 1 << FileTlsError::FingerprintChanged;
  QTest::newRow("revoked") << 2 << FileTlsError::RevokedPeer;
}

void FileTlsTransportTests::untrustedLoopbackFailsClosed()
{
  QFETCH(int, trustCase);
  QFETCH(FileTlsError, expectedError);
  QTemporaryDir directory;
  const QString identityPath = writeIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY(identity.ok());
  const DeviceId serverId = DeviceId::generate();
  const DeviceId clientId = DeviceId::generate();
  TrustedDeviceStore serverTrust(directory.filePath(QStringLiteral("server-trust.json")));
  TrustedDeviceStore clientTrust(directory.filePath(QStringLiteral("client-trust.json")));
  if (trustCase == 1) {
    QVERIFY(serverTrust.upsert(trustedDevice(clientId, QByteArray(32, '\x5a'))));
  } else if (trustCase == 2) {
    QVERIFY(serverTrust.upsert(trustedDevice(clientId, identity.fingerprintSha256, true)));
  }
  QVERIFY(clientTrust.upsert(trustedDevice(serverId, identity.fingerprintSha256)));

  FileTlsListener listener(serverId, &serverTrust, identityPath);
  std::optional<FileTlsError> serverError;
  QString serverDiagnostic;
  connect(&listener, &FileTlsListener::connectionCreated, this, [&](FileTlsConnection *connection) {
    connect(connection, &FileTlsConnection::failed, this, [&](FileTlsError error, const QString &diagnostic) {
      serverError = error;
      serverDiagnostic = diagnostic;
    });
  });
  QCOMPARE(listener.listen(QHostAddress::LocalHost), FileTlsError::None);
  FileTlsClient client(clientId, &clientTrust, identityPath);
  QCOMPARE(client.connectToHost(QHostAddress::LocalHost, listener.serverPort()), FileTlsError::None);

  QTRY_VERIFY_WITH_TIMEOUT(serverError.has_value(), 5'000);
  QVERIFY2(*serverError == expectedError, qPrintable(serverDiagnostic));
}

void FileTlsTransportTests::enforcesAuthenticationAndWriteBounds()
{
  QTemporaryDir directory;
  const QString identityPath = writeIdentity(directory);
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trust.json")));
  const DeviceId clientId = DeviceId::generate();
  FileTlsClient client(clientId, &trust, identityPath);
  QCOMPARE(client.connectToHost(QHostAddress::LocalHost, 9), FileTlsError::None);
  QVERIFY(client.connection() != nullptr);
  QCOMPARE(client.connection()->sendFrame(Frame{}), FileTlsError::NotAuthenticated);

  FileTlsSettings invalidSettings;
  invalidSettings.maxQueuedWriteBytes = 0;
  FileTlsListener invalidListener(DeviceId::generate(), &trust, identityPath, invalidSettings);
  QCOMPARE(invalidListener.listen(QHostAddress::LocalHost), FileTlsError::InvalidIdentity);
}

void FileTlsTransportTests::reportsHandshakeTimeout()
{
  QTemporaryDir directory;
  const QString identityPath = writeIdentity(directory);
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trust.json")));
  FileTlsSettings settings;
  settings.handshakeTimeoutMs = 100;
  FileTlsListener listener(DeviceId::generate(), &trust, identityPath, settings);
  std::optional<FileTlsError> listenerError;
  connect(&listener, &FileTlsListener::failed, this, [&](FileTlsError error, const QString &) {
    listenerError = error;
  });
  QCOMPARE(listener.listen(QHostAddress::LocalHost), FileTlsError::None);

  QTcpSocket plaintextClient;
  plaintextClient.connectToHost(QHostAddress::LocalHost, listener.serverPort());
  QVERIFY(plaintextClient.waitForConnected(1'000));
  QTRY_VERIFY_WITH_TIMEOUT(listenerError.has_value(), 2'000);
  QCOMPARE(*listenerError, FileTlsError::HandshakeTimeout);
}

QTEST_MAIN(FileTlsTransportTests)

#include "FileTlsTransportTests.moc"
