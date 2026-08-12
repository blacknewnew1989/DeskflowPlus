// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/CapabilityCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

enum CapabilityKey : qint64
{
  FeaturesKey = 1,
  PreferredChunkBytesKey = 2,
  MaxPayloadBytesKey = 3,
  MaxConcurrentTransfersKey = 4,
  MaxConcurrentFilesKey = 5,
  MaxManifestEntriesKey = 6,
  ConflictPoliciesKey = 7,
};

constexpr qsizetype kMaximumListEntries = 64;
constexpr qsizetype kMaximumTokenUtf8Bytes = 64;
const auto kFeaturePattern = QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

void setError(QString *error, const QString &diagnostic)
{
  if (error != nullptr) {
    *error = diagnostic;
  }
}

CapabilitiesDecodeResult failure(CapabilityCodecError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

CapabilityNegotiationResult negotiationFailure(CapabilityNegotiationError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

QString policyName(ConflictPolicy policy)
{
  switch (policy) {
  case ConflictPolicy::AutoRename:
    return QStringLiteral("auto-rename");
  case ConflictPolicy::Overwrite:
    return QStringLiteral("overwrite");
  case ConflictPolicy::Skip:
    return QStringLiteral("skip");
  case ConflictPolicy::Ask:
    return QStringLiteral("ask");
  }
  return {};
}

std::optional<ConflictPolicy> parsePolicy(const QString &value)
{
  if (value == QStringLiteral("auto-rename")) {
    return ConflictPolicy::AutoRename;
  }
  if (value == QStringLiteral("overwrite")) {
    return ConflictPolicy::Overwrite;
  }
  if (value == QStringLiteral("skip")) {
    return ConflictPolicy::Skip;
  }
  if (value == QStringLiteral("ask")) {
    return ConflictPolicy::Ask;
  }
  return std::nullopt;
}

bool hasExactFields(const QCborMap &map)
{
  static const QSet<qint64> fields = {
      FeaturesKey,           PreferredChunkBytesKey, MaxPayloadBytesKey,  MaxConcurrentTransfersKey,
      MaxConcurrentFilesKey, MaxManifestEntriesKey,  ConflictPoliciesKey,
  };
  if (map.size() != fields.size()) {
    return false;
  }
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || !fields.contains(iterator.key().toInteger())) {
      return false;
    }
  }
  return true;
}

bool validateFeatures(const QStringList &features, QString *error)
{
  if (features.isEmpty() || features.size() > kMaximumListEntries) {
    setError(error, QStringLiteral("capability features must contain between 1 and 64 entries"));
    return false;
  }
  QSet<QString> unique;
  for (const auto &feature : features) {
    if (feature.toUtf8().size() > kMaximumTokenUtf8Bytes || !kFeaturePattern.match(feature).hasMatch() ||
        unique.contains(feature)) {
      setError(error, QStringLiteral("capability feature names must be bounded, canonical, and unique"));
      return false;
    }
    unique.insert(feature);
  }
  return true;
}

bool validatePolicies(const QList<ConflictPolicy> &policies, QString *error)
{
  if (policies.isEmpty() || policies.size() > 4) {
    setError(error, QStringLiteral("capability conflict policies must contain between 1 and 4 entries"));
    return false;
  }
  QSet<QString> unique;
  for (const auto policy : policies) {
    const auto name = policyName(policy);
    if (name.isEmpty() || unique.contains(name)) {
      setError(error, QStringLiteral("capability conflict policies must be known and unique"));
      return false;
    }
    unique.insert(name);
  }
  return true;
}

bool validateLimits(const CapabilitiesMessage &message, QString *error)
{
  if (message.preferredChunkBytes == 0 || message.maxPayloadBytes == 0 ||
      message.preferredChunkBytes > message.maxPayloadBytes ||
      message.maxPayloadBytes > kMaximumNegotiablePayloadBytes || message.maxConcurrentTransfers == 0 ||
      message.maxConcurrentTransfers > kMaximumNegotiableConcurrency || message.maxConcurrentFiles == 0 ||
      message.maxConcurrentFiles > kMaximumNegotiableConcurrency || message.maxManifestEntries == 0 ||
      message.maxManifestEntries > kMaximumNegotiableManifestEntries) {
    setError(error, QStringLiteral("capability limits are zero, inconsistent, or exceed RDFT/1 bounds"));
    return false;
  }
  return true;
}

