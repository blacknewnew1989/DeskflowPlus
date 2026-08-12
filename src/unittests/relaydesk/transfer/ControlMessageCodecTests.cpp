// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ControlMessageCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QTest>

#include <variant>

using namespace relaydesk::transfer;

namespace {

const QUuid kTransferId(QStringLiteral("01234567-89ab-cdef-8123-456789abcdef"));
const QUuid kFileId(QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"));

ControlMessageDecodeResult roundTrip(const ControlMessage &source)
{
  QString error;
  const QByteArray metadata = ControlMessageCodec::encode(kProtocolMajorVersion, source, &error);
  if (metadata.isEmpty()) {
    return {
        .message = std::nullopt,
        .error = ControlMessageError::InvalidFieldValue,
        .diagnostic = error,
    };
  }
  return ControlMessageCodec::decode(kProtocolMajorVersion, messageType(source), metadata);
}

QCborMap validOfferMap()
{
  QCborMap map;
  map.insert(QCborValue(1), QCborValue(kTransferId.toRfc4122()));
  map.insert(QCborValue(2), QCborValue(QStringLiteral("Project")));
  map.insert(QCborValue(3), QCborValue(1234));
  map.insert(QCborValue(4), QCborValue(2));
  map.insert(QCborValue(5), QCborValue(1));
  map.insert(QCborValue(6), QCborValue(QByteArray(kSha256Bytes, '\x23')));
  map.insert(QCborValue(7), QCborValue(1));
  map.insert(QCborValue(8), QCborValue(QStringLiteral("ask")));
  map.insert(QCborValue(9), QCborValue(1730000000000LL));
  return map;
}

} // namespace

class ControlMessageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void offerRoundTrip();
  void acceptRoundTripPreservesUnicodePathMetadata();
  void rejectRoundTripSupportsOptionalDiagnostic();
  void rejectsUnknownRejectReason();
  void errorRoundTripPreservesOptionalIds();
  void rejectsUnsupportedVersion();
  void rejectsUnsupportedMessageType();
  void rejectsMalformedCbor();
  void rejectsTrailingCborValue();
  void rejectsMetadataThatIsNotAMap();
  void rejectsMissingRequiredField();
  void rejectsWrongFieldType();
  void rejectsInvalidUuidLength();
  void rejectsInvalidManifestHashLength();
  void rejectsNonIntegerKey();
  void ignoresUnknownIntegerKey();
};

void ControlMessageCodecTests::offerRoundTrip()
{
  TransferOffer source;
  source.transferId = kTransferId;
  source.displayName = QStringLiteral("设计资料 📁");
  source.totalBytes = 12'345'678'901ULL;
  source.fileCount = 42;
  source.directoryCount = 8;
  source.manifestSha256 = QByteArray(kSha256Bytes, '\x42');
  source.manifestPageCount = 3;
  source.requestedConflictPolicy = ConflictPolicy::Ask;
  source.createdAtMs = 1'730'000'000'000ULL;

  const auto result = roundTrip(ControlMessage(source));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  const auto *decoded = std::get_if<TransferOffer>(&*result.message);
  QVERIFY(decoded != nullptr);
  QVERIFY(*decoded == source);
}

void ControlMessageCodecTests::acceptRoundTripPreservesUnicodePathMetadata()
{
  TransferAccept source;
  source.transferId = kTransferId;
  source.effectiveConflictPolicy = ConflictPolicy::AutoRename;
  source.logicalDestination = QStringLiteral("下载/RelayDesk/客户资料 📁");
  source.freeBytes = 98'765'432'100ULL;
  source.autoAccepted = true;

  const auto result = roundTrip(ControlMessage(source));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  const auto *decoded = std::get_if<TransferAccept>(&*result.message);
  QVERIFY(decoded != nullptr);
  QVERIFY(*decoded == source);
}

void ControlMessageCodecTests::rejectRoundTripSupportsOptionalDiagnostic()
{
  const QList<TransferReject> messages = {
      {.transferId = kTransferId, .reason = RejectReason::UserDeclined},
      {.transferId = kTransferId,
       .reason = RejectReason::UnsupportedCapability,
       .diagnostic = QStringLiteral("folder.v1 is unavailable")},
  };
  for (const auto &source : messages) {
    const auto result = roundTrip(ControlMessage(source));
    QVERIFY2(result.ok(), qPrintable(result.diagnostic));
    const auto *decoded = std::get_if<TransferReject>(&*result.message);
    QVERIFY(decoded != nullptr);
    QCOMPARE(*decoded, source);
  }
}

