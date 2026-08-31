// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferRecoveryStore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <functional>
#include <limits>

namespace relaydesk::transfer {
namespace {

constexpr qint64 SchemaKey = 1;
constexpr qint64 TransferKey = 2;
constexpr qint64 LocalKey = 3;
constexpr qint64 PeerKey = 4;
constexpr qint64 FingerprintKey = 5;
constexpr qint64 CreatedKey = 6;
constexpr qint64 OptionsKey = 7;
constexpr qint64 SourcesKey = 8;
constexpr qint64 EntriesKey = 9;
constexpr qint64 SummaryKey = 10;
constexpr qint64 PlanKey = 11;
constexpr qint64 EffectivePolicyKey = 12;
constexpr qint64 ProgressKey = 13;
constexpr qint64 PeerNameKey = 14;
constexpr qint64 OfferKey = 15;
constexpr qint64 ReceiveOptionsKey = 16;
constexpr qint64 ResumeKey = 17;

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

TransferRecoveryStoreOperationResult fail(TransferRecoveryStoreError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

template <typename State>
TransferRecoveryStoreLoadResult<State> loadFail(TransferRecoveryStoreError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool exactKeys(const QCborMap &map, std::initializer_list<qint64> expected)
{
  if (map.size() != static_cast<qsizetype>(expected.size()))
    return false;
  for (const auto item : expected) {
    if (!map.contains(key(item)))
      return false;
  }
  return true;
}

QCborValue valueFor(const QCborMap &map, qint64 item)
{
  return map.value(key(item));
}

bool validPolicy(ConflictPolicy value)
{
  return value == ConflictPolicy::AutoRename || value == ConflictPolicy::Overwrite || value == ConflictPolicy::Skip ||
         value == ConflictPolicy::Ask;
}

std::optional<ConflictPolicy> policy(qint64 value)
{
  if (value < static_cast<qint64>(ConflictPolicy::AutoRename) || value > static_cast<qint64>(ConflictPolicy::Ask)) {
    return std::nullopt;
  }
  return static_cast<ConflictPolicy>(value);
}

bool validEntryType(ManifestEntryType value)
{
  return value == ManifestEntryType::File || value == ManifestEntryType::Directory;
}

bool validRecoveryDigest(const ManifestEntry &entry)
{
  if (entry.type == ManifestEntryType::Directory)
    return entry.sha256.isEmpty();
  return entry.type == ManifestEntryType::File && entry.sha256.size() == kSha256Bytes;
}

std::optional<ManifestEntryType> entryType(qint64 value)
{
  if (value < static_cast<qint64>(ManifestEntryType::File) ||
      value > static_cast<qint64>(ManifestEntryType::Directory)) {
    return std::nullopt;
  }
  return static_cast<ManifestEntryType>(value);
}

bool validAbsolutePath(const QString &path)
{
  return !path.isEmpty() && QDir::isAbsolutePath(path) && !path.contains(QChar::Null);
}

bool validRelativePath(const QString &path, const PathLimits &limits)
{
  const auto checked = PathPolicy::validateRelative(path, limits);
  return checked.ok && checked.normalized == path;
}

bool validCount(quint64 value)
{
  return value <= static_cast<quint64>(std::numeric_limits<qint64>::max());
}

QCborMap encodeEntry(const ManifestEntry &entry)
{
  return {
      {key(1), entry.id.toBytes()},
      {key(2), entry.relativeProtocolPath},
      {key(3), static_cast<qint64>(entry.type)},
      {key(4), static_cast<qint64>(entry.size)},
      {key(5), entry.modifiedUtc.toMSecsSinceEpoch()},
      {key(6), entry.sha256},
      {key(7), static_cast<qint64>(entry.flags)}
  };
}

std::optional<ManifestEntry> decodeEntry(const QCborValue &encoded, const PathLimits &limits)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3, 4, 5, 6, 7}))
    return std::nullopt;
  const auto id = valueFor(map, 1);
  const auto path = valueFor(map, 2);
  const auto type = valueFor(map, 3);
  const auto size = valueFor(map, 4);
  const auto modified = valueFor(map, 5);
  const auto hash = valueFor(map, 6);
  const auto flags = valueFor(map, 7);
  if (!id.isByteArray() || !path.isString() || !type.isInteger() || !size.isInteger() || !modified.isInteger() ||
      !hash.isByteArray() || !flags.isInteger() || size.toInteger() < 0 || modified.toInteger() <= 0 ||
      flags.toInteger() < 0 || static_cast<quint64>(flags.toInteger()) > std::numeric_limits<quint32>::max())
    return std::nullopt;
  const auto parsedId = FileId::fromBytes(id.toByteArray());
  const auto parsedType = entryType(type.toInteger());
  if (!parsedId || !parsedType || !validRelativePath(path.toString(), limits) ||
      !validCount(static_cast<quint64>(size.toInteger())))
    return std::nullopt;
  ManifestEntry entry{
      .id = *parsedId,
      .relativeProtocolPath = path.toString(),
      .type = *parsedType,
      .size = static_cast<quint64>(size.toInteger()),
      .modifiedUtc = QDateTime::fromMSecsSinceEpoch(modified.toInteger(), Qt::UTC),
      .sha256 = hash.toByteArray(),
      .flags = static_cast<quint32>(flags.toInteger())
  };
  if (!validRecoveryDigest(entry))
    return std::nullopt;
  return entry;
}

QCborMap encodePlan(const ManifestPagePlanBinding &value)
{
  return {
      {key(1), static_cast<qint64>(value.entryCount)},
      {key(2), static_cast<qint64>(value.pageCount)},
      {key(3), static_cast<qint64>(value.totalMetadataBytes)}
  };
}

