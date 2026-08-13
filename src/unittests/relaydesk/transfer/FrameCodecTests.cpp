// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/SessionMessageCodec.h"

#include <QTest>
#include <QtEndian>

#include <limits>

using namespace relaydesk::transfer;

class FrameCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void matchesFrozenHeartbeatVector();
  void matchesFrozenFileChunkVector();
  void roundTripControlMetadata();
  void fragmentedFrameLeavesBufferUntouched();
  void stickyFramesConsumeExactlyOne();
  void rejectsInvalidMagic();
  void rejectsUnsupportedMajor();
  void rejectsUnknownMessageType();
  void rejectsControlMetadataAboveLimit();
  void rejectsDataPayloadAboveLimit();
  void rejectsPayloadOnControlMessage();
  void rejectsFrameAboveAggregateLimit();
  void rejectsLengthOverflow();
  void rejectsUnknownFlagBit();
  void rejectsWrongFlagCombination();
  void rejectsInvalidControlAndFileStreams();
  void rejectsMissingMetadataAndPayload();
};

void FrameCodecTests::matchesFrozenHeartbeatVector()
{
  Frame frame;
  frame.type = MessageType::Heartbeat;
  frame.flags = AckRequired;
  frame.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 7, .timestampMs = 1'730'000'000'000ULL}
  );

  QString error;
  const QByteArray encoded = FrameCodec::encode(frame, {}, &error);

  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  QCOMPARE(
      encoded.toHex(),
      QByteArrayLiteral("5244465400010004000000010000000d00000000000000000000000000000000"
                        "a20107021b00000192cc091400")
  );
}

void FrameCodecTests::matchesFrozenFileChunkVector()
{
  Frame frame;
  frame.type = MessageType::FileChunk;
  frame.flags = AckRequired;
  frame.streamId = 42;
  frame.metadata = FileMessageCodec::encode(FileChunkMessage{
      .transferId = QUuid(QStringLiteral("11111111-2222-4333-8444-555555555555")),
      .fileId = QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
  });
  frame.payload = QByteArrayLiteral("ABC");

  QString error;
  const QByteArray encoded = FrameCodec::encode(frame, {}, &error);

  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  QCOMPARE(
      encoded.toHex(),
      QByteArrayLiteral("524446540001020100000001000000290000000000000003000000000000002a"
                        "a40150111111112222433384445555555555550250aaaaaaaabbbb4ccc8dddeeeeeeeeeeee"
                        "03000400414243")
  );
}

void FrameCodecTests::roundTripControlMetadata()
{
  Frame source;
  source.type = MessageType::TransferOffer;
  source.flags = AckRequired;
  source.streamId = 0;
  source.metadata = QByteArray::fromHex("a101500123456789abcdef8123456789abcdef");

  QByteArray buffer = FrameCodec::encode(source);
  QVERIFY(!buffer.isEmpty());

  Frame decoded;
  const FrameDecodeResult result = FrameCodec::tryDecode(buffer, decoded);

  QVERIFY(result.status == FrameDecodeStatus::FrameReady);
  QVERIFY(result.error == FrameDecodeError::None);
  QCOMPARE(result.consumedBytes, kFixedHeaderBytes + source.metadata.size());
  QVERIFY(decoded == source);
  QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::fragmentedFrameLeavesBufferUntouched()
{
  Frame source;
  source.type = MessageType::FileChunk;
  source.streamId = 9;
  source.metadata = QByteArrayLiteral("metadata");
  source.payload = QByteArrayLiteral("payload");
  const QByteArray encoded = FrameCodec::encode(source);
  QVERIFY(!encoded.isEmpty());

  QByteArray buffer;
  Frame decoded;
  for (qsizetype index = 0; index < encoded.size() - 1; ++index) {
    buffer.append(encoded.at(index));
    const QByteArray beforeDecode = buffer;
    const FrameDecodeResult result = FrameCodec::tryDecode(buffer, decoded);
    QVERIFY(result.status == FrameDecodeStatus::NeedMoreData);
    QCOMPARE(result.consumedBytes, 0);
    QCOMPARE(buffer, beforeDecode);
  }

  buffer.append(encoded.back());
  const FrameDecodeResult result = FrameCodec::tryDecode(buffer, decoded);
  QVERIFY(result.status == FrameDecodeStatus::FrameReady);
  QVERIFY(decoded == source);
  QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::stickyFramesConsumeExactlyOne()
{
  Frame first;
  first.type = MessageType::Heartbeat;
  first.flags = AckRequired;
  first.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 1, .timestampMs = 1}
  );
  Frame second;
  second.type = MessageType::FileChunk;
  second.streamId = 42;
  second.metadata = QByteArrayLiteral("metadata");
  second.payload = QByteArrayLiteral("ABC");
  const QByteArray secondEncoded = FrameCodec::encode(second);
  QByteArray buffer = FrameCodec::encode(first) + secondEncoded;

  Frame decoded;
  const FrameDecodeResult firstResult = FrameCodec::tryDecode(buffer, decoded);
  QVERIFY(firstResult.status == FrameDecodeStatus::FrameReady);
  QVERIFY(decoded == first);
  QCOMPARE(buffer, secondEncoded);

  const FrameDecodeResult secondResult = FrameCodec::tryDecode(buffer, decoded);
  QVERIFY(secondResult.status == FrameDecodeStatus::FrameReady);
  QVERIFY(decoded == second);
  QVERIFY(buffer.isEmpty());
}

