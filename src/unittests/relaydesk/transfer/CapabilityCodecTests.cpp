// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/CapabilityCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

CapabilitiesMessage localCapabilities()
{
  return {
      .features =
          {QStringLiteral("file.v1"), QStringLiteral("folder.v1"), QStringLiteral("resume.v1"), QStringLiteral("sha256")
          },
      .preferredChunkBytes = 1U * 1024U * 1024U,
      .maxPayloadBytes = 4U * 1024U * 1024U,
      .maxConcurrentTransfers = 3,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 100'000,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Overwrite, ConflictPolicy::Ask},
  };
}

QByteArray mutate(const QByteArray &encoded, const std::function<void(QCborMap &)> &mutation)
{
  auto map = QCborValue::fromCbor(encoded).toMap();
  mutation(map);
  return QCborValue(map).toCbor();
}

} // namespace

class CapabilityCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTrip();
  void rejectsWrongTypeAndTrailingCbor();
  void rejectsMissingUnknownAndNonIntegerFields();
  void rejectsFeatureShapeAndDuplicates();
  void rejectsOutOfRangeAndInconsistentLimits();
  void rejectsUnknownAndDuplicatePolicies();
  void negotiatesIntersectionAndLowerLimits();
  void choosesHighestCommonVersion();
  void rejectsNoVersionRequiredFeatureOrPolicy();
  void rejectsInvalidLocalAndPeerInputs();
};

void CapabilityCodecTests::roundTrip()
{
  const auto expected = localCapabilities();
  const auto encoded = CapabilityCodec::encode(expected);
  QVERIFY(!encoded.isEmpty());
  const auto decoded = CapabilityCodec::decode(MessageType::Capabilities, encoded);
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(*decoded.message, expected);
  QCOMPARE(CapabilityCodec::encode(*decoded.message), encoded);
}

void CapabilityCodecTests::rejectsWrongTypeAndTrailingCbor()
{
  auto encoded = CapabilityCodec::encode(localCapabilities());
  QCOMPARE(CapabilityCodec::decode(MessageType::Hello, encoded).error, CapabilityCodecError::UnsupportedMessageType);
  encoded.append('\0');
  QCOMPARE(CapabilityCodec::decode(MessageType::Capabilities, encoded).error, CapabilityCodecError::MalformedCbor);
}

void CapabilityCodecTests::rejectsMissingUnknownAndNonIntegerFields()
{
  const auto valid = CapabilityCodec::encode(localCapabilities());
  QCOMPARE(
      CapabilityCodec::decode(MessageType::Capabilities, mutate(valid, [](QCborMap &map) { map.remove(7); })).error,
      CapabilityCodecError::InvalidFields
  );
  QCOMPARE(
      CapabilityCodec::decode(
          MessageType::Capabilities, mutate(valid, [](QCborMap &map) { map.insert(99, true); })
      ).error,
      CapabilityCodecError::InvalidFields
  );
  QCOMPARE(
      CapabilityCodec::decode(
          MessageType::Capabilities,
          mutate(valid, [](QCborMap &map) { map.insert(QStringLiteral("features"), QCborArray{}); })
      ).error,
      CapabilityCodecError::InvalidFields
  );
}

void CapabilityCodecTests::rejectsFeatureShapeAndDuplicates()
{
  const auto valid = CapabilityCodec::encode(localCapabilities());
  const QList<QCborArray> invalid = {
      {},
      {QStringLiteral("file.v1"), QStringLiteral("file.v1")},
      {QStringLiteral("UPPERCASE")},
      {QString(65, QLatin1Char('a'))},
      {1},
  };
  for (const auto &features : invalid) {
    const auto encoded = mutate(valid, [&](QCborMap &map) { map.insert(1, features); });
    QCOMPARE(CapabilityCodec::decode(MessageType::Capabilities, encoded).error, CapabilityCodecError::InvalidFeatures);
  }
}

void CapabilityCodecTests::rejectsOutOfRangeAndInconsistentLimits()
{
  const auto valid = CapabilityCodec::encode(localCapabilities());
  const QList<QPair<int, qint64>> invalid = {
      {2, 0},
      {3, static_cast<qint64>(kMaximumNegotiablePayloadBytes) + 1},
      {4, static_cast<qint64>(kMaximumNegotiableConcurrency) + 1},
      {5, 0},
      {6, static_cast<qint64>(kMaximumNegotiableManifestEntries) + 1},
  };
  for (const auto &[field, value] : invalid) {
    const auto encoded = mutate(valid, [&](QCborMap &map) { map.insert(field, value); });
    QCOMPARE(CapabilityCodec::decode(MessageType::Capabilities, encoded).error, CapabilityCodecError::InvalidLimits);
  }
  const auto chunkExceedsPayload = mutate(valid, [](QCborMap &map) {
    map.insert(2, 1024);
    map.insert(3, 512);
  });
  QCOMPARE(
      CapabilityCodec::decode(MessageType::Capabilities, chunkExceedsPayload).error, CapabilityCodecError::InvalidLimits
  );
}