std::optional<ManifestPagePlanBinding> decodePlan(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3}))
    return std::nullopt;
  const auto entries = valueFor(map, 1);
  const auto pages = valueFor(map, 2);
  const auto bytes = valueFor(map, 3);
  if (!entries.isInteger() || !pages.isInteger() || !bytes.isInteger() || entries.toInteger() < 0 ||
      pages.toInteger() < 0 || bytes.toInteger() < 0)
    return std::nullopt;
  return ManifestPagePlanBinding{
      static_cast<quint64>(entries.toInteger()), static_cast<quint64>(pages.toInteger()),
      static_cast<quint64>(bytes.toInteger())
  };
}

QCborMap encodeOffer(const TransferOffer &offer)
{
  return {
      {key(1), offer.transferId.toBytes()},
      {key(2), offer.displayName},
      {key(3), static_cast<qint64>(offer.totalBytes)},
      {key(4), static_cast<qint64>(offer.fileCount)},
      {key(5), static_cast<qint64>(offer.directoryCount)},
      {key(6), offer.manifestSha256},
      {key(7), static_cast<qint64>(offer.manifestPageCount)},
      {key(8), static_cast<qint64>(offer.requestedConflictPolicy)},
      {key(9), static_cast<qint64>(offer.createdAtMs)}
  };
}

std::optional<TransferOffer> decodeOffer(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3, 4, 5, 6, 7, 8, 9}))
    return std::nullopt;
  const auto id = valueFor(map, 1);
  const auto name = valueFor(map, 2);
  const auto total = valueFor(map, 3);
  const auto files = valueFor(map, 4);
  const auto dirs = valueFor(map, 5);
  const auto hash = valueFor(map, 6);
  const auto pages = valueFor(map, 7);
  const auto policyValue = valueFor(map, 8);
  const auto created = valueFor(map, 9);
  if (!id.isByteArray() || !name.isString() || !total.isInteger() || !files.isInteger() || !dirs.isInteger() ||
      !hash.isByteArray() || !pages.isInteger() || !policyValue.isInteger() || !created.isInteger() ||
      total.toInteger() < 0 || files.toInteger() < 0 || dirs.toInteger() < 0 || pages.toInteger() <= 0 ||
      created.toInteger() <= 0 || hash.toByteArray().size() != kSha256Bytes)
    return std::nullopt;
  const auto transfer = TransferId::fromBytes(id.toByteArray());
  const auto parsedPolicy = policy(policyValue.toInteger());
  if (!transfer || !parsedPolicy)
    return std::nullopt;
  return TransferOffer{
      .transferId = *transfer,
      .displayName = name.toString(),
      .totalBytes = static_cast<quint64>(total.toInteger()),
      .fileCount = static_cast<quint64>(files.toInteger()),
      .directoryCount = static_cast<quint64>(dirs.toInteger()),
      .manifestSha256 = hash.toByteArray(),
      .manifestPageCount = static_cast<quint64>(pages.toInteger()),
      .requestedConflictPolicy = *parsedPolicy,
      .createdAtMs = static_cast<quint64>(created.toInteger())
  };
}

template <typename State>
TransferRecoveryStoreLoadResult<State> readState(
    const QString &path, const TransferId &expected, quint64 limit,
    const std::function<std::optional<State>(const QCborMap &)> &decode
)
{
  QFile file(path);
  if (!file.exists())
    return loadFail<State>(TransferRecoveryStoreError::NotFound, QStringLiteral("recovery state does not exist"));
  if (!file.open(QIODevice::ReadOnly))
    return loadFail<State>(TransferRecoveryStoreError::OpenFailed, file.errorString());
  if (file.size() <= 0 || static_cast<quint64>(file.size()) > limit)
    return loadFail<State>(
        TransferRecoveryStoreError::StateTooLarge, QStringLiteral("recovery state exceeds the limit")
    );
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    return loadFail<State>(TransferRecoveryStoreError::ReadFailed, file.errorString());
  QCborParserError error;
  const auto value = QCborValue::fromCbor(bytes, &error);
  if (error.error != QCborError::NoError || error.offset != bytes.size() || !value.isMap())
    return loadFail<State>(
        TransferRecoveryStoreError::MalformedCbor, QStringLiteral("recovery state is not exactly one CBOR map")
    );
  const auto map = value.toMap();
  const auto schema = valueFor(map, SchemaKey);
  if (!schema.isInteger())
    return loadFail<State>(
        TransferRecoveryStoreError::InvalidFields, QStringLiteral("recovery state schema is invalid")
    );
  if (schema.toInteger() != static_cast<qint64>(kTransferRecoverySchemaVersion))
    return loadFail<State>(
        TransferRecoveryStoreError::UnsupportedSchema, QStringLiteral("recovery state schema is unsupported")
    );
  const auto parsed = decode(map);
  if (!parsed)
    return loadFail<State>(
        TransferRecoveryStoreError::InvalidFields, QStringLiteral("recovery state fields are invalid")
    );
  if (parsed->transferId != expected)
    return loadFail<State>(
        TransferRecoveryStoreError::TransferIdMismatch,
        QStringLiteral("recovery state does not match its requested transfer ID")
    );
  return {.state = std::move(*parsed)};
}

} // namespace

TransferRecoveryStore::TransferRecoveryStore(QString rootDirectory, TransferRecoveryStoreLimits limits)
    : m_rootDirectory(QDir::cleanPath(std::move(rootDirectory))),
      m_limits(std::move(limits))
{
}