void FrameCodecTests::rejectsInvalidMagic()
{
  QByteArray buffer = QByteArray::fromHex("0000000000010004000000000000000000000000000000000000000000000000");
  const QByteArray original = buffer;
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::InvalidMagic);
  QCOMPARE(buffer, original);
}

void FrameCodecTests::rejectsUnsupportedMajor()
{
  QByteArray buffer = QByteArray::fromHex("5244465400020004000000000000000000000000000000000000000000000000");
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::UnsupportedMajorVersion);
}

void FrameCodecTests::rejectsUnknownMessageType()
{
  QByteArray buffer = QByteArray::fromHex("5244465400011234000000000000000000000000000000000000000000000000");
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::UnknownMessageType);
}

void FrameCodecTests::rejectsControlMetadataAboveLimit()
{
  QByteArray buffer = QByteArray::fromHex("5244465400010004000000000010000100000000000000000000000000000000");
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::ControlMetadataTooLarge);
}

void FrameCodecTests::rejectsDataPayloadAboveLimit()
{
  QByteArray buffer = QByteArray::fromHex("5244465400010201000000000000000000000000004000010000000000000001");
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::DataPayloadTooLarge);
}

void FrameCodecTests::rejectsPayloadOnControlMessage()
{
  Frame heartbeat;
  heartbeat.type = MessageType::Heartbeat;
  heartbeat.flags = AckRequired;
  heartbeat.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 1, .timestampMs = 1}
  );
  QByteArray buffer = FrameCodec::encode(heartbeat);
  qToBigEndian<quint64>(1, reinterpret_cast<uchar *>(buffer.data() + 16));
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::UnexpectedPayload);
}

void FrameCodecTests::rejectsFrameAboveAggregateLimit()
{
  Frame frame;
  frame.type = MessageType::FileChunk;
  frame.streamId = 1;
  frame.metadata = QByteArrayLiteral("m");
  frame.payload = QByteArrayLiteral("p");
  QByteArray buffer = FrameCodec::encode(frame);
  qToBigEndian<quint32>(4, reinterpret_cast<uchar *>(buffer.data() + 12));
  qToBigEndian<quint64>(4, reinterpret_cast<uchar *>(buffer.data() + 16));
  ProtocolLimits limits;
  limits.maxControlMetadataBytes = 4;
  limits.maxDataPayloadBytes = 4;
  limits.maxFrameBytes = kFixedHeaderBytes + 7;
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output, limits);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::FrameTooLarge);
}