bool validate(const CapabilitiesMessage &message, QString *error)
{
  return validateFeatures(message.features, error) && validateLimits(message, error) &&
         validatePolicies(message.conflictPolicies, error);
}

std::optional<quint32> readBoundedUint32(const QCborMap &map, qint64 field, quint32 maximum)
{
  const auto value = valueFor(map, field);
  if (!value.isInteger() || value.toInteger() <= 0 || value.toInteger() > maximum) {
    return std::nullopt;
  }
  return static_cast<quint32>(value.toInteger());
}

std::optional<QStringList> readFeatures(const QCborValue &value)
{
  if (!value.isArray()) {
    return std::nullopt;
  }
  QStringList result;
  for (const auto &entry : value.toArray()) {
    if (!entry.isString()) {
      return std::nullopt;
    }
    result.append(entry.toString());
  }
  QString diagnostic;
  return validateFeatures(result, &diagnostic) ? std::optional<QStringList>(result) : std::nullopt;
}

std::optional<QList<ConflictPolicy>> readPolicies(const QCborValue &value)
{
  if (!value.isArray()) {
    return std::nullopt;
  }
  QList<ConflictPolicy> result;
  for (const auto &entry : value.toArray()) {
    if (!entry.isString()) {
      return std::nullopt;
    }
    const auto policy = parsePolicy(entry.toString());
    if (!policy.has_value()) {
      return std::nullopt;
    }
    result.append(*policy);
  }
  QString diagnostic;
  return validatePolicies(result, &diagnostic) ? std::optional<QList<ConflictPolicy>>(result) : std::nullopt;
}

template <typename T> QList<T> intersectionPreservingLocalOrder(const QList<T> &local, const QList<T> &peer)
{
  QList<T> result;
  for (const auto &value : local) {
    if (peer.contains(value) && !result.contains(value)) {
      result.append(value);
    }
  }
  return result;
}

} // namespace

