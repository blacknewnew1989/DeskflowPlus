// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/TransferCompletionCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QtTest>

#include <limits>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId =
    *TransferId::fromString(QStringLiteral("11111111-2222-4333-8444-555555555555"));

QByteArray encodeFrame(const TransferCompletionMessage &message)
{
  Frame frame;
  frame.type = messageType(message);
  frame.flags = frame.type == MessageType::TransferComplete ? AckRequired | Final : Response | Final;
  frame.metadata = TransferCompletionCodec::encode(message);
  return FrameCodec::encode(frame);
}

QByteArray mutate(const QByteArray &encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto map = QCborValue::fromCbor(encoded).toMap();
  mutation(map);
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

} // namespace

class TransferCompletionCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void matchesFrozenPositiveVectors();
  void roundTripsCompletionMessages();
  void rejectsInvalidEncodeValues();
  void rejectsEnvelopeAndShapeErrors();
  void rejectsInvalidCompletionSummary();
  void rejectsInvalidResultCombinations();
};

void TransferCompletionCodecTests::matchesFrozenPositiveVectors()
{
  QCOMPARE(
      encodeFrame(TransferCompleteMessage{
                      .transferId = kTransferId,
                      .completedFiles = 2,
                      .skippedFiles = 1,
                      .totalBytes = 1'048'579,
                  })
          .toHex(),
      QByteArrayLiteral("5244465400010303000000050000001d00000000000000000000000000000000"
                        "a401501111111122224333844455555555555502020301041a00100003")
  );
  QCOMPARE(
      encodeFrame(TransferResultMessage{.transferId = kTransferId}).toHex(),
      QByteArrayLiteral("5244465400010304000000060000001500000000000000000000000000000000"
                        "a20150111111112222433384445555555555550200")
  );
}

void TransferCompletionCodecTests::roundTripsCompletionMessages()
{
  const QList<TransferCompletionMessage> messages = {
      TransferCompleteMessage{.transferId = kTransferId, .completedFiles = 2, .skippedFiles = 1, .totalBytes = 9},
      TransferResultMessage{.transferId = kTransferId},
      TransferResultMessage{
          .transferId = kTransferId,
          .code = TransferResultCode::Partial,
          .diagnostic = QStringLiteral("one file skipped"),
      },
      TransferResultMessage{
          .transferId = kTransferId,
          .code = TransferResultCode::Cancelled,
          .diagnostic = QStringLiteral("cancelled by peer"),
      },
      TransferResultMessage{
          .transferId = kTransferId,
          .code = TransferResultCode::Failed,
          .diagnostic = QStringLiteral("commit failed"),
      },
  };
  for (const auto &expected : messages) {
    const QByteArray encoded = TransferCompletionCodec::encode(expected);
    const auto decoded = TransferCompletionCodec::decode(kProtocolMajorVersion, messageType(expected), encoded);
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    QCOMPARE(*decoded.message, expected);
  }
}

void TransferCompletionCodecTests::rejectsInvalidEncodeValues()
{
  QString error;
  auto tooManyFiles = TransferCompleteMessage{.transferId = kTransferId};
  tooManyFiles.completedFiles = kMaximumCompletedTransferFiles;
  tooManyFiles.skippedFiles = 1;
  QVERIFY(TransferCompletionCodec::encode(tooManyFiles, &error).isEmpty());

  auto tooManyBytes = TransferCompleteMessage{.transferId = kTransferId};
  tooManyBytes.totalBytes = static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1;
  QVERIFY(TransferCompletionCodec::encode(tooManyBytes, &error).isEmpty());

  QVERIFY(
      TransferCompletionCodec::encode(
          TransferResultMessage{
              .transferId = kTransferId,
              .code = static_cast<TransferResultCode>(99),
              .diagnostic = QStringLiteral("unknown"),
          },
          &error
      )
          .isEmpty()
  );
  QVERIFY(
      TransferCompletionCodec::encode(
          TransferResultMessage{.transferId = kTransferId, .diagnostic = QStringLiteral("unexpected")}, &error
      )
          .isEmpty()
  );
  QVERIFY(
      TransferCompletionCodec::encode(
          TransferResultMessage{.transferId = kTransferId, .code = TransferResultCode::Failed}, &error
      )
          .isEmpty()
  );
}

