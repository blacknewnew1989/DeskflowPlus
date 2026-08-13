/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingMessageCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QTest>
#include <QTimeZone>

#include <type_traits>

using namespace deskflow::relaydesk;

namespace {

static_assert(std::is_same_v<std::underlying_type_t<PairingFailureReason>, quint32>);
static_assert(static_cast<quint32>(PairingFailureReason::None) == 0);
static_assert(static_cast<quint32>(PairingFailureReason::Cancelled) == 1);
static_assert(static_cast<quint32>(PairingFailureReason::CodeMismatch) == 2);
static_assert(static_cast<quint32>(PairingFailureReason::Expired) == 3);
static_assert(static_cast<quint32>(PairingFailureReason::TooManyAttempts) == 4);
static_assert(static_cast<quint32>(PairingFailureReason::TransportFailed) == 5);
static_assert(static_cast<quint32>(PairingFailureReason::TrustStoreWriteFailed) == 6);
static_assert(static_cast<quint32>(PairingFailureReason::CertificateChanged) == 7);
static_assert(static_cast<quint32>(PairingFailureReason::DirectConnectionRequired) == 8);

DeviceInfo deviceInfo(char fingerprintByte = '\x35')
{
  return {
      .deviceId = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .platform = QStringLiteral("macos"),
      .architecture = QStringLiteral("arm64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, fingerprintByte),
  };
}

QByteArray mutate(QByteArray encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto envelope = QCborValue::fromCbor(encoded).toMap();
  mutation(envelope);
  return QCborValue(envelope).toCbor();
}

} // namespace

class PairingMessageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void requestRoundTrip();
  void submissionRoundTrip();
  void acceptedAndRejectedResultRoundTrip();
  void freezesCanonicalResultBytes();
  void encodingRejectsUnsafeValues();
  void rejectsOversizedAndTrailingInput();
  void rejectsEnvelopeVersionsAndTypes();
  void rejectsInvalidSessionAndPayloadShape();
  void rejectsMissingFingerprintAndInvalidSas();
  void rejectsTrailingSenderAndInvalidExpiry();
  void rejectsInvalidResultCombinations();
};

void PairingMessageCodecTests::requestRoundTrip()
{
  const PairingRequest request{
      .pairingSessionId = QUuid::createUuid(),
      .sender = deviceInfo(),
      .expiresAtUtc = QDateTime::fromMSecsSinceEpoch(1'730'000'300'000LL, QTimeZone::UTC),
  };
  const auto decoded = PairingMessageCodec::decode(PairingMessageCodec::encode(PairingMessage(request)));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<PairingRequest>(*decoded.message), request);
}

void PairingMessageCodecTests::submissionRoundTrip()
{
  const PairingCodeSubmission submission{QUuid::createUuid(), deviceInfo('\x42'), QStringLiteral("004200")};
  const auto decoded = PairingMessageCodec::decode(PairingMessageCodec::encode(PairingMessage(submission)));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<PairingCodeSubmission>(*decoded.message), submission);
}

void PairingMessageCodecTests::acceptedAndRejectedResultRoundTrip()
{
  const auto sessionId = QUuid::createUuid();
  const QList<PairingResultMessage> messages = {
      {.pairingSessionId = sessionId, .accepted = true},
      {
          .pairingSessionId = sessionId,
          .accepted = false,
          .failureReason = PairingFailureReason::CodeMismatch,
          .diagnostic = QStringLiteral("peer comparison failed"),
      },
  };
  for (const auto &message : messages) {
    const auto decoded = PairingMessageCodec::decode(PairingMessageCodec::encode(PairingMessage(message)));
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    QCOMPARE(std::get<PairingResultMessage>(*decoded.message), message);
  }
}

void PairingMessageCodecTests::freezesCanonicalResultBytes()
{
  const auto sessionId = QUuid(QStringLiteral("00112233-4455-6677-8899-aabbccddeeff"));
  const auto accepted = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = sessionId,
      .accepted = true,
  }));
  QCOMPARE(
      accepted.toHex(),
      QByteArray("a4017172656c61796465736b2d70616972696e670201030304a3015000112233445566778899aabbccddeeff02f50300")
  );

  const auto rejected = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = sessionId,
      .accepted = false,
      .failureReason = PairingFailureReason::CodeMismatch,
  }));
  QCOMPARE(
      rejected.toHex(),
      QByteArray("a4017172656c61796465736b2d70616972696e670201030304a3015000112233445566778899aabbccddeeff02f40302")
  );
}

void PairingMessageCodecTests::encodingRejectsUnsafeValues()
{
  QString error;
  auto sender = deviceInfo();
  sender.certificateFingerprintSha256.clear();
  QVERIFY(PairingMessageCodec::encode(PairingMessage(PairingRequest{QUuid::createUuid(), sender, {}}), &error).isEmpty()
  );
  QVERIFY(!error.isEmpty());

  QVERIFY(PairingMessageCodec::encode(
              PairingMessage(PairingCodeSubmission{QUuid::createUuid(), deviceInfo(), QStringLiteral("12345x")}), &error
  )
              .isEmpty());
  QVERIFY(PairingMessageCodec::encode(
              PairingMessage(PairingResultMessage{
                  .pairingSessionId = QUuid::createUuid(),
                  .accepted = true,
                  .failureReason = PairingFailureReason::CodeMismatch,
              }),
              &error
  )
              .isEmpty());
  QVERIFY(PairingMessageCodec::encode(
              PairingMessage(PairingResultMessage{
                  .pairingSessionId = QUuid::createUuid(),
                  .accepted = false,
              }),
              &error
  )
              .isEmpty());
  QVERIFY(PairingMessageCodec::encode(
              PairingMessage(PairingResultMessage{
                  .pairingSessionId = QUuid::createUuid(),
                  .accepted = false,
                  .failureReason = static_cast<PairingFailureReason>(99),
              }),
              &error
  )
              .isEmpty());
  QVERIFY(PairingMessageCodec::encode(
              PairingMessage(PairingResultMessage{
                  .pairingSessionId = QUuid::createUuid(),
                  .accepted = false,
                  .failureReason = PairingFailureReason::TransportFailed,
                  .diagnostic = QString(513, QLatin1Char('x')),
              }),
              &error
  )
              .isEmpty());
}