QByteArray CapabilityCodec::encode(const CapabilitiesMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (!validate(message, error)) {
    return {};
  }

  QCborArray features;
  for (const auto &feature : message.features) {
    features.append(feature);
  }
  QCborArray policies;
  for (const auto policy : message.conflictPolicies) {
    policies.append(policyName(policy));
  }
  const QCborMap map = {
      {key(FeaturesKey), features},
      {key(PreferredChunkBytesKey), message.preferredChunkBytes},
      {key(MaxPayloadBytesKey), message.maxPayloadBytes},
      {key(MaxConcurrentTransfersKey), message.maxConcurrentTransfers},
      {key(MaxConcurrentFilesKey), message.maxConcurrentFiles},
      {key(MaxManifestEntriesKey), message.maxManifestEntries},
      {key(ConflictPoliciesKey), policies},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

CapabilitiesDecodeResult CapabilityCodec::decode(MessageType type, QByteArrayView metadata)
{
  if (type != MessageType::Capabilities) {
    return failure(
        CapabilityCodecError::UnsupportedMessageType, QStringLiteral("metadata is not a CAPABILITIES message")
    );
  }
  QCborParserError parserError;
  const auto root = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (metadata.isEmpty() || parserError.error != QCborError::NoError || parserError.offset != metadata.size() ||
      !root.isMap()) {
    return failure(CapabilityCodecError::MalformedCbor, QStringLiteral("CAPABILITIES is not one CBOR map"));
  }
  const auto map = root.toMap();
  if (!hasExactFields(map)) {
    return failure(
        CapabilityCodecError::InvalidFields, QStringLiteral("CAPABILITIES contains missing or unknown fields")
    );
  }

  const auto features = readFeatures(valueFor(map, FeaturesKey));
  if (!features.has_value()) {
    return failure(CapabilityCodecError::InvalidFeatures, QStringLiteral("CAPABILITIES feature list is invalid"));
  }
  const auto preferredChunk = readBoundedUint32(map, PreferredChunkBytesKey, kMaximumNegotiablePayloadBytes);
  const auto maxPayload = readBoundedUint32(map, MaxPayloadBytesKey, kMaximumNegotiablePayloadBytes);
  const auto maxTransfers = readBoundedUint32(map, MaxConcurrentTransfersKey, kMaximumNegotiableConcurrency);
  const auto maxFiles = readBoundedUint32(map, MaxConcurrentFilesKey, kMaximumNegotiableConcurrency);
  const auto maxEntries = readBoundedUint32(map, MaxManifestEntriesKey, kMaximumNegotiableManifestEntries);
  if (!preferredChunk.has_value() || !maxPayload.has_value() || !maxTransfers.has_value() || !maxFiles.has_value() ||
      !maxEntries.has_value() || *preferredChunk > *maxPayload) {
    return failure(CapabilityCodecError::InvalidLimits, QStringLiteral("CAPABILITIES limits are invalid"));
  }
  const auto policies = readPolicies(valueFor(map, ConflictPoliciesKey));
  if (!policies.has_value()) {
    return failure(
        CapabilityCodecError::InvalidConflictPolicies, QStringLiteral("CAPABILITIES conflict policies are invalid")
    );
  }

  return {
      .message =
          CapabilitiesMessage{
              .features = *features,
              .preferredChunkBytes = *preferredChunk,
              .maxPayloadBytes = *maxPayload,
              .maxConcurrentTransfers = static_cast<quint16>(*maxTransfers),
              .maxConcurrentFiles = static_cast<quint16>(*maxFiles),
              .maxManifestEntries = *maxEntries,
              .conflictPolicies = *policies,
          }
  };
}

CapabilityNegotiationResult CapabilityNegotiator::negotiate(
    const QList<quint16> &localVersions, const CapabilitiesMessage &local, const QList<quint16> &peerVersions,
    const CapabilitiesMessage &peer
)
{
  QList<quint16> commonVersions = intersectionPreservingLocalOrder(localVersions, peerVersions);
  if (commonVersions.isEmpty()) {
    return negotiationFailure(
        CapabilityNegotiationError::NoCommonProtocolVersion, QStringLiteral("peers have no common RDFT major version")
    );
  }
  const auto selectedVersion = *std::ranges::max_element(commonVersions);

  QString diagnostic;
  if (!validate(local, &diagnostic)) {
    return negotiationFailure(CapabilityNegotiationError::InvalidLocalCapabilities, std::move(diagnostic));
  }
  if (!validate(peer, &diagnostic)) {
    return negotiationFailure(CapabilityNegotiationError::InvalidPeerCapabilities, std::move(diagnostic));
  }

  const QStringList features = intersectionPreservingLocalOrder(local.features, peer.features);
  if (!features.contains(QStringLiteral("file.v1")) || !features.contains(QStringLiteral("sha256"))) {
    return negotiationFailure(
        CapabilityNegotiationError::MissingRequiredFeature,
        QStringLiteral("peer does not share the required file.v1 and sha256 capabilities")
    );
  }
  const auto policies = intersectionPreservingLocalOrder(local.conflictPolicies, peer.conflictPolicies);
  if (policies.isEmpty()) {
    return negotiationFailure(
        CapabilityNegotiationError::NoCommonConflictPolicy, QStringLiteral("peers have no common conflict policy")
    );
  }

  const quint32 maxPayload = std::min(local.maxPayloadBytes, peer.maxPayloadBytes);
  return {
      .capabilities =
          NegotiatedCapabilities{
              .protocolMajorVersion = selectedVersion,
              .features = features,
              .chunkBytes = std::min({local.preferredChunkBytes, peer.preferredChunkBytes, maxPayload}),
              .maxPayloadBytes = maxPayload,
              .maxConcurrentTransfers = std::min(local.maxConcurrentTransfers, peer.maxConcurrentTransfers),
              .maxConcurrentFiles = std::min(local.maxConcurrentFiles, peer.maxConcurrentFiles),
              .maxManifestEntries = std::min(local.maxManifestEntries, peer.maxManifestEntries),
              .conflictPolicies = policies,
          }
  };
}

} // namespace relaydesk::transfer
