// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ControlMessageCodec.h"
#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/ResumeMessageCodec.h"
#include "relaydesk/transfer/SessionMessageCodec.h"
#include "relaydesk/transfer/TransferCommandCodec.h"
#include "relaydesk/transfer/TransferCompletionCodec.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace relaydesk::transfer;

namespace {

struct FrozenVector
{
  QString name;
  QString kind;
  QJsonObject value;
};

QList<FrozenVector> loadVectors()
{
  QFile file(QStringLiteral(RELAYDESK_PROTOCOL_TEST_VECTORS_PATH));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  QList<FrozenVector> result;
  for (const auto value : document.object().value(QStringLiteral("vectors")).toArray()) {
    const auto object = value.toObject();
    result.append({
        .name = object.value(QStringLiteral("name")).toString(),
        .kind = object.value(QStringLiteral("kind")).toString(),
        .value = object,
    });
  }
  return result;
}

QString errorName(FrameDecodeError error)
{
  switch (error) {
  case FrameDecodeError::InvalidMagic:
    return QStringLiteral("InvalidMagic");
  case FrameDecodeError::UnsupportedMajorVersion:
    return QStringLiteral("UnsupportedVersion");
  case FrameDecodeError::UnknownMessageType:
    return QStringLiteral("UnknownMessageType");
  case FrameDecodeError::ControlMetadataTooLarge:
    return QStringLiteral("MetadataTooLarge");
  case FrameDecodeError::DataPayloadTooLarge:
    return QStringLiteral("PayloadTooLarge");
  case FrameDecodeError::UnexpectedPayload:
    return QStringLiteral("UnexpectedPayload");
  case FrameDecodeError::FrameTooLarge:
    return QStringLiteral("FrameTooLarge");
  case FrameDecodeError::LengthOverflow:
    return QStringLiteral("LengthOverflow");
  case FrameDecodeError::ReservedMessageType:
    return QStringLiteral("ReservedMessageType");
  case FrameDecodeError::InvalidFlags:
    return QStringLiteral("InvalidFlags");
  case FrameDecodeError::InvalidStreamId:
    return QStringLiteral("InvalidStreamId");
  case FrameDecodeError::MissingMetadata:
    return QStringLiteral("MissingMetadata");
  case FrameDecodeError::UnexpectedMetadata:
    return QStringLiteral("UnexpectedMetadata");
  case FrameDecodeError::MissingPayload:
    return QStringLiteral("MissingPayload");
  case FrameDecodeError::None:
    return QStringLiteral("None");
  }
  return QStringLiteral("Unknown");
}

QString errorName(SessionMessageError error)
{
  switch (error) {
  case SessionMessageError::None: return QStringLiteral("None");
  case SessionMessageError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case SessionMessageError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case SessionMessageError::TooLarge: return QStringLiteral("TooLarge");
  case SessionMessageError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case SessionMessageError::MetadataNotMap: return QStringLiteral("MetadataNotMap");
  case SessionMessageError::InvalidFields: return QStringLiteral("InvalidFields");
  case SessionMessageError::InvalidDeviceId: return QStringLiteral("InvalidDeviceId");
  case SessionMessageError::InvalidSessionId: return QStringLiteral("InvalidSessionId");
  case SessionMessageError::InvalidAppVersion: return QStringLiteral("InvalidAppVersion");
  case SessionMessageError::InvalidVersions: return QStringLiteral("InvalidVersions");
  case SessionMessageError::InvalidFingerprint: return QStringLiteral("InvalidFingerprint");
  case SessionMessageError::InvalidTimestamp: return QStringLiteral("InvalidTimestamp");
  case SessionMessageError::InvalidAuthResult: return QStringLiteral("InvalidAuthResult");
  case SessionMessageError::InvalidSequence: return QStringLiteral("InvalidSequence");
  case SessionMessageError::InvalidGoodbyeReason: return QStringLiteral("InvalidGoodbyeReason");
  case SessionMessageError::InvalidDiagnostic: return QStringLiteral("InvalidDiagnostic");
  }
  return {};
}

QString errorName(CapabilityCodecError error)
{
  switch (error) {
  case CapabilityCodecError::None: return QStringLiteral("None");
  case CapabilityCodecError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case CapabilityCodecError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case CapabilityCodecError::InvalidFields: return QStringLiteral("InvalidFields");
  case CapabilityCodecError::InvalidFeatures: return QStringLiteral("InvalidFeatures");
  case CapabilityCodecError::InvalidLimits: return QStringLiteral("InvalidLimits");
  case CapabilityCodecError::InvalidConflictPolicies: return QStringLiteral("InvalidConflictPolicies");
  }
  return {};
}

QString errorName(ControlMessageError error)
{
  switch (error) {
  case ControlMessageError::None: return QStringLiteral("None");
  case ControlMessageError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case ControlMessageError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case ControlMessageError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case ControlMessageError::MetadataNotMap: return QStringLiteral("MetadataNotMap");
  case ControlMessageError::NonIntegerKey: return QStringLiteral("NonIntegerKey");
  case ControlMessageError::MissingField: return QStringLiteral("MissingField");
  case ControlMessageError::InvalidFieldType: return QStringLiteral("InvalidFieldType");
  case ControlMessageError::InvalidFieldValue: return QStringLiteral("InvalidFieldValue");
  }
  return {};
}

QString errorName(ManifestPageError error)
{
  switch (error) {
  case ManifestPageError::None: return QStringLiteral("None");
  case ManifestPageError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case ManifestPageError::InvalidLimits: return QStringLiteral("InvalidLimits");
  case ManifestPageError::EmptyManifest: return QStringLiteral("EmptyManifest");
  case ManifestPageError::TooManyEntries: return QStringLiteral("TooManyEntries");
  case ManifestPageError::TooManyPages: return QStringLiteral("TooManyPages");
  case ManifestPageError::EntryTooLarge: return QStringLiteral("EntryTooLarge");
  case ManifestPageError::PageMetadataTooLarge: return QStringLiteral("PageMetadataTooLarge");
  case ManifestPageError::ManifestMetadataTooLarge: return QStringLiteral("ManifestMetadataTooLarge");
  case ManifestPageError::InvalidManifestOrder: return QStringLiteral("InvalidManifestOrder");
  case ManifestPageError::InvalidManifestEntry: return QStringLiteral("InvalidManifestEntry");
  case ManifestPageError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case ManifestPageError::MetadataNotMap: return QStringLiteral("MetadataNotMap");
  case ManifestPageError::NonIntegerKey: return QStringLiteral("NonIntegerKey");
  case ManifestPageError::MissingField: return QStringLiteral("MissingField");
  case ManifestPageError::InvalidFieldType: return QStringLiteral("InvalidFieldType");
  case ManifestPageError::InvalidFieldValue: return QStringLiteral("InvalidFieldValue");
  case ManifestPageError::TransferMismatch: return QStringLiteral("TransferMismatch");
  case ManifestPageError::PageCountMismatch: return QStringLiteral("PageCountMismatch");
  case ManifestPageError::DuplicatePage: return QStringLiteral("DuplicatePage");
  case ManifestPageError::OutOfOrderPage: return QStringLiteral("OutOfOrderPage");
  case ManifestPageError::MissingPage: return QStringLiteral("MissingPage");
  case ManifestPageError::EntryCountMismatch: return QStringLiteral("EntryCountMismatch");
  case ManifestPageError::DigestMismatch: return QStringLiteral("DigestMismatch");
  case ManifestPageError::ProtocolPathCollision: return QStringLiteral("ProtocolPathCollision");
  case ManifestPageError::DuplicateFileId: return QStringLiteral("DuplicateFileId");
  case ManifestPageError::AlreadyComplete: return QStringLiteral("AlreadyComplete");
  }
  return {};
}

QString errorName(FileMessageCodecError error)
{
  switch (error) {
  case FileMessageCodecError::None: return QStringLiteral("None");
  case FileMessageCodecError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case FileMessageCodecError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case FileMessageCodecError::InvalidFields: return QStringLiteral("InvalidFields");
  case FileMessageCodecError::InvalidTransferId: return QStringLiteral("InvalidTransferId");
  case FileMessageCodecError::InvalidFileId: return QStringLiteral("InvalidFileId");
  case FileMessageCodecError::InvalidInteger: return QStringLiteral("InvalidInteger");
  case FileMessageCodecError::InvalidChunkSize: return QStringLiteral("InvalidChunkSize");
  case FileMessageCodecError::InvalidHash: return QStringLiteral("InvalidHash");
  case FileMessageCodecError::InvalidResult: return QStringLiteral("InvalidResult");
  }
  return {};
}

QString errorName(TransferCommandCodecError error)
{
  switch (error) {
  case TransferCommandCodecError::None: return QStringLiteral("None");
  case TransferCommandCodecError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case TransferCommandCodecError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case TransferCommandCodecError::TooLarge: return QStringLiteral("TooLarge");
  case TransferCommandCodecError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case TransferCommandCodecError::InvalidFields: return QStringLiteral("InvalidFields");
  case TransferCommandCodecError::InvalidTransferId: return QStringLiteral("InvalidTransferId");
  case TransferCommandCodecError::InvalidReason: return QStringLiteral("InvalidReason");
  case TransferCommandCodecError::InvalidKeepPartial: return QStringLiteral("InvalidKeepPartial");
  }
  return {};
}

QString errorName(TransferCompletionCodecError error)
{
  switch (error) {
  case TransferCompletionCodecError::None: return QStringLiteral("None");
  case TransferCompletionCodecError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case TransferCompletionCodecError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case TransferCompletionCodecError::TooLarge: return QStringLiteral("TooLarge");
  case TransferCompletionCodecError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case TransferCompletionCodecError::InvalidFields: return QStringLiteral("InvalidFields");
  case TransferCompletionCodecError::InvalidTransferId: return QStringLiteral("InvalidTransferId");
  case TransferCompletionCodecError::InvalidFileCount: return QStringLiteral("InvalidFileCount");
  case TransferCompletionCodecError::InvalidTotalBytes: return QStringLiteral("InvalidTotalBytes");
  case TransferCompletionCodecError::InvalidResultCode: return QStringLiteral("InvalidResultCode");
  case TransferCompletionCodecError::InvalidDiagnostic: return QStringLiteral("InvalidDiagnostic");
  }
  return {};
}

QString errorName(ResumeMessageCodecError error)
{
  switch (error) {
  case ResumeMessageCodecError::None: return QStringLiteral("None");
  case ResumeMessageCodecError::UnsupportedVersion: return QStringLiteral("UnsupportedVersion");
  case ResumeMessageCodecError::UnsupportedMessageType: return QStringLiteral("UnsupportedMessageType");
  case ResumeMessageCodecError::TooLarge: return QStringLiteral("TooLarge");
  case ResumeMessageCodecError::MalformedCbor: return QStringLiteral("MalformedCbor");
  case ResumeMessageCodecError::InvalidFields: return QStringLiteral("InvalidFields");
  case ResumeMessageCodecError::InvalidTransferId: return QStringLiteral("InvalidTransferId");
  case ResumeMessageCodecError::InvalidManifestHash: return QStringLiteral("InvalidManifestHash");
  case ResumeMessageCodecError::TooManyFiles: return QStringLiteral("TooManyFiles");
  case ResumeMessageCodecError::InvalidFileId: return QStringLiteral("InvalidFileId");
  case ResumeMessageCodecError::InvalidOffset: return QStringLiteral("InvalidOffset");
  case ResumeMessageCodecError::DuplicateFileId: return QStringLiteral("DuplicateFileId");
  case ResumeMessageCodecError::InvalidFileOrder: return QStringLiteral("InvalidFileOrder");
  }
  return {};
}

QString metadataErrorName(MessageType type, QByteArrayView metadata)
{
  switch (type) {
  case MessageType::Hello:
    return errorName(SessionMessageCodec::decodeHello(type, metadata).error);
  case MessageType::AuthResult:
    return errorName(SessionMessageCodec::decodeAuthResult(type, metadata).error);
  case MessageType::Capabilities:
    return errorName(CapabilityCodec::decode(type, metadata).error);
  case MessageType::Heartbeat:
  case MessageType::HeartbeatAck:
    return errorName(SessionMessageCodec::decodeHeartbeat(kProtocolMajorVersion, type, metadata).error);
  case MessageType::TransferOffer:
  case MessageType::TransferAccept:
  case MessageType::TransferReject:
  case MessageType::Error:
    return errorName(ControlMessageCodec::decode(kProtocolMajorVersion, type, metadata.toByteArray()).error);
  case MessageType::ManifestPage:
    return errorName(ManifestPageCodec::decode(kProtocolMajorVersion, metadata.toByteArray()).error);
  case MessageType::ManifestComplete:
    return errorName(ManifestPageCodec::decodeComplete(kProtocolMajorVersion, metadata.toByteArray()).error);
  case MessageType::FileBegin:
  case MessageType::FileChunk:
  case MessageType::FileCheckpoint:
  case MessageType::FileEnd:
  case MessageType::FileResult:
    return errorName(FileMessageCodec::decode(type, metadata).error);
  case MessageType::TransferPause:
  case MessageType::TransferResume:
  case MessageType::TransferCancel:
    return errorName(TransferCommandCodec::decode(kProtocolMajorVersion, type, metadata).error);
  case MessageType::TransferComplete:
  case MessageType::TransferResult:
    return errorName(TransferCompletionCodec::decode(kProtocolMajorVersion, type, metadata).error);
  case MessageType::ResumeQuery:
  case MessageType::ResumeResponse:
    return errorName(ResumeMessageCodec::decode(kProtocolMajorVersion, type, metadata).error);
  case MessageType::Goodbye:
    return errorName(SessionMessageCodec::decodeGoodbye(kProtocolMajorVersion, type, metadata).error);
  }
  return {};
}

} // namespace

class ProtocolVectorTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void framePositive_data();
  void framePositive();
  void frameNegative_data();
  void frameNegative();
  void metadataNegative_data();
  void metadataNegative();
};

void ProtocolVectorTests::framePositive_data()
{
  QTest::addColumn<QJsonObject>("vector");
  for (const auto &vector : loadVectors()) {
    if (vector.kind == QStringLiteral("frame-positive")) {
      QTest::newRow(qPrintable(vector.name)) << vector.value;
    }
  }
}

void ProtocolVectorTests::framePositive()
{
  QFETCH(QJsonObject, vector);
  const QByteArray header = QByteArray::fromHex(vector.value(QStringLiteral("headerHex")).toString().toLatin1());
  const QByteArray metadata = QByteArray::fromHex(vector.value(QStringLiteral("metadataHex")).toString().toLatin1());
  const QByteArray payload = QByteArray::fromHex(vector.value(QStringLiteral("payloadHex")).toString().toLatin1());
  QByteArray encoded = header + metadata + payload;
  const QByteArray frozen = encoded;
  Frame frame;
  const auto decoded = FrameCodec::tryDecode(encoded, frame);
  QVERIFY2(decoded.status == FrameDecodeStatus::FrameReady, qPrintable(decoded.diagnostic));
  QVERIFY(encoded.isEmpty());
  QCOMPARE(metadataErrorName(frame.type, frame.metadata), QStringLiteral("None"));
  QString diagnostic;
  QCOMPARE(FrameCodec::encode(frame, {}, &diagnostic), frozen);
}