namespace {
QCborMap encodeSummary(const TransferManifestSummary &value)
{
  return {
      {key(1), value.id.toBytes()},
      {key(2), value.displayName},
      {key(3), static_cast<qint64>(value.totalBytes)},
      {key(4), static_cast<qint64>(value.fileCount)},
      {key(5), static_cast<qint64>(value.directoryCount)},
      {key(6), value.canonicalSha256}
  };
}

std::optional<TransferManifestSummary> decodeSummary(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3, 4, 5, 6}))
    return std::nullopt;
  const auto id = valueFor(map, 1);
  const auto name = valueFor(map, 2);
  const auto total = valueFor(map, 3);
  const auto files = valueFor(map, 4);
  const auto directories = valueFor(map, 5);
  const auto hash = valueFor(map, 6);
  if (!id.isByteArray() || !name.isString() || !total.isInteger() || !files.isInteger() || !directories.isInteger() ||
      !hash.isByteArray() || total.toInteger() < 0 || files.toInteger() < 0 || directories.toInteger() < 0 ||
      hash.toByteArray().size() != kSha256Bytes)
    return std::nullopt;
  const auto transfer = TransferId::fromBytes(id.toByteArray());
  if (!transfer)
    return std::nullopt;
  return TransferManifestSummary{
      .id = *transfer,
      .displayName = name.toString(),
      .totalBytes = static_cast<quint64>(total.toInteger()),
      .fileCount = static_cast<quint64>(files.toInteger()),
      .directoryCount = static_cast<quint64>(directories.toInteger()),
      .canonicalSha256 = hash.toByteArray()
  };
}

QCborArray encodeEntries(const QList<PreparedManifestEntry> &entries)
{
  QCborArray result;
  for (const auto &value : entries)
    result.append(QCborMap{
        {key(1), value.canonicalSourcePath}, {key(2), value.protocolCollisionKey}, {key(3), encodeEntry(value.entry)}
    });
  return result;
}

std::optional<QList<PreparedManifestEntry>> decodePreparedEntries(const QCborValue &encoded, const PathLimits &limits)
{
  if (!encoded.isArray())
    return std::nullopt;
  QList<PreparedManifestEntry> result;
  for (const auto &item : encoded.toArray()) {
    if (!item.isMap())
      return std::nullopt;
    const auto map = item.toMap();
    if (!exactKeys(map, {1, 2, 3}))
      return std::nullopt;
    const auto path = valueFor(map, 1);
    const auto collision = valueFor(map, 2);
    const auto entry = decodeEntry(valueFor(map, 3), limits);
    if (!path.isString() || !collision.isString() || !validAbsolutePath(path.toString()) || !entry)
      return std::nullopt;
    result.append(
        {.canonicalSourcePath = path.toString(), .protocolCollisionKey = collision.toString(), .entry = *entry}
    );
  }
  return result;
}

QCborArray encodeSources(const QList<RecoverySource> &sources)
{
  QCborArray result;
  for (const auto &value : sources)
    result.append(QCborMap{
        {key(1), value.canonicalPath}, {key(2), value.relativeProtocolPath}, {key(3), static_cast<qint64>(value.type)}
    });
  return result;
}

std::optional<QList<RecoverySource>> decodeSources(const QCborValue &encoded, const PathLimits &limits)
{
  if (!encoded.isArray())
    return std::nullopt;
  QList<RecoverySource> result;
  for (const auto &item : encoded.toArray()) {
    if (!item.isMap())
      return std::nullopt;
    const auto map = item.toMap();
    if (!exactKeys(map, {1, 2, 3}))
      return std::nullopt;
    const auto path = valueFor(map, 1);
    const auto relative = valueFor(map, 2);
    const auto type = valueFor(map, 3);
    if (!path.isString() || !relative.isString() || !type.isInteger() || !validAbsolutePath(path.toString()) ||
        !validRelativePath(relative.toString(), limits))
      return std::nullopt;
    const auto parsed = entryType(type.toInteger());
    if (!parsed)
      return std::nullopt;
    result.append({path.toString(), relative.toString(), *parsed});
  }
  return result;
}

QCborMap encodeProgress(const RecoveryProgress &value)
{
  return {
      {key(1), static_cast<qint64>(value.completedBytes)},
      {key(2), static_cast<qint64>(value.completedFiles)},
      {key(3), static_cast<qint64>(value.currentEntry)}
  };
}

std::optional<RecoveryProgress> decodeProgress(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3}))
    return std::nullopt;
  const auto bytes = valueFor(map, 1);
  const auto files = valueFor(map, 2);
  const auto current = valueFor(map, 3);
  if (!bytes.isInteger() || !files.isInteger() || !current.isInteger() || bytes.toInteger() < 0 ||
      files.toInteger() < 0 || current.toInteger() < 0)
    return std::nullopt;
  return RecoveryProgress{
      static_cast<quint64>(bytes.toInteger()), static_cast<quint64>(files.toInteger()),
      static_cast<quint64>(current.toInteger())
  };
}

QCborMap encodeReceiveOptions(const ReceiveOptions &value)
{
  return {
      {key(1), value.destinationRoot},
      {key(2), static_cast<qint64>(value.conflictPolicy)},
      {key(3), static_cast<qint64>(value.failurePartialDisposition)},
      {key(4), static_cast<qint64>(value.acceptanceOrigin)}
  };
}

std::optional<ReceiveOptions> decodeReceiveOptions(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3, 4}))
    return std::nullopt;
  const auto root = valueFor(map, 1);
  const auto conflict = valueFor(map, 2);
  const auto partial = valueFor(map, 3);
  const auto origin = valueFor(map, 4);
  if (!root.isString() || !conflict.isInteger() || !partial.isInteger() || !origin.isInteger() ||
      !validAbsolutePath(root.toString()))
    return std::nullopt;
  const auto parsed = policy(conflict.toInteger());
  if (!parsed || partial.toInteger() < 0 || partial.toInteger() > 1 || origin.toInteger() < 0 || origin.toInteger() > 1)
    return std::nullopt;
  return ReceiveOptions{
      .destinationRoot = root.toString(),
      .conflictPolicy = *parsed,
      .failurePartialDisposition = static_cast<PartialDisposition>(partial.toInteger()),
      .acceptanceOrigin = static_cast<AcceptanceOrigin>(origin.toInteger())
  };
}

