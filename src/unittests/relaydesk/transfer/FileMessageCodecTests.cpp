// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileMessageCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

const QUuid kTransferId(QStringLiteral("01234567-89ab-cdef-8123-456789abcdef"));
const QUuid kFileId(QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"));

QByteArray mutate(const QByteArray &encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto map = QCborValue::fromCbor(encoded).toMap();
  mutation(map);
  return QCborValue(map).toCbor();
}

FileMessageDecodeResult roundTrip(const FileControlMessage &message)
{
  QString error;
  const auto encoded = FileMessageCodec::encode(message, &error);
  if (encoded.isEmpty()) {
    return {.error = FileMessageCodecError::InvalidFields, .diagnostic = error};
  }
  return FileMessageCodec::decode(fileMessageType(message), encoded);
}

} // namespace

class FileMessageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void beginRoundTrip();
  void chunkRoundTrip();
  void checkpointRoundTrip();
  void endRoundTrip();
  void resultRoundTrip();
  void encodingRejectsInvalidMessages();
  void rejectsWrongTypeMalformedAndTrailingCbor();
  void rejectsMissingUnknownAndNonIntegerFields();
  void rejectsInvalidIdentifiers();
  void rejectsInvalidBeginBoundsAndHash();
  void rejectsInvalidChunkPosition();
  void rejectsInvalidEndAndResult();
};

void FileMessageCodecTests::beginRoundTrip()
{
  const FileBeginMessage source{kTransferId, kFileId, 12'345'678, 1'048'576, 1'048'576, QByteArray(32, '\x42')};
  const auto decoded = roundTrip(FileControlMessage(source));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<FileBeginMessage>(*decoded.message), source);
}

void FileMessageCodecTests::chunkRoundTrip()
{
  const FileChunkMessage source{kTransferId, kFileId, 4'194'304, 4};
  const auto decoded = roundTrip(FileControlMessage(source));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<FileChunkMessage>(*decoded.message), source);
}

void FileMessageCodecTests::checkpointRoundTrip()
{
  const FileCheckpointMessage source{kTransferId, kFileId, 8'388'608};
  const auto decoded = roundTrip(FileControlMessage(source));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<FileCheckpointMessage>(*decoded.message), source);
}

void FileMessageCodecTests::endRoundTrip()
{
  const FileEndMessage source{kTransferId, kFileId, 12'345'678, QByteArray(32, '\x31')};
  const auto decoded = roundTrip(FileControlMessage(source));
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(std::get<FileEndMessage>(*decoded.message), source);
}

void FileMessageCodecTests::resultRoundTrip()
{
  const QList<FileResultMessage> messages = {
      {kTransferId, kFileId, FileResultCode::Ok, {}},
      {kTransferId, kFileId, FileResultCode::HashMismatch, QStringLiteral("computed hash differs")},
      {kTransferId, kFileId, FileResultCode::Cancelled, QStringLiteral("cancelled by peer")},
  };
  for (const auto &source : messages) {
    const auto decoded = roundTrip(FileControlMessage(source));
    QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
    QCOMPARE(std::get<FileResultMessage>(*decoded.message), source);
  }
}

void FileMessageCodecTests::encodingRejectsInvalidMessages()
{
  QString error;
  QVERIFY(FileMessageCodec::encode(
              FileControlMessage(FileBeginMessage{QUuid{}, kFileId, 1, 0, 1024, QByteArray(32, '\x01')}), &error
  )
              .isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(FileMessageCodec::encode(
              FileControlMessage(FileResultMessage{kTransferId, kFileId, FileResultCode::Ok, QStringLiteral("error")}),
              &error
  )
              .isEmpty());
  QVERIFY(FileMessageCodec::encode(
              FileControlMessage(FileResultMessage{kTransferId, kFileId, FileResultCode::IoError, {}}), &error
  )
              .isEmpty());
}

void FileMessageCodecTests::rejectsWrongTypeMalformedAndTrailingCbor()
{
  const auto valid = FileMessageCodec::encode(FileControlMessage(FileChunkMessage{kTransferId, kFileId, 0, 0}));
  QCOMPARE(
      FileMessageCodec::decode(MessageType::Heartbeat, valid).error, FileMessageCodecError::UnsupportedMessageType
  );
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileChunk, QByteArrayLiteral("broken")).error,
      FileMessageCodecError::MalformedCbor
  );
  auto trailing = valid;
  trailing.append('\0');
  QCOMPARE(FileMessageCodec::decode(MessageType::FileChunk, trailing).error, FileMessageCodecError::MalformedCbor);
}