void CapabilityCodecTests::rejectsUnknownAndDuplicatePolicies()
{
  const auto valid = CapabilityCodec::encode(localCapabilities());
  const QList<QCborArray> invalid = {
      {},
      {QStringLiteral("ask"), QStringLiteral("ask")},
      {QStringLiteral("replace-everything")},
      {1},
  };
  for (const auto &policies : invalid) {
    const auto encoded = mutate(valid, [&](QCborMap &map) { map.insert(7, policies); });
    QCOMPARE(
        CapabilityCodec::decode(MessageType::Capabilities, encoded).error, CapabilityCodecError::InvalidConflictPolicies
    );
  }
}

void CapabilityCodecTests::negotiatesIntersectionAndLowerLimits()
{
  const auto local = localCapabilities();
  auto peer = local;
  peer.features = {QStringLiteral("sha256"), QStringLiteral("file.v1"), QStringLiteral("peer.optional")};
  peer.preferredChunkBytes = 512U * 1024U;
  peer.maxPayloadBytes = 2U * 1024U * 1024U;
  peer.maxConcurrentTransfers = 1;
  peer.maxConcurrentFiles = 1;
  peer.maxManifestEntries = 8'000;
  peer.conflictPolicies = {ConflictPolicy::Ask, ConflictPolicy::AutoRename};

  const auto result = CapabilityNegotiator::negotiate({1}, local, {1}, peer);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.capabilities->protocolMajorVersion, 1);
  QCOMPARE(result.capabilities->features, QStringList({QStringLiteral("file.v1"), QStringLiteral("sha256")}));
  QCOMPARE(result.capabilities->chunkBytes, 512U * 1024U);
  QCOMPARE(result.capabilities->maxPayloadBytes, 2U * 1024U * 1024U);
  QCOMPARE(result.capabilities->maxConcurrentTransfers, 1);
  QCOMPARE(result.capabilities->maxConcurrentFiles, 1);
  QCOMPARE(result.capabilities->maxManifestEntries, 8'000U);
  QCOMPARE(
      result.capabilities->conflictPolicies, QList<ConflictPolicy>({ConflictPolicy::AutoRename, ConflictPolicy::Ask})
  );
}

void CapabilityCodecTests::choosesHighestCommonVersion()
{
  const auto capabilities = localCapabilities();
  const auto result = CapabilityNegotiator::negotiate({1, 3, 2}, capabilities, {2, 1}, capabilities);
  QVERIFY(result.ok());
  QCOMPARE(result.capabilities->protocolMajorVersion, 2);
}

void CapabilityCodecTests::rejectsNoVersionRequiredFeatureOrPolicy()
{
  const auto local = localCapabilities();
  auto peer = local;
  QCOMPARE(
      CapabilityNegotiator::negotiate({1}, local, {2}, peer).error, CapabilityNegotiationError::NoCommonProtocolVersion
  );
  peer.features = {QStringLiteral("file.v1")};
  QCOMPARE(
      CapabilityNegotiator::negotiate({1}, local, {1}, peer).error, CapabilityNegotiationError::MissingRequiredFeature
  );
  peer.features = {QStringLiteral("file.v1"), QStringLiteral("sha256")};
  peer.conflictPolicies = {ConflictPolicy::Skip};
  QCOMPARE(
      CapabilityNegotiator::negotiate({1}, local, {1}, peer).error, CapabilityNegotiationError::NoCommonConflictPolicy
  );
}

void CapabilityCodecTests::rejectsInvalidLocalAndPeerInputs()
{
  auto local = localCapabilities();
  auto peer = local;
  local.preferredChunkBytes = 0;
  QCOMPARE(
      CapabilityNegotiator::negotiate({1}, local, {1}, peer).error, CapabilityNegotiationError::InvalidLocalCapabilities
  );
  local = peer;
  peer.maxConcurrentFiles = 0;
  QCOMPARE(
      CapabilityNegotiator::negotiate({1}, local, {1}, peer).error, CapabilityNegotiationError::InvalidPeerCapabilities
  );
}

QTEST_MAIN(CapabilityCodecTests)

#include "CapabilityCodecTests.moc"