QCborMap encodeCapabilities(const NegotiatedCapabilities &value)
{
  QCborArray features;
  for (const auto &feature : value.features)
    features.append(feature);
  QCborArray policies;
  for (const auto policyValue : value.conflictPolicies)
    policies.append(static_cast<qint64>(policyValue));
  return {
      {key(1), static_cast<qint64>(value.protocolMajorVersion)},
      {key(2), features},
      {key(3), static_cast<qint64>(value.chunkBytes)},
      {key(4), static_cast<qint64>(value.maxPayloadBytes)},
      {key(5), static_cast<qint64>(value.maxConcurrentTransfers)},
      {key(6), static_cast<qint64>(value.maxConcurrentFiles)},
      {key(7), static_cast<qint64>(value.maxManifestEntries)},
      {key(8), policies},
      {key(9), value.localCanReceiveFiles},
      {key(10), value.peerCanReceiveFiles}
  };
}

std::optional<NegotiatedCapabilities> decodeCapabilities(const QCborValue &encoded)
{
  if (!encoded.isMap())
    return std::nullopt;
  const auto map = encoded.toMap();
  if (!exactKeys(map, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}))
    return std::nullopt;
  const auto version = valueFor(map, 1), features = valueFor(map, 2), chunk = valueFor(map, 3),
             payload = valueFor(map, 4), transfers = valueFor(map, 5), files = valueFor(map, 6),
             entries = valueFor(map, 7), policies = valueFor(map, 8), local = valueFor(map, 9),
             peer = valueFor(map, 10);
  if (!version.isInteger() || !features.isArray() || !chunk.isInteger() || !payload.isInteger() ||
      !transfers.isInteger() || !files.isInteger() || !entries.isInteger() || !policies.isArray() || !local.isBool() ||
      !peer.isBool() || version.toInteger() != kProtocolMajorVersion || chunk.toInteger() <= 0 ||
      chunk.toInteger() > kMaximumNegotiablePayloadBytes || payload.toInteger() <= 0 ||
      payload.toInteger() > kMaximumNegotiablePayloadBytes || transfers.toInteger() <= 0 ||
      transfers.toInteger() > kMaximumNegotiableConcurrency || files.toInteger() <= 0 ||
      files.toInteger() > kMaximumNegotiableConcurrency || entries.toInteger() <= 0 ||
      entries.toInteger() > kMaximumNegotiableManifestEntries)
    return std::nullopt;
  QStringList decodedFeatures;
  for (const auto &feature : features.toArray()) {
    if (!feature.isString() || feature.toString().isEmpty() || decodedFeatures.contains(feature.toString()))
      return std::nullopt;
    decodedFeatures.append(feature.toString());
  }
  QList<ConflictPolicy> decodedPolicies;
  for (const auto &policyValue : policies.toArray()) {
    if (!policyValue.isInteger())
      return std::nullopt;
    const auto parsed = policy(policyValue.toInteger());
    if (!parsed || decodedPolicies.contains(*parsed))
      return std::nullopt;
    decodedPolicies.append(*parsed);
  }
  if (decodedPolicies.isEmpty() || !decodedFeatures.contains(QStringLiteral("resume.v1")) || !local.toBool())
    return std::nullopt;
  return NegotiatedCapabilities{
      .protocolMajorVersion = static_cast<quint16>(version.toInteger()),
      .features = decodedFeatures,
      .chunkBytes = static_cast<quint32>(chunk.toInteger()),
      .maxPayloadBytes = static_cast<quint32>(payload.toInteger()),
      .maxConcurrentTransfers = static_cast<quint16>(transfers.toInteger()),
      .maxConcurrentFiles = static_cast<quint16>(files.toInteger()),
      .maxManifestEntries = static_cast<quint32>(entries.toInteger()),
      .conflictPolicies = decodedPolicies,
      .localCanReceiveFiles = local.toBool(),
      .peerCanReceiveFiles = peer.toBool()
  };
}

bool validateNegotiatedCapabilities(const NegotiatedCapabilities &value)
{
  if (value.protocolMajorVersion != kProtocolMajorVersion || value.chunkBytes == 0 ||
      value.chunkBytes > kMaximumNegotiablePayloadBytes || value.maxPayloadBytes == 0 ||
      value.maxPayloadBytes > kMaximumNegotiablePayloadBytes || value.maxConcurrentTransfers == 0 ||
      value.maxConcurrentTransfers > kMaximumNegotiableConcurrency || value.maxConcurrentFiles == 0 ||
      value.maxConcurrentFiles > kMaximumNegotiableConcurrency || value.maxManifestEntries == 0 ||
      value.maxManifestEntries > kMaximumNegotiableManifestEntries || !value.localCanReceiveFiles ||
      !value.features.contains(QStringLiteral("resume.v1")))
    return false;
  QSet<QString> features;
  for (const auto &feature : value.features) {
    if (feature.isEmpty() || features.contains(feature))
      return false;
    features.insert(feature);
  }
  QSet<ConflictPolicy> policies;
  for (const auto policyValue : value.conflictPolicies) {
    if (!validPolicy(policyValue) || policies.contains(policyValue))
      return false;
    policies.insert(policyValue);
  }
  return !policies.isEmpty();
}

bool validCommon(
    const TransferId &transfer, const deskflow::relaydesk::DeviceId &local, const deskflow::relaydesk::DeviceId &peer,
    QByteArrayView fingerprint
)
{
  return !transfer.toBytes().isEmpty() && !local.value().isNull() && !peer.value().isNull() &&
         fingerprint.size() == kSha256Bytes;
}

