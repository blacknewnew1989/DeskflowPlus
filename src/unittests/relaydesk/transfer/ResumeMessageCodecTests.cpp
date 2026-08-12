// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ResumeMessageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>
#include <variant>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId(QStringLiteral("11111111-2222-4333-8444-555555555555"));
const FileId kFirstFileId(QStringLiteral("10000000-0000-4000-8000-000000000001"));
const FileId kSecondFileId(QStringLiteral("20000000-0000-4000-8000-000000000002"));

ResumeQueryMessage query()
{
  return {.transferId = kTransferId, .manifestSha256 = QByteArray(32, '\xaa')};
}

ResumeResponseMessage response()
{
  return {
      .transferId = kTransferId,
      .manifestSha256 = QByteArray(32, '\xaa'),
      .files = {{kFirstFileId, 12}, {kSecondFileId, 34}},
  };
}

ResumeState storedState(ResumeDirection direction = ResumeDirection::Receiving)
{
  return {
      .transferId = kTransferId,
      .peerDeviceId =
          *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .manifestSha256 = QByteArray(32, '\xaa'),
      .direction = direction,
      .files =
          {
              {kSecondFileId, QStringLiteral("second.bin"), 34, 40, QStringLiteral("second.part")},
              {kFirstFileId, QStringLiteral("first.bin"), 12, 20, QStringLiteral("first.part")},
          },
      .updatedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC),
  };
}

QByteArray encodeRaw(const QCborMap &map)
{
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

QCborMap validResponseMap()
{
  return {
      {QCborValue(1), kTransferId.toRfc4122()},
      {QCborValue(2), QByteArray(32, '\xaa')},
      {QCborValue(3),
       QCborArray{
           QCborMap{{QCborValue(1), kFirstFileId.toRfc4122()}, {QCborValue(2), 12}},
           QCborMap{{QCborValue(1), kSecondFileId.toRfc4122()}, {QCborValue(2), 34}},
       }},
  };
}

QList<ManifestEntry> manifest()
{
  return {
      {.id = kFirstFileId, .relativeProtocolPath = QStringLiteral("first.bin"), .size = 20},
      {.id = kSecondFileId, .relativeProtocolPath = QStringLiteral("second.bin"), .size = 40},
  };
}

} // namespace

class ResumeMessageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTripsQueryWithFrozenEncoding();
  void roundTripsOrderedResponse();
  void encodeRejectsInvalidMessages();
  void decodeRejectsEnvelopeAndFieldErrors();
  void decodeRejectsDuplicateAndUnorderedFiles();
  void buildsResponseOnlyFromMatchingReceiverState();
  void validatesResponseAgainstQueryAndManifest();
};

void ResumeMessageCodecTests::roundTripsQueryWithFrozenEncoding()
{
  QString error;
  const QByteArray encoded = ResumeMessageCodec::encode(ResumeControlMessage{query()}, &error);
  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  QCOMPARE(
      encoded.toHex(), QByteArrayLiteral("a2015011111111222243338444555555555555025820"
                                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
  );
  const auto decoded = ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, encoded);
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  const auto *message = std::get_if<ResumeQueryMessage>(&*decoded.message);
  QVERIFY(message != nullptr);
  QCOMPARE(*message, query());
}

void ResumeMessageCodecTests::roundTripsOrderedResponse()
{
  QString error;
  const QByteArray encoded = ResumeMessageCodec::encode(ResumeControlMessage{response()}, &error);
  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  const auto decoded = ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeResponse, encoded);
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  const auto *message = std::get_if<ResumeResponseMessage>(&*decoded.message);
  QVERIFY(message != nullptr);
  QCOMPARE(*message, response());
}

void ResumeMessageCodecTests::encodeRejectsInvalidMessages()
{
  QString error;
  auto invalidQuery = query();
  invalidQuery.manifestSha256.chop(1);
  QVERIFY(ResumeMessageCodec::encode(ResumeControlMessage{invalidQuery}, &error).isEmpty());
  QVERIFY(!error.isEmpty());

  auto unordered = response();
  std::swap(unordered.files[0], unordered.files[1]);
  QVERIFY(ResumeMessageCodec::encode(ResumeControlMessage{unordered}, &error).isEmpty());

  auto duplicate = response();
  duplicate.files[1].fileId = duplicate.files[0].fileId;
  QVERIFY(ResumeMessageCodec::encode(ResumeControlMessage{duplicate}, &error).isEmpty());

  auto overflow = response();
  overflow.files[0].durableOffset = static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1;
  QVERIFY(ResumeMessageCodec::encode(ResumeControlMessage{overflow}, &error).isEmpty());
}

