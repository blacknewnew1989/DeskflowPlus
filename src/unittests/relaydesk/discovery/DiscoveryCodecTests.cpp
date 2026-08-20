/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {
DeviceInfo exampleDevice()
{
  return {
      .deviceId = DeviceId::generate(),
      .displayName = QString::fromUtf8("设计室 MacBook 🖥️"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .appVersion = QStringLiteral("1.26.0-relaydesk.1"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities =
          {
              .input = true,
              .clipboardText = true,
              .clipboardImage = true,
              .fileV1 = true,
              .folderV1 = true,
              .resumeV1 = false,
          },
      .certificateFingerprintSha256 = QByteArray(32, '\x5a'),
  };
}

QByteArray replaceEnvelopeValue(const QByteArray &encoded, qint64 key, const QCborValue &replacement)
{
  auto envelope = QCborValue::fromCbor(encoded).toMap();
  envelope.insert(QCborValue(key), replacement);
  return QCborValue(envelope).toCbor();
}
} // namespace

class DiscoveryCodecTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void advertisementRoundTripsDeviceInfo();
  void probeRoundTripsWithoutDeviceInfo();
  void invalidCborIsRejected_data();
  void invalidCborIsRejected();
  void protocolVersionAndMessageTypeAreStrict_data();
  void protocolVersionAndMessageTypeAreStrict();
  void unknownAndMissingEnvelopeFieldsAreRejected();
  void datagramAndPayloadLimitsAreEnforced();
  void deviceFieldLimitsAreEnforced();
};

void DiscoveryCodecTests::advertisementRoundTripsDeviceInfo()
{
  const auto device = exampleDevice();
  QString errorMessage;
  const auto encoded = DiscoveryCodec::encodeAdvertisement(device, &errorMessage);

  QVERIFY2(!encoded.isEmpty(), qPrintable(errorMessage));
  QVERIFY(encoded.size() <= kMaximumDiscoveryDatagramBytes);

  const auto decoded = DiscoveryCodec::decode(encoded);
  QVERIFY2(decoded.isSuccess(), qPrintable(decoded.diagnostic));
  QCOMPARE(decoded.error, DiscoveryCodecError::None);
  QCOMPARE(decoded.datagram->type, DiscoveryMessageType::Advertisement);
  QVERIFY(decoded.datagram->device.has_value());
  QCOMPARE(*decoded.datagram->device, device);
}

void DiscoveryCodecTests::probeRoundTripsWithoutDeviceInfo()
{
  const auto decoded = DiscoveryCodec::decode(DiscoveryCodec::encodeProbe());
  QVERIFY2(decoded.isSuccess(), qPrintable(decoded.diagnostic));
  QCOMPARE(decoded.datagram->type, DiscoveryMessageType::Probe);
  QVERIFY(!decoded.datagram->device.has_value());
}

void DiscoveryCodecTests::invalidCborIsRejected_data()
{
  QTest::addColumn<QByteArray>("datagram");
  QTest::addColumn<DiscoveryCodecError>("expectedError");

  QTest::newRow("empty") << QByteArray() << DiscoveryCodecError::EmptyDatagram;
  QTest::newRow("not-cbor") << QByteArray("not cbor") << DiscoveryCodecError::InvalidCbor;
  QTest::newRow("not-map") << QCborValue(QStringLiteral("relaydesk")).toCbor() << DiscoveryCodecError::InvalidCbor;
  QTest::newRow("truncated-map") << QByteArray::fromHex("a401") << DiscoveryCodecError::InvalidCbor;
  QTest::newRow("trailing-data")
      << (DiscoveryCodec::encodeAdvertisement(exampleDevice()) + QByteArray("trailing"))
      << DiscoveryCodecError::InvalidCbor;
}

void DiscoveryCodecTests::invalidCborIsRejected()
{
  QFETCH(QByteArray, datagram);
  QFETCH(DiscoveryCodecError, expectedError);

  const auto decoded = DiscoveryCodec::decode(datagram);
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, expectedError);
}