TransferRecoveryStoreOperationResult writeState(const QString &path, const QByteArray &encoded, quint64 limit)
{
  if (encoded.isEmpty() || static_cast<quint64>(encoded.size()) > limit)
    return fail(TransferRecoveryStoreError::StateTooLarge, QStringLiteral("encoded recovery state exceeds the limit"));
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail(
        TransferRecoveryStoreError::DirectoryCreateFailed, QStringLiteral("could not create recovery directory")
    );
  QSaveFile output(path);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly))
    return fail(TransferRecoveryStoreError::OpenFailed, output.errorString());
  if (output.write(encoded) != encoded.size()) {
    output.cancelWriting();
    return fail(TransferRecoveryStoreError::WriteFailed, output.errorString());
  }
  if (!output.commit())
    return fail(TransferRecoveryStoreError::CommitFailed, output.errorString());
  return {};
}

bool validRoot(const QString &root)
{
  return !root.isEmpty() && QDir::isAbsolutePath(root);
}

bool validateSources(const QList<RecoverySource> &sources, quint64 maximumEntries, const PathLimits &limits)
{
  if (sources.isEmpty() || static_cast<quint64>(sources.size()) > maximumEntries)
    return false;

  QSet<QString> canonicalPaths;
  QSet<QString> protocolPaths;
  for (const auto &source : sources) {
    const auto canonicalPath = QDir::cleanPath(source.canonicalPath);
    const auto protocolPath = PathPolicy::validateRelative(source.relativeProtocolPath, limits);
    if (!validAbsolutePath(source.canonicalPath) || canonicalPath != source.canonicalPath || !protocolPath.ok ||
        protocolPath.normalized != source.relativeProtocolPath || !validEntryType(source.type) ||
        canonicalPaths.contains(canonicalPath) || protocolPaths.contains(protocolPath.collisionKey))
      return false;
    canonicalPaths.insert(canonicalPath);
    protocolPaths.insert(protocolPath.collisionKey);
  }
  return true;
}

bool validateManifest(
    const QList<PreparedManifestEntry> &prepared, const TransferManifestSummary &summary,
    const ManifestPagePlanBinding &binding, const PathLimits &limits
)
{
  if (prepared.isEmpty() || summary.canonicalSha256.size() != kSha256Bytes ||
      binding.entryCount != static_cast<quint64>(prepared.size()) || !validCount(summary.totalBytes) ||
      !validCount(summary.fileCount) || !validCount(summary.directoryCount) || !validCount(binding.entryCount) ||
      !validCount(binding.pageCount) || !validCount(binding.totalMetadataBytes))
    return false;
  QSet<QByteArray> ids;
  QSet<QString> paths;
  quint64 bytes = 0;
  quint64 files = 0;
  quint64 directories = 0;
  for (const auto &item : prepared) {
    const auto checked = PathPolicy::validateRelative(item.entry.relativeProtocolPath, limits);
    if (!validAbsolutePath(item.canonicalSourcePath) || !checked.ok ||
        checked.normalized != item.entry.relativeProtocolPath || item.protocolCollisionKey != checked.collisionKey ||
        !validRecoveryDigest(item.entry) || !validCount(item.entry.size) ||
        ids.contains(item.entry.id.toBytes()) || paths.contains(checked.collisionKey))
      return false;
    ids.insert(item.entry.id.toBytes());
    paths.insert(checked.collisionKey);
    if (item.entry.type == ManifestEntryType::File) {
      ++files;
      if (item.entry.size > std::numeric_limits<quint64>::max() - bytes)
        return false;
      bytes += item.entry.size;
    } else if (item.entry.type == ManifestEntryType::Directory)
      ++directories;
    else
      return false;
  }
  if (summary.totalBytes != bytes || summary.fileCount != files || summary.directoryCount != directories)
    return false;
  const auto digest = ManifestPageCodec::canonicalSha256(prepared);
  if (digest != summary.canonicalSha256)
    return false;
  TransferManifest manifest{.entries = prepared, .summary = summary};
  const auto plan = ManifestPageCodec::plan(manifest);
  return plan.ok() && plan.plan->entryCount == binding.entryCount && plan.plan->pageCount() == binding.pageCount &&
         plan.plan->totalMetadataBytes == binding.totalMetadataBytes;
}

bool validateIncomingManifest(
    const QList<ManifestEntry> &entries, const TransferOffer &offer, const ManifestPagePlanBinding &binding,
    const PathLimits &limits
)
{
  QList<PreparedManifestEntry> prepared;
  prepared.reserve(entries.size());
  for (const auto &entry : entries)
    prepared.append(
        {.canonicalSourcePath = QDir::rootPath(),
         .protocolCollisionKey = PathPolicy::validateRelative(entry.relativeProtocolPath, limits).collisionKey,
         .entry = entry}
    );
  const TransferManifestSummary summary{
      .id = offer.transferId,
      .displayName = offer.displayName,
      .totalBytes = offer.totalBytes,
      .fileCount = offer.fileCount,
      .directoryCount = offer.directoryCount,
      .canonicalSha256 = offer.manifestSha256
  };
  return validateManifest(prepared, summary, binding, limits) && offer.manifestPageCount == binding.pageCount;
}

} // namespace