void ResumeMessageCodecTests::decodeRejectsEnvelopeAndFieldErrors()
{
  const QByteArray valid = ResumeMessageCodec::encode(ResumeControlMessage{query()});
  QCOMPARE(
      ResumeMessageCodec::decode(2, MessageType::ResumeQuery, valid).error, ResumeMessageCodecError::UnsupportedVersion
  );
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::Heartbeat, valid).error,
      ResumeMessageCodecError::UnsupportedMessageType
  );
  QCOMPARE(
      ResumeMessageCodec::decode(
          kProtocolMajorVersion, MessageType::ResumeQuery, QByteArray(kMaximumResumeMetadataBytes + 1, '\0')
      )
          .error,
      ResumeMessageCodecError::TooLarge
  );
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, valid + '\0').error,
      ResumeMessageCodecError::MalformedCbor
  );

  QCborMap unknown = {
      {QCborValue(1), kTransferId.toRfc4122()},
      {QCborValue(2), QByteArray(32, '\xaa')},
      {QCborValue(9), true},
  };
  QVERIFY(ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, encodeRaw(unknown)).ok());
  unknown.insert(QCborValue(QStringLiteral("not-an-integer")), true);
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, encodeRaw(unknown)).error,
      ResumeMessageCodecError::InvalidFields
  );
  QCborMap invalidId = {
      {QCborValue(1), QByteArray(15, '\0')},
      {QCborValue(2), QByteArray(32, '\xaa')},
  };
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, encodeRaw(invalidId)).error,
      ResumeMessageCodecError::InvalidTransferId
  );
  QCborMap invalidHash = {
      {QCborValue(1), kTransferId.toRfc4122()},
      {QCborValue(2), QByteArray(31, '\xaa')},
  };
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeQuery, encodeRaw(invalidHash)).error,
      ResumeMessageCodecError::InvalidManifestHash
  );
}

void ResumeMessageCodecTests::decodeRejectsDuplicateAndUnorderedFiles()
{
  auto duplicate = validResponseMap();
  duplicate.insert(
      QCborValue(3),
      QCborArray{
          QCborMap{{QCborValue(1), kFirstFileId.toRfc4122()}, {QCborValue(2), 12}},
          QCborMap{{QCborValue(1), kFirstFileId.toRfc4122()}, {QCborValue(2), 13}},
      }
  );
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeResponse, encodeRaw(duplicate)).error,
      ResumeMessageCodecError::DuplicateFileId
  );

  auto unordered = validResponseMap();
  unordered.insert(
      QCborValue(3),
      QCborArray{
          QCborMap{{QCborValue(1), kSecondFileId.toRfc4122()}, {QCborValue(2), 34}},
          QCborMap{{QCborValue(1), kFirstFileId.toRfc4122()}, {QCborValue(2), 12}},
      }
  );
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeResponse, encodeRaw(unordered)).error,
      ResumeMessageCodecError::InvalidFileOrder
  );

  auto negative = validResponseMap();
  negative.insert(QCborValue(3), QCborArray{QCborMap{{QCborValue(1), kFirstFileId.toRfc4122()}, {QCborValue(2), -1}}});
  QCOMPARE(
      ResumeMessageCodec::decode(kProtocolMajorVersion, MessageType::ResumeResponse, encodeRaw(negative)).error,
      ResumeMessageCodecError::InvalidOffset
  );
}

void ResumeMessageCodecTests::buildsResponseOnlyFromMatchingReceiverState()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ResumeStore store(temporary.path());
  QVERIFY(store.save(storedState()).ok());

  const auto built = ResumeNegotiator::buildResponse(store, query());
  QVERIFY2(built.ok(), qPrintable(built.diagnostic));
  QCOMPARE(built.response->files, response().files);

  auto unknown = query();
  unknown.transferId = QUuid::createUuid();
  QCOMPARE(ResumeNegotiator::buildResponse(store, unknown).error, ResumeNegotiationError::StateNotFound);

  auto changedManifest = query();
  changedManifest.manifestSha256.fill('\xbb');
  QCOMPARE(ResumeNegotiator::buildResponse(store, changedManifest).error, ResumeNegotiationError::ManifestMismatch);

  QTemporaryDir sendingTemporary;
  QVERIFY(sendingTemporary.isValid());
  ResumeStore sendingStore(sendingTemporary.path());
  QVERIFY(sendingStore.save(storedState(ResumeDirection::Sending)).ok());
  QCOMPARE(ResumeNegotiator::buildResponse(sendingStore, query()).error, ResumeNegotiationError::DirectionMismatch);

  QFile corrupt(store.statePath(kTransferId));
  QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(corrupt.write("bad"), qint64{3});
  corrupt.close();
  QCOMPARE(ResumeNegotiator::buildResponse(store, query()).error, ResumeNegotiationError::StoredStateInvalid);
}

void ResumeMessageCodecTests::validatesResponseAgainstQueryAndManifest()
{
  const auto valid = ResumeNegotiator::validateResponse(query(), response(), manifest());
  QVERIFY2(valid.ok(), qPrintable(valid.diagnostic));
  QCOMPARE(valid.plan->files, response().files);

  auto wrongTransfer = response();
  wrongTransfer.transferId = QUuid::createUuid();
  QCOMPARE(
      ResumeNegotiator::validateResponse(query(), wrongTransfer, manifest()).error,
      ResumeNegotiationError::ResponseMismatch
  );

  auto unknown = response();
  unknown.files[1].fileId = QUuid(QStringLiteral("30000000-0000-4000-8000-000000000003"));
  QCOMPARE(ResumeNegotiator::validateResponse(query(), unknown, manifest()).error, ResumeNegotiationError::UnknownFile);

  auto beyondEnd = response();
  beyondEnd.files[0].durableOffset = 21;
  QCOMPARE(
      ResumeNegotiator::validateResponse(query(), beyondEnd, manifest()).error, ResumeNegotiationError::OffsetOutOfRange
  );

  auto duplicateManifest = manifest();
  duplicateManifest.append(duplicateManifest.constFirst());
  QCOMPARE(
      ResumeNegotiator::validateResponse(query(), response(), duplicateManifest).error,
      ResumeNegotiationError::DuplicateFile
  );
}

QTEST_GUILESS_MAIN(ResumeMessageCodecTests)
#include "ResumeMessageCodecTests.moc"