void PairingMessageCodecTests::rejectsOversizedAndTrailingInput()
{
  QCOMPARE(
      PairingMessageCodec::decode(QByteArray(kMaximumPairingMessageBytes + 1, '\0')).error,
      PairingMessageError::TooLarge
  );
  auto encoded = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = QUuid::createUuid(),
      .accepted = true,
  }));
  encoded.append('\0');
  QCOMPARE(PairingMessageCodec::decode(encoded).error, PairingMessageError::MalformedCbor);
}

void PairingMessageCodecTests::rejectsEnvelopeVersionsAndTypes()
{
  const auto valid = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = QUuid::createUuid(),
      .accepted = true,
  }));
  QCOMPARE(
      PairingMessageCodec::decode(mutate(valid, [](QCborMap &map) { map.insert(2, 2); })).error,
      PairingMessageError::UnsupportedVersion
  );
  QCOMPARE(
      PairingMessageCodec::decode(mutate(valid, [](QCborMap &map) { map.insert(3, 99); })).error,
      PairingMessageError::UnsupportedMessageType
  );
  QCOMPARE(
      PairingMessageCodec::decode(mutate(valid, [](QCborMap &map) { map.insert(QStringLiteral("extra"), true); })
      ).error,
      PairingMessageError::InvalidEnvelope
  );
}

void PairingMessageCodecTests::rejectsInvalidSessionAndPayloadShape()
{
  const auto valid = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = QUuid::createUuid(),
      .accepted = true,
  }));
  const auto badSession = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(1, QByteArray(15, '\0'));
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(badSession).error, PairingMessageError::InvalidSessionId);

  const auto extraPayload = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(99, true);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(extraPayload).error, PairingMessageError::InvalidResult);
}

void PairingMessageCodecTests::rejectsMissingFingerprintAndInvalidSas()
{
  auto sender = deviceInfo();
  sender.certificateFingerprintSha256.clear();
  const auto encodedSender = DeviceInfoCodec::serialize(sender);
  QCborMap envelope = {
      {1, QString::fromLatin1(kPairingProtocol)},
      {2, kPairingProtocolVersion},
      {3, static_cast<qint64>(PairingMessageType::CodeSubmission)},
      {4, QCborMap{{1, QUuid::createUuid().toRfc4122()}, {2, encodedSender}, {3, QStringLiteral("12ab56")}}},
  };
  QCOMPARE(PairingMessageCodec::decode(QCborValue(envelope).toCbor()).error, PairingMessageError::InvalidFingerprint);

  auto validSender = DeviceInfoCodec::serialize(deviceInfo());
  auto payload = envelope.value(4).toMap();
  payload.insert(2, validSender);
  envelope.insert(4, payload);
  QCOMPARE(PairingMessageCodec::decode(QCborValue(envelope).toCbor()).error, PairingMessageError::InvalidSas);
}

void PairingMessageCodecTests::rejectsTrailingSenderAndInvalidExpiry()
{
  const PairingRequest request{
      QUuid::createUuid(), deviceInfo(), QDateTime::fromMSecsSinceEpoch(1'730'000'300'000LL, QTimeZone::UTC)
  };
  const auto valid = PairingMessageCodec::encode(PairingMessage(request));
  const auto trailingSender = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    auto sender = payload.value(2).toByteArray();
    sender.append('\0');
    payload.insert(2, sender);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(trailingSender).error, PairingMessageError::InvalidDeviceInfo);

  const auto invalidExpiry = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(3, 0);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(invalidExpiry).error, PairingMessageError::InvalidExpiry);
}

void PairingMessageCodecTests::rejectsInvalidResultCombinations()
{
  const auto valid = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = QUuid::createUuid(),
      .accepted = true,
  }));
  const auto acceptedWithFailure = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(3, static_cast<qint64>(PairingFailureReason::CodeMismatch));
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(acceptedWithFailure).error, PairingMessageError::InvalidResult);

  const auto rejectedWithoutFailure = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(2, false);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(rejectedWithoutFailure).error, PairingMessageError::InvalidResult);

  const auto unknownFailure = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(2, false);
    payload.insert(3, 99);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(unknownFailure).error, PairingMessageError::InvalidResult);

  const auto missingFailure = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.remove(3);
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(missingFailure).error, PairingMessageError::InvalidPayload);

  const auto acceptedWithDiagnostic = mutate(valid, [](QCborMap &envelope) {
    auto payload = envelope.value(4).toMap();
    payload.insert(4, QStringLiteral("remote diagnostic"));
    envelope.insert(4, payload);
  });
  QCOMPARE(PairingMessageCodec::decode(acceptedWithDiagnostic).error, PairingMessageError::InvalidResult);
}

QTEST_MAIN(PairingMessageCodecTests)

#include "PairingMessageCodecTests.moc"