TransferRecoveryStoreOperationResult TransferRecoveryStore::saveOutgoing(const OutgoingRecoveryState &state) const
{
  if (!validRoot(m_rootDirectory))
    return fail(TransferRecoveryStoreError::InvalidStoreDirectory, QStringLiteral("recovery root must be absolute"));
  if (m_limits.maximumEntries == 0)
    return fail(TransferRecoveryStoreError::TooManyEntries, QStringLiteral("recovery entry limit is zero"));
  if (!validCommon(state.transferId, state.localDeviceId, state.peerDeviceId, state.peerFingerprintSha256) ||
      !state.createdUtc.isValid() || state.createdUtc.toMSecsSinceEpoch() <= 0 ||
      !validPolicy(state.sendOptions.conflictPolicy) || !validPolicy(state.effectiveConflictPolicy) ||
      m_limits.maximumEntries == 0 || state.entries.isEmpty() ||
      static_cast<quint64>(state.entries.size()) > m_limits.maximumEntries || state.summary.id != state.transferId ||
      state.summary.canonicalSha256.size() != kSha256Bytes || !validCount(state.progress.completedBytes) ||
      !validCount(state.progress.completedFiles) || !validCount(state.progress.currentEntry) ||
      state.progress.completedBytes > state.summary.totalBytes ||
      state.progress.completedFiles > state.summary.fileCount ||
      state.progress.currentEntry > static_cast<quint64>(state.entries.size()))
    return fail(TransferRecoveryStoreError::InvalidState, QStringLiteral("outgoing recovery state is invalid"));
  if (!validateSources(state.sourceRoots, m_limits.maximumEntries, m_limits.pathLimits))
    return fail(TransferRecoveryStoreError::InvalidPath, QStringLiteral("outgoing recovery source path is invalid"));
  for (const auto &entry : state.entries)
    if (!validAbsolutePath(entry.canonicalSourcePath) ||
        !validRelativePath(entry.entry.relativeProtocolPath, m_limits.pathLimits) ||
        !validRecoveryDigest(entry.entry))
      return fail(TransferRecoveryStoreError::InvalidPath, QStringLiteral("outgoing recovery entry is invalid"));
  if (!validateManifest(state.entries, state.summary, state.pagePlan, m_limits.pathLimits))
    return fail(TransferRecoveryStoreError::InvalidState, QStringLiteral("outgoing recovery manifest is inconsistent"));
  const QCborMap map{
      {key(SchemaKey), static_cast<qint64>(kTransferRecoverySchemaVersion)},
      {key(TransferKey), state.transferId.toBytes()},
      {key(LocalKey), state.localDeviceId.toBytes()},
      {key(PeerKey), state.peerDeviceId.toBytes()},
      {key(FingerprintKey), state.peerFingerprintSha256},
      {key(CreatedKey), state.createdUtc.toMSecsSinceEpoch()},
      {key(OptionsKey), QCborMap{{key(1), static_cast<qint64>(state.sendOptions.conflictPolicy)}}},
      {key(SourcesKey), encodeSources(state.sourceRoots)},
      {key(EntriesKey), encodeEntries(state.entries)},
      {key(SummaryKey), encodeSummary(state.summary)},
      {key(PlanKey), encodePlan(state.pagePlan)},
      {key(EffectivePolicyKey), static_cast<qint64>(state.effectiveConflictPolicy)},
      {key(ProgressKey), encodeProgress(state.progress)}
  };
  return writeState(
      outgoingStatePath(state.transferId), QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps),
      m_limits.maximumEncodedBytes
  );
}

TransferRecoveryStoreOperationResult TransferRecoveryStore::saveIncoming(const IncomingRecoveryState &state) const
{
  if (!validRoot(m_rootDirectory))
    return fail(TransferRecoveryStoreError::InvalidStoreDirectory, QStringLiteral("recovery root must be absolute"));
  if (m_limits.maximumEntries == 0)
    return fail(TransferRecoveryStoreError::TooManyEntries, QStringLiteral("recovery entry limit is zero"));
  if (!validCommon(state.transferId, state.localDeviceId, state.peerDeviceId, state.peerFingerprintSha256) ||
      state.offer.transferId != state.transferId || state.offer.manifestSha256.size() != kSha256Bytes ||
      !validPolicy(state.offer.requestedConflictPolicy) || !validPolicy(state.receiveOptions.conflictPolicy) ||
      !validAbsolutePath(state.receiveOptions.destinationRoot) || m_limits.maximumEntries == 0 ||
      static_cast<quint64>(state.entries.size()) > m_limits.maximumEntries || state.offer.createdAtMs == 0 ||
      !validCount(state.offer.createdAtMs) || !validCount(state.offer.totalBytes) ||
      !validCount(state.offer.fileCount) || !validCount(state.offer.directoryCount) ||
      !validCount(state.offer.manifestPageCount) ||
      state.pagePlan.entryCount != static_cast<quint64>(state.entries.size()) ||
      state.offer.fileCount > std::numeric_limits<quint64>::max() - state.offer.directoryCount ||
      state.offer.fileCount + state.offer.directoryCount != static_cast<quint64>(state.entries.size()) ||
      !validateIncomingManifest(state.entries, state.offer, state.pagePlan, m_limits.pathLimits) ||
      !validateNegotiatedCapabilities(state.negotiatedCapabilities))
    return fail(TransferRecoveryStoreError::InvalidState, QStringLiteral("incoming recovery state is invalid"));
  for (const auto &entry : state.entries)
    if (!validRelativePath(entry.relativeProtocolPath, m_limits.pathLimits) || !validRecoveryDigest(entry))
      return fail(TransferRecoveryStoreError::InvalidPath, QStringLiteral("incoming recovery entry is invalid"));
  QCborArray entries;
  for (const auto &entry : state.entries)
    entries.append(encodeEntry(entry));
  const QCborMap map{
      {key(SchemaKey), static_cast<qint64>(kTransferRecoverySchemaVersion)},
      {key(TransferKey), state.transferId.toBytes()},
      {key(LocalKey), state.localDeviceId.toBytes()},
      {key(PeerKey), state.peerDeviceId.toBytes()},
      {key(FingerprintKey), state.peerFingerprintSha256},
      {key(PeerNameKey), state.peerDisplayName},
      {key(OfferKey), encodeOffer(state.offer)},
      {key(ReceiveOptionsKey), encodeReceiveOptions(state.receiveOptions)},
      {key(EntriesKey), entries},
      {key(PlanKey), encodePlan(state.pagePlan)},
      {key(ResumeKey), encodeCapabilities(state.negotiatedCapabilities)}
  };
  return writeState(
      incomingStatePath(state.transferId), QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps),
      m_limits.maximumEncodedBytes
  );
}