void TransferCompletionCodecTests::rejectsEnvelopeAndShapeErrors()
{
  const QByteArray valid = TransferCompletionCodec::encode(TransferResultMessage{.transferId = kTransferId});
  QCOMPARE(
      TransferCompletionCodec::decode(2, MessageType::TransferResult, valid).error,
      TransferCompletionCodecError::UnsupportedVersion
  );
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::Heartbeat, valid).error,
      TransferCompletionCodecError::UnsupportedMessageType
  );
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, {}).error,
      TransferCompletionCodecError::TooLarge
  );
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, valid + '\0').error,
      TransferCompletionCodecError::MalformedCbor
  );
  const QByteArray unknown = mutate(valid, [](QCborMap &map) { map.insert(9, true); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, unknown).error,
      TransferCompletionCodecError::InvalidFields
  );
  const QByteArray invalidId = mutate(valid, [](QCborMap &map) { map.insert(1, QByteArray(15, '\0')); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, invalidId).error,
      TransferCompletionCodecError::InvalidTransferId
  );
}

void TransferCompletionCodecTests::rejectsInvalidCompletionSummary()
{
  const QByteArray valid = TransferCompletionCodec::encode(
      TransferCompleteMessage{.transferId = kTransferId, .completedFiles = 2, .skippedFiles = 1, .totalBytes = 9}
  );
  const QByteArray negativeFiles = mutate(valid, [](QCborMap &map) { map.insert(2, -1); });
  QCOMPARE(negativeFiles.toHex(), QByteArrayLiteral("a4015011111111222243338444555555555555022003010409"));
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferComplete, negativeFiles).error,
      TransferCompletionCodecError::InvalidFileCount
  );
  const QByteArray tooManyFiles = mutate(valid, [](QCborMap &map) {
    map.insert(2, qint64{kMaximumCompletedTransferFiles});
    map.insert(3, 1);
  });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferComplete, tooManyFiles).error,
      TransferCompletionCodecError::InvalidFileCount
  );
  const QByteArray negativeBytes = mutate(valid, [](QCborMap &map) { map.insert(4, -1); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferComplete, negativeBytes).error,
      TransferCompletionCodecError::InvalidTotalBytes
  );
}

void TransferCompletionCodecTests::rejectsInvalidResultCombinations()
{
  const QByteArray valid = TransferCompletionCodec::encode(TransferResultMessage{.transferId = kTransferId});
  const QByteArray invalidCode = mutate(valid, [](QCborMap &map) { map.insert(2, 99); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, invalidCode).error,
      TransferCompletionCodecError::InvalidResultCode
  );
  const QByteArray okWithDiagnostic = mutate(valid, [](QCborMap &map) { map.insert(3, QStringLiteral("stop")); });
  QCOMPARE(
      okWithDiagnostic.toHex(),
      QByteArrayLiteral("a30150111111112222433384445555555555550200036473746f70")
  );
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, okWithDiagnostic).error,
      TransferCompletionCodecError::InvalidDiagnostic
  );
  const QByteArray failedWithoutDiagnostic = mutate(valid, [](QCborMap &map) { map.insert(2, 3); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, failedWithoutDiagnostic)
          .error,
      TransferCompletionCodecError::InvalidDiagnostic
  );
  const QByteArray nonStringDiagnostic = mutate(failedWithoutDiagnostic, [](QCborMap &map) { map.insert(3, true); });
  QCOMPARE(
      TransferCompletionCodec::decode(kProtocolMajorVersion, MessageType::TransferResult, nonStringDiagnostic).error,
      TransferCompletionCodecError::InvalidDiagnostic
  );
}

QTEST_GUILESS_MAIN(TransferCompletionCodecTests)
#include "TransferCompletionCodecTests.moc"
