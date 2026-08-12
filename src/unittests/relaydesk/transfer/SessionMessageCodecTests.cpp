// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/SessionMessageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

HelloMessage hello()
{
  return {
      .deviceId = deskflow::relaydesk::DeviceId::generate(),
      .sessionId = QUuid::createUuid(),
      .appVersion = QStringLiteral("0.1.0"),
      .supportedMajorVersions = {1},
      .certificateFingerprintSha256 = QByteArray(32, '\x42'),
      .timestampMs = 1'730'000'000'000ULL,
  };
}

QByteArray mutate(const QByteArray &encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto map = QCborValue::fromCbor(encoded).toMap();
  mutation(map);
  return QCborValue(map).toCbor();
}

} // namespace

class SessionMessageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void helloRoundTrip();
  void authResultRoundTrip();
  void deterministicEncoding();
  void rejectsWrongMessageTypes();
  void rejectsMalformedAndTrailingCbor();
  void rejectsHelloFieldShapeAndLengths();
  void rejectsInvalidVersions();
  void rejectsUnsafeAuthCombinations();
};

void SessionMessageCodecTests::helloRoundTrip()
{
  const auto expected = hello();
  const auto decoded = SessionMessageCodec::decodeHello(MessageType::Hello, SessionMessageCodec::encodeHello(expected));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(*decoded.message, expected);
}

void SessionMessageCodecTests::authResultRoundTrip()
{
  const QList<AuthResultMessage> messages = {
      {.accepted = true},
      {.accepted = false, .errorCode = 7, .diagnostic = QStringLiteral("fingerprint mismatch")},
  };
  for (const auto &expected : messages) {
    const auto decoded =
        SessionMessageCodec::decodeAuthResult(MessageType::AuthResult, SessionMessageCodec::encodeAuthResult(expected));
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    QCOMPARE(*decoded.message, expected);
  }
}

void SessionMessageCodecTests::deterministicEncoding()
{
  const auto message = hello();
  QCOMPARE(SessionMessageCodec::encodeHello(message), SessionMessageCodec::encodeHello(message));
  QCOMPARE(SessionMessageCodec::encodeAuthResult({.accepted = true}), QByteArray::fromHex("a101f5"));
}

void SessionMessageCodecTests::rejectsWrongMessageTypes()
{
  const auto encodedHello = SessionMessageCodec::encodeHello(hello());
  QCOMPARE(
      SessionMessageCodec::decodeHello(MessageType::Capabilities, encodedHello).error,
      SessionMessageError::UnsupportedMessageType
  );
  const auto encodedAuth = SessionMessageCodec::encodeAuthResult({.accepted = true});
  QCOMPARE(
      SessionMessageCodec::decodeAuthResult(MessageType::Hello, encodedAuth).error,
      SessionMessageError::UnsupportedMessageType
  );
}

void SessionMessageCodecTests::rejectsMalformedAndTrailingCbor()
{
  QCOMPARE(
      SessionMessageCodec::decodeHello(MessageType::Hello, QByteArrayLiteral("broken")).error,
      SessionMessageError::MalformedCbor
  );
  auto encoded = SessionMessageCodec::encodeHello(hello());
  encoded.append('\0');
  QCOMPARE(SessionMessageCodec::decodeHello(MessageType::Hello, encoded).error, SessionMessageError::MalformedCbor);
}

void SessionMessageCodecTests::rejectsHelloFieldShapeAndLengths()
{
  const auto valid = SessionMessageCodec::encodeHello(hello());
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(QStringLiteral("unknown"), true); })
      ).error,
      SessionMessageError::InvalidFields
  );
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(1, QByteArray(15, '\0')); })
      ).error,
      SessionMessageError::InvalidDeviceId
  );
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(2, QByteArray(16, '\0')); })
      ).error,
      SessionMessageError::InvalidSessionId
  );
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(3, QString(65, QLatin1Char('a'))); })
      ).error,
      SessionMessageError::InvalidAppVersion
  );
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(5, QByteArray(31, '\0')); })
      ).error,
      SessionMessageError::InvalidFingerprint
  );
  QCOMPARE(
      SessionMessageCodec::decodeHello(
          MessageType::Hello, mutate(valid, [](QCborMap &map) { map.insert(6, 0); })
      ).error,
      SessionMessageError::InvalidTimestamp
  );
}

void SessionMessageCodecTests::rejectsInvalidVersions()
{
  const auto valid = SessionMessageCodec::encodeHello(hello());
  QCborArray tooMany;
  for (int index = 0; index < 17; ++index) {
    tooMany.append(index + 1);
  }
  const QList<QCborArray> invalid = {{}, {0}, {1, 1}, {65536}, tooMany};
  for (const auto &versions : invalid) {
    const auto encoded = mutate(valid, [&](QCborMap &map) { map.insert(4, versions); });
    QCOMPARE(SessionMessageCodec::decodeHello(MessageType::Hello, encoded).error, SessionMessageError::InvalidVersions);
  }
}

void SessionMessageCodecTests::rejectsUnsafeAuthCombinations()
{
  QString error;
  QVERIFY(SessionMessageCodec::encodeAuthResult({.accepted = true, .errorCode = 1}, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(SessionMessageCodec::encodeAuthResult({.accepted = false}, &error).isEmpty());

  const auto accepted = SessionMessageCodec::encodeAuthResult({.accepted = true});
  const auto acceptedWithError = mutate(accepted, [](QCborMap &map) { map.insert(2, 1); });
  QCOMPARE(
      SessionMessageCodec::decodeAuthResult(MessageType::AuthResult, acceptedWithError).error,
      SessionMessageError::InvalidAuthResult
  );
  const auto rejectedWithoutDiagnostic = mutate(accepted, [](QCborMap &map) {
    map.insert(1, false);
    map.insert(2, 1);
  });
  QCOMPARE(
      SessionMessageCodec::decodeAuthResult(MessageType::AuthResult, rejectedWithoutDiagnostic).error,
      SessionMessageError::InvalidAuthResult
  );
}

QTEST_MAIN(SessionMessageCodecTests)

#include "SessionMessageCodecTests.moc"