TransferRecoveryStoreLoadResult<OutgoingRecoveryState> TransferRecoveryStore::loadOutgoing(const TransferId &id) const
{
  if (!validRoot(m_rootDirectory))
    return loadFail<OutgoingRecoveryState>(
        TransferRecoveryStoreError::InvalidStoreDirectory, QStringLiteral("recovery root must be absolute")
    );
  return readState<OutgoingRecoveryState>(
      outgoingStatePath(id), id, m_limits.maximumEncodedBytes,
      [this](const QCborMap &map) -> std::optional<OutgoingRecoveryState> {
        if (!exactKeys(
                map, {SchemaKey, TransferKey, LocalKey, PeerKey, FingerprintKey, CreatedKey, OptionsKey, SourcesKey,
                      EntriesKey, SummaryKey, PlanKey, EffectivePolicyKey, ProgressKey}
            ))
          return std::nullopt;
        const auto schema = valueFor(map, SchemaKey);
        if (!schema.isInteger() || schema.toInteger() != static_cast<qint64>(kTransferRecoverySchemaVersion))
          return std::nullopt;
        const auto transfer = TransferId::fromBytes(valueFor(map, TransferKey).toByteArray());
        const auto local = deskflow::relaydesk::DeviceId::fromBytes(valueFor(map, LocalKey).toByteArray());
        const auto peer = deskflow::relaydesk::DeviceId::fromBytes(valueFor(map, PeerKey).toByteArray());
        const auto fingerprint = valueFor(map, FingerprintKey);
        const auto created = valueFor(map, CreatedKey);
        const auto options = valueFor(map, OptionsKey);
        const auto policyValue = valueFor(map, EffectivePolicyKey);
        if (!transfer || !local || !peer || !fingerprint.isByteArray() ||
            fingerprint.toByteArray().size() != kSha256Bytes || !created.isInteger() || created.toInteger() <= 0 ||
            !options.isMap() || !exactKeys(options.toMap(), {1}) || !valueFor(options.toMap(), 1).isInteger() ||
            !policyValue.isInteger())
          return std::nullopt;
        const auto sendPolicy = policy(valueFor(options.toMap(), 1).toInteger());
        const auto effective = policy(policyValue.toInteger());
        const auto sources = decodeSources(valueFor(map, SourcesKey), m_limits.pathLimits);
        const auto entries = decodePreparedEntries(valueFor(map, EntriesKey), m_limits.pathLimits);
        const auto summary = decodeSummary(valueFor(map, SummaryKey));
        const auto plan = decodePlan(valueFor(map, PlanKey));
        const auto progress = decodeProgress(valueFor(map, ProgressKey));
        if (!sendPolicy || !effective || !sources || !entries || !summary || !plan || !progress ||
            static_cast<quint64>(entries->size()) > m_limits.maximumEntries ||
            !validateSources(*sources, m_limits.maximumEntries, m_limits.pathLimits))
          return std::nullopt;
        OutgoingRecoveryState state{
            *transfer,
            *local,
            *peer,
            fingerprint.toByteArray(),
            QDateTime::fromMSecsSinceEpoch(created.toInteger(), Qt::UTC),
            SendOptions{*sendPolicy},
            *sources,
            *entries,
            *summary,
            *plan,
            *effective,
            *progress
        };
        if (!validateManifest(state.entries, state.summary, state.pagePlan, m_limits.pathLimits) ||
            state.summary.id != state.transferId || state.progress.completedBytes > state.summary.totalBytes ||
            state.progress.completedFiles > state.summary.fileCount ||
            state.progress.currentEntry > static_cast<quint64>(state.entries.size()))
          return std::nullopt;
        return state;
      }
  );
}

TransferRecoveryStoreLoadResult<IncomingRecoveryState> TransferRecoveryStore::loadIncoming(const TransferId &id) const
{
  if (!validRoot(m_rootDirectory))
    return loadFail<IncomingRecoveryState>(
        TransferRecoveryStoreError::InvalidStoreDirectory, QStringLiteral("recovery root must be absolute")
    );
  return readState<IncomingRecoveryState>(
      incomingStatePath(id), id, m_limits.maximumEncodedBytes,
      [this](const QCborMap &map) -> std::optional<IncomingRecoveryState> {
        if (!exactKeys(
                map, {SchemaKey, TransferKey, LocalKey, PeerKey, FingerprintKey, PeerNameKey, OfferKey,
                      ReceiveOptionsKey, EntriesKey, PlanKey, ResumeKey}
            ))
          return std::nullopt;
        const auto schema = valueFor(map, SchemaKey);
        if (!schema.isInteger() || schema.toInteger() != static_cast<qint64>(kTransferRecoverySchemaVersion))
          return std::nullopt;
        const auto transfer = TransferId::fromBytes(valueFor(map, TransferKey).toByteArray());
        const auto local = deskflow::relaydesk::DeviceId::fromBytes(valueFor(map, LocalKey).toByteArray());
        const auto peer = deskflow::relaydesk::DeviceId::fromBytes(valueFor(map, PeerKey).toByteArray());
        const auto fingerprint = valueFor(map, FingerprintKey);
        const auto name = valueFor(map, PeerNameKey);
        const auto capabilities = decodeCapabilities(valueFor(map, ResumeKey));
        const auto offer = decodeOffer(valueFor(map, OfferKey));
        const auto options = decodeReceiveOptions(valueFor(map, ReceiveOptionsKey));
        const auto plan = decodePlan(valueFor(map, PlanKey));
        if (!transfer || !local || !peer || !fingerprint.isByteArray() ||
            fingerprint.toByteArray().size() != kSha256Bytes || !name.isString() || !capabilities || !offer ||
            !options || !plan || offer->transferId != *transfer || !valueFor(map, EntriesKey).isArray())
          return std::nullopt;
        QList<ManifestEntry> entries;
        for (const auto &item : valueFor(map, EntriesKey).toArray()) {
          const auto entry = decodeEntry(item, m_limits.pathLimits);
          if (!entry)
            return std::nullopt;
          entries.append(*entry);
        }
        if (static_cast<quint64>(entries.size()) > m_limits.maximumEntries)
          return std::nullopt;
        IncomingRecoveryState state{*transfer,       *local,       *peer,    fingerprint.toByteArray(),
                                    name.toString(), *offer,       *options, entries,
                                    *plan,           *capabilities};
        if (!validateIncomingManifest(state.entries, state.offer, state.pagePlan, m_limits.pathLimits))
          return std::nullopt;
        return state;
      }
  );
}