void FrameCodecTests::rejectsLengthOverflow()
{
  Frame frame;
  frame.type = MessageType::FileChunk;
  frame.streamId = 1;
  frame.metadata = QByteArrayLiteral("m");
  frame.payload = QByteArrayLiteral("p");
  QByteArray buffer = FrameCodec::encode(frame);
  qToBigEndian<quint64>(std::numeric_limits<quint64>::max(), reinterpret_cast<uchar *>(buffer.data() + 16));
  ProtocolLimits limits;
  limits.maxDataPayloadBytes = std::numeric_limits<quint64>::max();
  limits.maxFrameBytes = std::numeric_limits<quint64>::max();
  Frame output;
  const auto result = FrameCodec::tryDecode(buffer, output, limits);

  QVERIFY(result.status == FrameDecodeStatus::ProtocolError);
  QVERIFY(result.error == FrameDecodeError::LengthOverflow);
}

void FrameCodecTests::rejectsUnknownFlagBit()
{
  Frame frame;
  frame.type = MessageType::Heartbeat;
  frame.flags = AckRequired;
  frame.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 1, .timestampMs = 1}
  );
  QByteArray buffer = FrameCodec::encode(frame);
  qToBigEndian<quint32>(AckRequired | 0x80000000U, reinterpret_cast<uchar *>(buffer.data() + 8));
  Frame output;
  QCOMPARE(FrameCodec::tryDecode(buffer, output).error, FrameDecodeError::InvalidFlags);
}

void FrameCodecTests::rejectsWrongFlagCombination()
{
  Frame frame;
  frame.type = MessageType::Heartbeat;
  frame.flags = AckRequired;
  frame.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 1, .timestampMs = 1}
  );
  QByteArray buffer = FrameCodec::encode(frame);
  qToBigEndian<quint32>(Response | Final, reinterpret_cast<uchar *>(buffer.data() + 8));
  Frame output;
  QCOMPARE(FrameCodec::tryDecode(buffer, output).error, FrameDecodeError::InvalidFlags);
}

void FrameCodecTests::rejectsInvalidControlAndFileStreams()
{
  Frame heartbeat;
  heartbeat.type = MessageType::Heartbeat;
  heartbeat.flags = AckRequired;
  heartbeat.metadata = SessionMessageCodec::encodeHeartbeat(
      MessageType::Heartbeat, HeartbeatMessage{.sequence = 1, .timestampMs = 1}
  );
  QByteArray control = FrameCodec::encode(heartbeat);
  qToBigEndian<quint64>(1, reinterpret_cast<uchar *>(control.data() + 24));
  Frame output;
  QCOMPARE(FrameCodec::tryDecode(control, output).error, FrameDecodeError::InvalidStreamId);

  Frame chunk;
  chunk.type = MessageType::FileChunk;
  chunk.streamId = 1;
  chunk.metadata = QByteArrayLiteral("m");
  chunk.payload = QByteArrayLiteral("p");
  QByteArray file = FrameCodec::encode(chunk);
  qToBigEndian<quint64>(0, reinterpret_cast<uchar *>(file.data() + 24));
  QCOMPARE(FrameCodec::tryDecode(file, output).error, FrameDecodeError::InvalidStreamId);
}

void FrameCodecTests::rejectsMissingMetadataAndPayload()
{
  Frame heartbeat;
  heartbeat.type = MessageType::Heartbeat;
  heartbeat.flags = AckRequired;
  heartbeat.metadata = QByteArrayLiteral("m");
  QByteArray missingMetadata = FrameCodec::encode(heartbeat);
  qToBigEndian<quint32>(0, reinterpret_cast<uchar *>(missingMetadata.data() + 12));
  missingMetadata.chop(1);
  Frame output;
  QCOMPARE(FrameCodec::tryDecode(missingMetadata, output).error, FrameDecodeError::MissingMetadata);

  Frame chunk;
  chunk.type = MessageType::FileChunk;
  chunk.streamId = 1;
  chunk.metadata = QByteArrayLiteral("m");
  chunk.payload = QByteArrayLiteral("p");
  QByteArray missingPayload = FrameCodec::encode(chunk);
  qToBigEndian<quint64>(0, reinterpret_cast<uchar *>(missingPayload.data() + 16));
  missingPayload.chop(1);
  QCOMPARE(FrameCodec::tryDecode(missingPayload, output).error, FrameDecodeError::MissingPayload);
}

QTEST_MAIN(FrameCodecTests)

#include "FrameCodecTests.moc"
