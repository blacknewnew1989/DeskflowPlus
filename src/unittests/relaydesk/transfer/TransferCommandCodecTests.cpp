// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/TransferCommandCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QtTest>

#include <variant>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId(QStringLiteral("11111111-2222-4333-8444-555555555555"));

QByteArray encodeFrame(const TransferCommandMessage &message)
{
  Frame frame;
  frame.type = messageType(message);
  frame.flags = AckRequired;
  frame.metadata = TransferCommandCodec::encode(message);
  return FrameCodec::encode(frame);
}

QByteArray mutate(const QByteArray &encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto map = QCborValue::fromCbor(encoded).toMap();
  mutation(map);
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

} // namespace

class TransferCommandCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void matchesFrozenPositiveVectors();
  void roundTripsCommands();
  void rejectsInvalidEncodeValues();
  void rejectsEnvelopeAndShapeErrors();
  void rejectsInvalidCancelFields();
};

void TransferCommandCodecTests::matchesFrozenPositiveVectors()
{
  QCOMPARE(
      encodeFrame(TransferPauseMessage{.transferId = kTransferId}).toHex(),
      QByteArrayLiteral("5244465400010300000000010000001300000000000000000000000000000000"
                        "a1015011111111222243338444555555555555")
  );
  QCOMPARE(
      encodeFrame(TransferResumeMessage{.transferId = kTransferId}).toHex(),
      QByteArrayLiteral("5244465400010301000000010000001300000000000000000000000000000000"
                        "a1015011111111222243338444555555555555")
  );
  QCOMPARE(
      encodeFrame(TransferCancelMessage{.transferId = kTransferId}).toHex(),
      QByteArrayLiteral("5244465400010302000000010000001700000000000000000000000000000000"
                        "a3015011111111222243338444555555555555020103f5")
  );
}

void TransferCommandCodecTests::roundTripsCommands()
{
  const QList<TransferCommandMessage> messages = {
      TransferPauseMessage{.transferId = kTransferId},
      TransferResumeMessage{.transferId = kTransferId},
      TransferCancelMessage{
          .transferId = kTransferId,
          .reason = TransferCancelReason::ApplicationShutdown,
          .keepPartial = false,
      },
  };
  for (const auto &expected : messages) {
    const QByteArray encoded = TransferCommandCodec::encode(expected);
    const auto decoded = TransferCommandCodec::decode(kProtocolMajorVersion, messageType(expected), encoded);
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    QCOMPARE(*decoded.message, expected);
  }
}

void TransferCommandCodecTests::rejectsInvalidEncodeValues()
{
  QString error;
  QVERIFY(TransferCommandCodec::encode(TransferPauseMessage{}, &error).isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(TransferCommandCodec::encode(TransferResumeMessage{}, &error).isEmpty());
  QVERIFY(TransferCommandCodec::encode(TransferCancelMessage{}, &error).isEmpty());
  QVERIFY(
      TransferCommandCodec::encode(
          TransferCancelMessage{
              .transferId = kTransferId,
              .reason = static_cast<TransferCancelReason>(99),
          },
          &error
      )
          .isEmpty()
  );
}

void TransferCommandCodecTests::rejectsEnvelopeAndShapeErrors()
{
  const QByteArray valid = TransferCommandCodec::encode(TransferPauseMessage{.transferId = kTransferId});
  QCOMPARE(
      TransferCommandCodec::decode(2, MessageType::TransferPause, valid).error,
      TransferCommandCodecError::UnsupportedVersion
  );
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::Heartbeat, valid).error,
      TransferCommandCodecError::UnsupportedMessageType
  );
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferPause, {}).error,
      TransferCommandCodecError::TooLarge
  );
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferPause, valid + '\0').error,
      TransferCommandCodecError::MalformedCbor
  );
  const QByteArray unknown = mutate(valid, [](QCborMap &map) { map.insert(2, true); });
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferPause, unknown).error,
      TransferCommandCodecError::InvalidFields
  );
  const QByteArray invalidId = mutate(valid, [](QCborMap &map) { map.insert(1, QByteArray(15, '\0')); });
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferPause, invalidId).error,
      TransferCommandCodecError::InvalidTransferId
  );
}

void TransferCommandCodecTests::rejectsInvalidCancelFields()
{
  const QByteArray valid = TransferCommandCodec::encode(TransferCancelMessage{.transferId = kTransferId});
  const QByteArray invalidReason = mutate(valid, [](QCborMap &map) { map.insert(2, 0); });
  QCOMPARE(invalidReason.toHex(), QByteArrayLiteral("a3015011111111222243338444555555555555020003f5"));
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferCancel, invalidReason).error,
      TransferCommandCodecError::InvalidReason
  );
  const QByteArray overflowingReason = mutate(valid, [](QCborMap &map) { map.insert(2, qint64{4'294'967'297}); });
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferCancel, overflowingReason).error,
      TransferCommandCodecError::InvalidReason
  );
  const QByteArray invalidKeep = mutate(valid, [](QCborMap &map) { map.insert(3, 1); });
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferCancel, invalidKeep).error,
      TransferCommandCodecError::InvalidKeepPartial
  );
  const QByteArray missing = mutate(valid, [](QCborMap &map) { map.remove(3); });
  QCOMPARE(
      TransferCommandCodec::decode(kProtocolMajorVersion, MessageType::TransferCancel, missing).error,
      TransferCommandCodecError::InvalidFields
  );
}

QTEST_GUILESS_MAIN(TransferCommandCodecTests)
#include "TransferCommandCodecTests.moc"