template <typename State, typename Loader>
TransferRecoveryStoreScanResult<State> scanStates(const QString &directory, quint64 maximumStates, Loader loader)
{
  TransferRecoveryStoreScanResult<State> result;
  const QDir dir(directory);
  if (!dir.exists())
    return result;
  if (maximumStates == 0)
    return {
        .error = TransferRecoveryStoreError::TooManyStates,
        .diagnostic = QStringLiteral("recovery state scan limit is zero")
    };
  QDirIterator entries(
      directory, {QStringLiteral("*.recovery.cbor")}, QDir::Files | QDir::NoDotAndDotDot
  );
  quint64 scanned = 0;
  while (entries.hasNext()) {
    if (scanned == maximumStates) {
      result.issues.append(
          {dir.absolutePath(), TransferRecoveryStoreError::TooManyStates,
           QStringLiteral("recovery state scan limit exceeded")}
      );
      break;
    }
    entries.next();
    ++scanned;
    const QFileInfo entry = entries.fileInfo();
    if (entry.isSymLink()) {
      result.issues.append(
          {entry.absoluteFilePath(), TransferRecoveryStoreError::InvalidPath,
           QStringLiteral("recovery state symbolic links are not followed")}
      );
      continue;
    }
    QString name = entry.fileName();
    name.chop(QStringLiteral(".recovery.cbor").size());
    const auto id = TransferId::fromString(name);
    if (!id || id->toString().compare(name, Qt::CaseInsensitive) != 0) {
      result.issues.append(
          {entry.absoluteFilePath(), TransferRecoveryStoreError::InvalidFields,
           QStringLiteral("recovery filename is not a canonical transfer ID")}
      );
      continue;
    }
    auto loaded = loader(*id);
    if (loaded.ok())
      result.states.append(std::move(*loaded.state));
    else
      result.issues.append({entry.absoluteFilePath(), loaded.error, std::move(loaded.diagnostic)});
  }
  return result;
}

TransferRecoveryStoreScanResult<OutgoingRecoveryState> TransferRecoveryStore::scanOutgoing() const
{
  if (!validRoot(m_rootDirectory))
    return {
        .error = TransferRecoveryStoreError::InvalidStoreDirectory,
        .diagnostic = QStringLiteral("recovery root must be absolute")
    };
  return scanStates<OutgoingRecoveryState>(
      QDir(m_rootDirectory).filePath(QStringLiteral("outgoing")), m_limits.maximumStates,
      [this](const TransferId &id) { return loadOutgoing(id); }
  );
}
TransferRecoveryStoreScanResult<IncomingRecoveryState> TransferRecoveryStore::scanIncoming() const
{
  if (!validRoot(m_rootDirectory))
    return {
        .error = TransferRecoveryStoreError::InvalidStoreDirectory,
        .diagnostic = QStringLiteral("recovery root must be absolute")
    };
  return scanStates<IncomingRecoveryState>(
      QDir(m_rootDirectory).filePath(QStringLiteral("incoming")), m_limits.maximumStates,
      [this](const TransferId &id) { return loadIncoming(id); }
  );
}

TransferRecoveryStoreOperationResult removeState(const QString &root, const QString &path)
{
  if (!validRoot(root))
    return fail(TransferRecoveryStoreError::InvalidStoreDirectory, QStringLiteral("recovery root must be absolute"));
  if (!QFileInfo::exists(path))
    return {};
  QFile file(path);
  if (!file.remove())
    return fail(TransferRecoveryStoreError::RemoveFailed, file.errorString());
  return {};
}
TransferRecoveryStoreOperationResult TransferRecoveryStore::removeOutgoing(const TransferId &id) const
{
  return removeState(m_rootDirectory, outgoingStatePath(id));
}
TransferRecoveryStoreOperationResult TransferRecoveryStore::removeIncoming(const TransferId &id) const
{
  return removeState(m_rootDirectory, incomingStatePath(id));
}
QString TransferRecoveryStore::outgoingStatePath(const TransferId &transferId) const
{
  return QDir(m_rootDirectory).filePath(QStringLiteral("outgoing/%1.recovery.cbor").arg(transferId.toString()));
}
QString TransferRecoveryStore::incomingStatePath(const TransferId &transferId) const
{
  return QDir(m_rootDirectory).filePath(QStringLiteral("incoming/%1.recovery.cbor").arg(transferId.toString()));
}

} // namespace relaydesk::transfer