void ControlMessageCodecTests::rejectsUnknownRejectReason()
{
  QCborMap map;
  map.insert(QCborValue(1), QCborValue(kTransferId.toRfc4122()));
  map.insert(QCborValue(2), QCborValue(99));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferReject, QCborValue(map).toCbor());

  QCOMPARE(result.error, ControlMessageError::InvalidFieldValue);
}

void ControlMessageCodecTests::errorRoundTripPreservesOptionalIds()
{
  ErrorMessage source;
  source.code = 1002;
  source.diagnostic = QStringLiteral("INVALID_FRAME_LENGTH");
  source.retryable = false;
  source.transferId = kTransferId;
  source.fileId = kFileId;

  const auto result = roundTrip(ControlMessage(source));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  const auto *decoded = std::get_if<ErrorMessage>(&*result.message);
  QVERIFY(decoded != nullptr);
  QVERIFY(*decoded == source);
}

void ControlMessageCodecTests::rejectsUnsupportedVersion()
{
  const QByteArray metadata = QCborValue(validOfferMap()).toCbor();
  const auto result = ControlMessageCodec::decode(2, MessageType::TransferOffer, metadata);

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::UnsupportedVersion);
}

void ControlMessageCodecTests::rejectsUnsupportedMessageType()
{
  const QByteArray metadata = QCborValue(validOfferMap()).toCbor();
  const auto result = ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::Heartbeat, metadata);

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::UnsupportedMessageType);
}

void ControlMessageCodecTests::rejectsMalformedCbor()
{
  const QByteArray truncatedMap(1, static_cast<char>(0xbf));
  const auto result = ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, truncatedMap);

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::MalformedCbor);
}

void ControlMessageCodecTests::rejectsTrailingCborValue()
{
  const QByteArray metadata = QCborValue(validOfferMap()).toCbor() + QCborValue(0).toCbor();
  const auto result = ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, metadata);

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::MalformedCbor);
}

void ControlMessageCodecTests::rejectsMetadataThatIsNotAMap()
{
  const QByteArray metadata = QCborValue(QStringLiteral("not a map")).toCbor();
  const auto result = ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, metadata);

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::MetadataNotMap);
}

void ControlMessageCodecTests::rejectsMissingRequiredField()
{
  QCborMap map = validOfferMap();
  map.remove(QCborValue(6));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::MissingField);
}

void ControlMessageCodecTests::rejectsWrongFieldType()
{
  QCborMap map = validOfferMap();
  map.insert(QCborValue(3), QCborValue(QStringLiteral("1234")));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::InvalidFieldType);
}

void ControlMessageCodecTests::rejectsInvalidUuidLength()
{
  QCborMap map = validOfferMap();
  map.insert(QCborValue(1), QCborValue(QByteArray(15, '\x01')));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::InvalidFieldValue);
}

void ControlMessageCodecTests::rejectsInvalidManifestHashLength()
{
  QCborMap map = validOfferMap();
  map.insert(QCborValue(6), QCborValue(QByteArray(31, '\x23')));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::InvalidFieldValue);
}

void ControlMessageCodecTests::rejectsNonIntegerKey()
{
  QCborMap map = validOfferMap();
  map.insert(QCborValue(QStringLiteral("future")), QCborValue(1));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY(!result.ok());
  QVERIFY(result.error == ControlMessageError::NonIntegerKey);
}

void ControlMessageCodecTests::ignoresUnknownIntegerKey()
{
  QCborMap map = validOfferMap();
  map.insert(QCborValue(99), QCborValue(QStringLiteral("future optional value")));

  const auto result =
      ControlMessageCodec::decode(kProtocolMajorVersion, MessageType::TransferOffer, QCborValue(map).toCbor());

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
}

QTEST_MAIN(ControlMessageCodecTests)

#include "ControlMessageCodecTests.moc"
