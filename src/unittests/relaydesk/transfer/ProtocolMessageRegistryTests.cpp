// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ProtocolMessageRegistry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

using namespace relaydesk::transfer;

class ProtocolMessageRegistryTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void registryIsCompleteAndUnique();
  void acceptsEveryFrozenEnvelopeVariant();
  void rejectsInvalidEnvelopeDimensions();
  void cddlAndFrozenVectorsCoverRegistry();
};

void ProtocolMessageRegistryTests::registryIsCompleteAndUnique()
{
  const auto descriptors = protocolMessageDescriptors();
  QCOMPARE(descriptors.size(), static_cast<size_t>(kProtocolMessageTypeCount));

  QSet<quint16> types;
  QSet<QString> names;
  for (const auto &descriptor : descriptors) {
    const auto encodedType = static_cast<quint16>(descriptor.type);
    QVERIFY2(!types.contains(encodedType), descriptor.name);
    QVERIFY2(!names.contains(QString::fromLatin1(descriptor.name)), descriptor.name);
    types.insert(encodedType);
    names.insert(QString::fromLatin1(descriptor.name));
    QCOMPARE(descriptor.classification, ProtocolMessageClassification::Implemented);
    QVERIFY(descriptor.schema != nullptr && descriptor.schema[0] != '\0');
    QVERIFY(descriptor.testTarget != nullptr && descriptor.testTarget[0] != '\0');
    QVERIFY(descriptor.validFlagSetCount > 0 && descriptor.validFlagSetCount <= descriptor.validFlagSets.size());
    QVERIFY(isKnownMessageType(descriptor.type));
    QCOMPARE(protocolMessageDescriptor(descriptor.type), &descriptor);
  }
  QVERIFY(!isKnownMessageType(static_cast<MessageType>(0x1234)));
  QVERIFY(protocolMessageDescriptor(static_cast<MessageType>(0x1234)) == nullptr);
}

void ProtocolMessageRegistryTests::acceptsEveryFrozenEnvelopeVariant()
{
  for (const auto &descriptor : protocolMessageDescriptors()) {
    const quint64 streamId = descriptor.streamRule == ProtocolStreamRule::Zero ? 0 : 1;
    const quint64 metadataBytes = descriptor.metadataRule == ProtocolContentRule::Required ? 1 : 0;
    const quint64 payloadBytes = descriptor.payloadRule == ProtocolContentRule::Required ? 1 : 0;
    for (quint8 index = 0; index < descriptor.validFlagSetCount; ++index) {
      const auto result = validateProtocolEnvelope(
          descriptor.type, descriptor.validFlagSets.at(index), streamId, metadataBytes, payloadBytes
      );
      QVERIFY2(result.ok(), qPrintable(result.diagnostic));
    }
  }
}

void ProtocolMessageRegistryTests::rejectsInvalidEnvelopeDimensions()
{
  QCOMPARE(
      validateProtocolEnvelope(static_cast<MessageType>(0x1234), 0, 0, 1, 0).error,
      ProtocolEnvelopeError::UnknownMessageType
  );
  for (const auto &descriptor : protocolMessageDescriptors()) {
    const quint64 streamId = descriptor.streamRule == ProtocolStreamRule::Zero ? 0 : 1;
    const quint64 wrongStreamId = streamId == 0 ? 1 : 0;
    const quint64 payloadBytes = descriptor.payloadRule == ProtocolContentRule::Required ? 1 : 0;
    const quint32 validFlags = descriptor.validFlagSets.front();
    QCOMPARE(
        validateProtocolEnvelope(descriptor.type, validFlags | CompressedMetadata, streamId, 1, payloadBytes).error,
        ProtocolEnvelopeError::InvalidFlags
    );
    QCOMPARE(
        validateProtocolEnvelope(descriptor.type, validFlags, wrongStreamId, 1, payloadBytes).error,
        ProtocolEnvelopeError::InvalidStreamId
    );
    QCOMPARE(
        validateProtocolEnvelope(descriptor.type, validFlags, streamId, 0, payloadBytes).error,
        ProtocolEnvelopeError::MissingMetadata
    );
    if (descriptor.payloadRule == ProtocolContentRule::Required) {
      QCOMPARE(
          validateProtocolEnvelope(descriptor.type, validFlags, streamId, 1, 0).error,
          ProtocolEnvelopeError::MissingPayload
      );
    } else {
      QCOMPARE(
          validateProtocolEnvelope(descriptor.type, validFlags, streamId, 1, 1).error,
          ProtocolEnvelopeError::UnexpectedPayload
      );
    }
  }
}

void ProtocolMessageRegistryTests::cddlAndFrozenVectorsCoverRegistry()
{
#ifndef RELAYDESK_PROTOCOL_CDDL_PATH
  QFAIL("RELAYDESK_PROTOCOL_CDDL_PATH is not configured");
#endif
#ifndef RELAYDESK_PROTOCOL_TEST_VECTORS_PATH
  QFAIL("RELAYDESK_PROTOCOL_TEST_VECTORS_PATH is not configured");
#endif
  QFile cddl(QStringLiteral(RELAYDESK_PROTOCOL_CDDL_PATH));
  QVERIFY2(cddl.open(QIODevice::ReadOnly), qPrintable(cddl.errorString()));
  const QString schemas = QString::fromUtf8(cddl.readAll());

  QFile vectorsFile(QStringLiteral(RELAYDESK_PROTOCOL_TEST_VECTORS_PATH));
  QVERIFY2(vectorsFile.open(QIODevice::ReadOnly), qPrintable(vectorsFile.errorString()));
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(vectorsFile.readAll(), &parseError);
  QVERIFY2(!document.isNull(), qPrintable(parseError.errorString()));

  QSet<quint16> positiveTypes;
  QSet<quint16> negativeTypes;
  for (const auto value : document.object().value(QStringLiteral("vectors")).toArray()) {
    const QJsonObject vector = value.toObject();
    const QString kind = vector.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("frame-positive")) {
      positiveTypes.insert(
          static_cast<quint16>(vector.value(QStringLiteral("expected"))
                                   .toObject()
                                   .value(QStringLiteral("messageType"))
                                   .toInt())
      );
    } else if (kind == QStringLiteral("metadata-negative")) {
      negativeTypes.insert(static_cast<quint16>(vector.value(QStringLiteral("messageType")).toInt()));
    }
  }

  for (const auto &descriptor : protocolMessageDescriptors()) {
    const QRegularExpression schemaDefinition(
        QStringLiteral("(?m)^%1\\s*=").arg(QRegularExpression::escape(QString::fromLatin1(descriptor.schema)))
    );
    QVERIFY2(schemaDefinition.match(schemas).hasMatch(), descriptor.schema);
    const quint16 type = static_cast<quint16>(descriptor.type);
    QVERIFY2(positiveTypes.contains(type), descriptor.name);
    QVERIFY2(negativeTypes.contains(type), descriptor.name);
  }
}

QTEST_GUILESS_MAIN(ProtocolMessageRegistryTests)
#include "ProtocolMessageRegistryTests.moc"