void ProtocolVectorTests::frameNegative_data()
{
  QTest::addColumn<QJsonObject>("vector");
  for (const auto &vector : loadVectors()) {
    if (vector.kind == QStringLiteral("frame-negative")) {
      QTest::newRow(qPrintable(vector.name)) << vector.value;
    }
  }
}

void ProtocolVectorTests::frameNegative()
{
  QFETCH(QJsonObject, vector);
  QByteArray header = QByteArray::fromHex(vector.value(QStringLiteral("headerHex")).toString().toLatin1());
  Frame frame;
  const auto decoded = FrameCodec::tryDecode(header, frame);
  QVERIFY(decoded.status == FrameDecodeStatus::ProtocolError);
  QCOMPARE(errorName(decoded.error), vector.value(QStringLiteral("expectedError")).toString());
}

void ProtocolVectorTests::metadataNegative_data()
{
  QTest::addColumn<QJsonObject>("vector");
  for (const auto &vector : loadVectors()) {
    if (vector.kind == QStringLiteral("metadata-negative")) {
      QTest::newRow(qPrintable(vector.name)) << vector.value;
    }
  }
}

void ProtocolVectorTests::metadataNegative()
{
  QFETCH(QJsonObject, vector);
  const auto type = static_cast<MessageType>(vector.value(QStringLiteral("messageType")).toInt());
  const QByteArray metadata = QByteArray::fromHex(vector.value(QStringLiteral("metadataHex")).toString().toLatin1());
  const QString actual = metadataErrorName(type, metadata);
  QCOMPARE(actual, vector.value(QStringLiteral("expectedCodecError")).toString());
}

QTEST_GUILESS_MAIN(ProtocolVectorTests)
#include "ProtocolVectorTests.moc"