void DiscoveryCodecTests::protocolVersionAndMessageTypeAreStrict_data()
{
  QTest::addColumn<qint64>("key");
  QTest::addColumn<QCborValue>("replacement");
  QTest::addColumn<DiscoveryCodecError>("expectedError");

  QTest::newRow("wrong-protocol")
      << qint64(1) << QCborValue(QStringLiteral("another-product")) << DiscoveryCodecError::UnsupportedProtocol;
  QTest::newRow("protocol-type") << qint64(1) << QCborValue(1) << DiscoveryCodecError::UnsupportedProtocol;
  QTest::newRow("future-version")
      << qint64(2) << QCborValue(kDiscoveryProtocolVersion + 1) << DiscoveryCodecError::UnsupportedVersion;
  QTest::newRow("version-type")
      << qint64(2) << QCborValue(QStringLiteral("1")) << DiscoveryCodecError::UnsupportedVersion;
  QTest::newRow("unknown-message-type") << qint64(3) << QCborValue(99) << DiscoveryCodecError::UnknownMessageType;
  QTest::newRow("message-type-type")
      << qint64(3) << QCborValue(QStringLiteral("advertisement")) << DiscoveryCodecError::UnknownMessageType;
}

void DiscoveryCodecTests::protocolVersionAndMessageTypeAreStrict()
{
  QFETCH(qint64, key);
  QFETCH(QCborValue, replacement);
  QFETCH(DiscoveryCodecError, expectedError);

  const auto valid = DiscoveryCodec::encodeAdvertisement(exampleDevice());
  const auto decoded = DiscoveryCodec::decode(replaceEnvelopeValue(valid, key, replacement));

  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, expectedError);
}

void DiscoveryCodecTests::unknownAndMissingEnvelopeFieldsAreRejected()
{
  auto envelope = QCborValue::fromCbor(DiscoveryCodec::encodeAdvertisement(exampleDevice())).toMap();
  envelope.insert(QCborValue(5), QCborValue(true));
  auto decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::InvalidEnvelope);

  envelope.remove(QCborValue(5));
  envelope.remove(QCborValue(4));
  decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::InvalidEnvelope);

  envelope.insert(QCborValue(4), QCborValue(QStringLiteral("not-bytes")));
  decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::InvalidEnvelope);
}

void DiscoveryCodecTests::datagramAndPayloadLimitsAreEnforced()
{
  auto decoded = DiscoveryCodec::decode(QByteArray(kMaximumDiscoveryDatagramBytes + 1, '\0'));
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::DatagramTooLarge);

  auto envelope = QCborValue::fromCbor(DiscoveryCodec::encodeAdvertisement(exampleDevice())).toMap();
  envelope.insert(QCborValue(4), QCborValue(QByteArray(kMaximumDiscoveryPayloadBytes + 1, '\0')));
  decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::PayloadTooLarge);

  envelope.insert(QCborValue(4), QCborValue(QByteArray()));
  decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::PayloadTooLarge);
}

void DiscoveryCodecTests::deviceFieldLimitsAreEnforced()
{
  auto device = exampleDevice();
  device.displayName = QString(257, QLatin1Char('a'));

  QString errorMessage;
  QVERIFY(DiscoveryCodec::encodeAdvertisement(device, &errorMessage).isEmpty());
  QVERIFY(errorMessage.contains(QStringLiteral("display name")));

  auto payloadMap = QCborValue::fromCbor(DeviceInfoCodec::serialize(exampleDevice())).toMap();
  payloadMap.insert(QCborValue(4), QCborValue(QString(257, QLatin1Char('a'))));
  const auto oversizedPayload = QCborValue(payloadMap).toCbor();

  auto envelope = QCborValue::fromCbor(DiscoveryCodec::encodeAdvertisement(exampleDevice())).toMap();
  envelope.insert(QCborValue(4), QCborValue(oversizedPayload));
  const auto decoded = DiscoveryCodec::decode(QCborValue(envelope).toCbor());
  QVERIFY(!decoded.isSuccess());
  QCOMPARE(decoded.error, DiscoveryCodecError::InvalidDeviceInfo);
}

QTEST_MAIN(DiscoveryCodecTests)

#include "DiscoveryCodecTests.moc"