void FileMessageCodecTests::rejectsMissingUnknownAndNonIntegerFields()
{
  const auto valid = FileMessageCodec::encode(FileControlMessage(FileChunkMessage{kTransferId, kFileId, 0, 0}));
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.remove(4); })).error,
      FileMessageCodecError::InvalidFields
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(99, true); })
      ).error,
      FileMessageCodecError::InvalidFields
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(QStringLiteral("offset"), 0); })
      ).error,
      FileMessageCodecError::InvalidFields
  );
}

void FileMessageCodecTests::rejectsInvalidIdentifiers()
{
  const auto valid = FileMessageCodec::encode(FileControlMessage(FileChunkMessage{kTransferId, kFileId, 0, 0}));
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(1, QByteArray(15, '\0')); })
      ).error,
      FileMessageCodecError::InvalidTransferId
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(2, QByteArray(16, '\0')); })
      ).error,
      FileMessageCodecError::InvalidFileId
  );
}

void FileMessageCodecTests::rejectsInvalidBeginBoundsAndHash()
{
  const auto valid = FileMessageCodec::encode(
      FileControlMessage(FileBeginMessage{kTransferId, kFileId, 100, 0, 64, QByteArray(32, '\x20')})
  );
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileBegin, mutate(valid, [](QCborMap &map) { map.insert(4, 101); })).error,
      FileMessageCodecError::InvalidInteger
  );
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileBegin, mutate(valid, [](QCborMap &map) { map.insert(5, 0); })).error,
      FileMessageCodecError::InvalidChunkSize
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileBegin, mutate(valid, [](QCborMap &map) { map.insert(5, 4 * 1024 * 1024 + 1); })
      ).error,
      FileMessageCodecError::InvalidChunkSize
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileBegin, mutate(valid, [](QCborMap &map) { map.insert(6, QByteArray(31, '\0')); })
      ).error,
      FileMessageCodecError::InvalidHash
  );
}

void FileMessageCodecTests::rejectsInvalidChunkPosition()
{
  const auto valid = FileMessageCodec::encode(FileControlMessage(FileChunkMessage{kTransferId, kFileId, 0, 0}));
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(3, -1); })).error,
      FileMessageCodecError::InvalidInteger
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileChunk, mutate(valid, [](QCborMap &map) { map.insert(4, QStringLiteral("zero")); })
      ).error,
      FileMessageCodecError::InvalidInteger
  );
}

void FileMessageCodecTests::rejectsInvalidEndAndResult()
{
  const auto end =
      FileMessageCodec::encode(FileControlMessage(FileEndMessage{kTransferId, kFileId, 1, QByteArray(32, '\x12')}));
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileEnd, mutate(end, [](QCborMap &map) { map.insert(4, QByteArray{}); })
      ).error,
      FileMessageCodecError::InvalidHash
  );

  const auto ok =
      FileMessageCodec::encode(FileControlMessage(FileResultMessage{kTransferId, kFileId, FileResultCode::Ok, {}}));
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileResult, mutate(ok, [](QCborMap &map) { map.insert(3, 99); })).error,
      FileMessageCodecError::InvalidResult
  );
  QCOMPARE(
      FileMessageCodec::decode(
          MessageType::FileResult, mutate(ok, [](QCborMap &map) { map.insert(4, QStringLiteral("unexpected")); })
      ).error,
      FileMessageCodecError::InvalidResult
  );
  const auto failedWithoutDiagnostic = mutate(ok, [](QCborMap &map) { map.insert(3, 8); });
  QCOMPARE(
      FileMessageCodec::decode(MessageType::FileResult, failedWithoutDiagnostic).error,
      FileMessageCodecError::InvalidResult
  );
}

QTEST_MAIN(FileMessageCodecTests)

#include "FileMessageCodecTests.moc"
