// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ManifestPageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QCborValue>
#include <QCryptographicHash>
#include <QTimeZone>

#include <algorithm>
#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr quint64 kMaxWireInteger = static_cast<quint64>(std::numeric_limits<qint64>::max());

ManifestPagePlanResult planFailure(ManifestPageError error, QString diagnostic)
{
  return {.plan = std::nullopt, .error = error, .diagnostic = std::move(diagnostic)};
}

ManifestPageDecodeResult decodeFailure(ManifestPageError error, QString diagnostic)
{
  return {.page = std::nullopt, .error = error, .diagnostic = std::move(diagnostic)};
}

ManifestCompleteDecodeResult completeDecodeFailure(ManifestPageError error, QString diagnostic)
{
  return {.message = std::nullopt, .error = error, .diagnostic = std::move(diagnostic)};
}

ManifestReassemblyResult reassemblyFailure(ManifestPageError error, QString diagnostic)
{
  return {.entries = std::nullopt, .error = error, .diagnostic = std::move(diagnostic)};
}

void setDiagnostic(QString *output, QString diagnostic)
{
  if (output != nullptr) {
    *output = std::move(diagnostic);
  }
}

bool limitsAreValid(const ManifestPagingLimits &limits)
{
  return limits.maxPageMetadataBytes > 0 && limits.maxPageMetadataBytes <= kMaxManifestPageMetadataBytes &&
         limits.maxEntriesPerPage > 0 && limits.maxEntriesPerPage <= kMaxManifestEntriesPerPage &&
         limits.maxPages > 0 && limits.maxPages <= kMaxManifestPageCount && limits.maxEntries > 0 &&
         limits.maxEntries <= kMaxPagedManifestEntries && limits.maxManifestMetadataBytes > 0 &&
         limits.maxManifestMetadataBytes <= kMaxPagedManifestMetadataBytes;
}

bool isWireInteger(quint64 value)
{
  return value <= kMaxWireInteger;
}

qsizetype unsignedCborBytes(quint64 value)
{
  if (value < 24) {
    return 1;
  }
  if (value <= std::numeric_limits<quint8>::max()) {
    return 2;
  }
  if (value <= std::numeric_limits<quint16>::max()) {
    return 3;
  }
  if (value <= std::numeric_limits<quint32>::max()) {
    return 5;
  }
  return 9;
}

QByteArray arrayHeader(quint64 count)
{
  QByteArray output;
  if (count < 24) {
    output.append(static_cast<char>(0x80U | static_cast<quint8>(count)));
  } else if (count <= std::numeric_limits<quint8>::max()) {
    output.append(static_cast<char>(0x98U));
    output.append(static_cast<char>(count));
  } else if (count <= std::numeric_limits<quint16>::max()) {
    output.append(static_cast<char>(0x99U));
    output.append(static_cast<char>((count >> 8U) & 0xffU));
    output.append(static_cast<char>(count & 0xffU));
  } else if (count <= std::numeric_limits<quint32>::max()) {
    output.append(static_cast<char>(0x9aU));
    for (int shift = 24; shift >= 0; shift -= 8) {
      output.append(static_cast<char>((count >> static_cast<unsigned>(shift)) & 0xffU));
    }
  } else {
    output.append(static_cast<char>(0x9bU));
    for (int shift = 56; shift >= 0; shift -= 8) {
      output.append(static_cast<char>((count >> static_cast<unsigned>(shift)) & 0xffU));
    }
  }
  return output;
}

void insertUnsigned(QCborMap &map, qint64 field, quint64 value)
{
  map.insert(QCborValue(field), QCborValue(static_cast<qint64>(value)));
}

QCborMap entryMap(const ManifestEntry &entry)
{
  QCborMap map;
  map.insert(QCborValue(1), QCborValue(entry.id.toRfc4122()));
  map.insert(QCborValue(2), QCborValue(entry.relativeProtocolPath));
  insertUnsigned(map, 3, static_cast<quint8>(entry.type));
  insertUnsigned(map, 4, entry.size);
  insertUnsigned(map, 5, static_cast<quint64>(entry.modifiedUtc.toMSecsSinceEpoch()));
  map.insert(QCborValue(6), QCborValue(entry.sha256));
  insertUnsigned(map, 7, entry.flags);
  return map;
}

QByteArray canonicalEntryBytes(const ManifestEntry &entry)
{
  return QCborValue(entryMap(entry)).toCbor(QCborValue::SortKeysInMaps);
}

bool entryLess(const ManifestEntry &left, const ManifestEntry &right)
{
  const QByteArray leftPath = left.relativeProtocolPath.toUtf8();
  const QByteArray rightPath = right.relativeProtocolPath.toUtf8();
  if (leftPath != rightPath) {
    return leftPath < rightPath;
  }
  if (left.type != right.type) {
    return left.type < right.type;
  }
  return left.id.toRfc4122() < right.id.toRfc4122();
}

ManifestPageError validateEntry(const ManifestEntry &entry, const PathLimits &pathLimits, QString &diagnostic)
{
  if (entry.id.isNull() || entry.id.toRfc4122().size() != kUuidBytes) {
    diagnostic = QStringLiteral("manifest fileId must be a non-null 16-byte UUID");
    return ManifestPageError::InvalidManifestEntry;
  }
  const PathValidationResult path = PathPolicy::validateRelative(entry.relativeProtocolPath, pathLimits);
  if (!path.ok || path.normalized != entry.relativeProtocolPath) {
    diagnostic = QStringLiteral("manifest path must already be a safe normalized RDFT/1 path");
    return ManifestPageError::InvalidManifestEntry;
  }
  if (entry.type != ManifestEntryType::File && entry.type != ManifestEntryType::Directory) {
    diagnostic = QStringLiteral("manifest entry type is not defined by RDFT/1");
    return ManifestPageError::InvalidManifestEntry;
  }
  if (!entry.modifiedUtc.isValid() || !isWireInteger(entry.size) || entry.modifiedUtc.toMSecsSinceEpoch() < 0 ||
      !isWireInteger(static_cast<quint64>(entry.modifiedUtc.toMSecsSinceEpoch()))) {
    diagnostic = QStringLiteral("manifest numeric field exceeds the Qt CBOR integer range");
    return ManifestPageError::InvalidManifestEntry;
  }
  if (entry.type == ManifestEntryType::Directory) {
    if (entry.size != 0 || !entry.sha256.isEmpty()) {
      diagnostic = QStringLiteral("directory entries must have zero size and no SHA-256");
      return ManifestPageError::InvalidManifestEntry;
    }
  } else if (!entry.sha256.isEmpty() && entry.sha256.size() != kSha256Bytes) {
    diagnostic = QStringLiteral("file SHA-256 must be absent or contain 32 bytes");
    return ManifestPageError::InvalidManifestEntry;
  }
  return ManifestPageError::None;
}

quint64 pageEnvelopeBytes(quint64 pageIndex, quint64 pageCount, quint64 entryCount)
{
  // Deterministic CBOR map {1: uuid, 2: index, 3: count, 4: entries}.
  return 1U + 18U + 1U + static_cast<quint64>(unsignedCborBytes(pageIndex)) + 1U +
         static_cast<quint64>(unsignedCborBytes(pageCount)) + 1U + static_cast<quint64>(arrayHeader(entryCount).size());
}

ManifestPage pageForRange(const TransferManifest &manifest, const ManifestPagePlan &plan, quint64 pageIndex)
{
  ManifestPage page;
  page.transferId = plan.transferId;
  page.pageIndex = pageIndex;
  page.pageCount = plan.pageCount();
  const ManifestPageRange &range = plan.ranges.at(static_cast<qsizetype>(pageIndex));
  page.entries.reserve(range.entryCount);
  for (qsizetype offset = 0; offset < range.entryCount; ++offset) {
    page.entries.append(manifest.entries.at(range.firstEntry + offset).entry);
  }
  return page;
}

bool planMatchesManifest(
    const TransferManifest &manifest, const ManifestPagePlan &plan, const ManifestPagingLimits &limits
)
{
  if (plan.transferId.isNull() || plan.transferId != manifest.summary.id || plan.ranges.isEmpty() ||
      plan.pageCount() > limits.maxPages || plan.entryCount != static_cast<quint64>(manifest.entries.size())) {
    return false;
  }
  qsizetype expectedFirst = 0;
  for (const ManifestPageRange &range : plan.ranges) {
    if (range.firstEntry != expectedFirst || range.entryCount <= 0 ||
        static_cast<quint64>(range.entryCount) > limits.maxEntriesPerPage ||
        range.entryCount > manifest.entries.size() - expectedFirst) {
      return false;
    }
    expectedFirst += range.entryCount;
  }
  return expectedFirst == manifest.entries.size();
}

ManifestPageDecodeResult invalidType(qint64 field, const QString &expected)
{
  return decodeFailure(
      ManifestPageError::InvalidFieldType, QStringLiteral("field %1 must be %2").arg(field).arg(expected)
  );
}

ManifestPageDecodeResult invalidValue(qint64 field, const QString &reason)
{
  return decodeFailure(
      ManifestPageError::InvalidFieldValue, QStringLiteral("field %1 is invalid: %2").arg(field).arg(reason)
  );
}

std::optional<QCborValue> requiredValue(const QCborMap &map, qint64 field)
{
  const QCborValue key(field);
  if (!map.contains(key)) {
    return std::nullopt;
  }
  return map.value(key);
}

bool mapKeysAreIntegers(const QCborMap &map)
{
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || iterator.key().toInteger() < 0) {
      return false;
    }
  }
  return true;
}

bool readUnsigned(const QCborMap &map, qint64 field, quint64 &output, ManifestPageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = decodeFailure(ManifestPageError::MissingField, QStringLiteral("required field %1 is missing").arg(field));
    return false;
  }
  if (!value->isInteger()) {
    failure = invalidType(field, QStringLiteral("an unsigned integer"));
    return false;
  }
  const qint64 integer = value->toInteger();
  if (integer < 0) {
    failure = invalidValue(field, QStringLiteral("integer must not be negative"));
    return false;
  }
  output = static_cast<quint64>(integer);
  return true;
}

bool readUuid(const QCborMap &map, qint64 field, QUuid &output, ManifestPageDecodeResult &failure)
{
  const auto value = requiredValue(map, field);
  if (!value.has_value()) {
    failure = decodeFailure(ManifestPageError::MissingField, QStringLiteral("required field %1 is missing").arg(field));
    return false;
  }
  if (!value->isByteArray()) {
    failure = invalidType(field, QStringLiteral("a 16-byte UUID byte string"));
    return false;
  }
  const QByteArray bytes = value->toByteArray();
  if (bytes.size() != kUuidBytes || QUuid::fromRfc4122(bytes).isNull()) {
    failure = invalidValue(field, QStringLiteral("UUID must contain 16 non-null bytes"));
    return false;
  }
  output = QUuid::fromRfc4122(bytes);
  return true;
}

ManifestPageDecodeResult decodeEntry(const QCborValue &value, const ManifestPagingLimits &limits)
{
  if (!value.isMap()) {
    return decodeFailure(ManifestPageError::InvalidFieldType, QStringLiteral("manifest entry must be a CBOR map"));
  }
  const QCborMap map = value.toMap();
  if (!mapKeysAreIntegers(map)) {
    return decodeFailure(
        ManifestPageError::NonIntegerKey, QStringLiteral("manifest entry keys must be non-negative integers")
    );
  }

  ManifestEntry entry;
  ManifestPageDecodeResult failure;
  quint64 type = 0;
  quint64 modifiedAtMs = 0;
  quint64 flags = 0;
  if (!readUuid(map, 1, entry.id, failure)) {
    return failure;
  }
  const auto path = requiredValue(map, 2);
  if (!path.has_value()) {
    return decodeFailure(ManifestPageError::MissingField, QStringLiteral("required field 2 is missing"));
  }
  if (!path->isString()) {
    return invalidType(2, QStringLiteral("a UTF-8 text string"));
  }
  entry.relativeProtocolPath = path->toString();
  if (!readUnsigned(map, 3, type, failure) || !readUnsigned(map, 4, entry.size, failure) ||
      !readUnsigned(map, 5, modifiedAtMs, failure)) {
    return failure;
  }
  if (type > static_cast<quint64>(ManifestEntryType::Directory)) {
    return invalidValue(3, QStringLiteral("entry type is not defined by RDFT/1"));
  }
  entry.type = static_cast<ManifestEntryType>(type);
  entry.modifiedUtc = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(modifiedAtMs), QTimeZone::UTC);
  if (map.contains(QCborValue(6))) {
    const QCborValue sha = map.value(QCborValue(6));
    if (!sha.isByteArray()) {
      return invalidType(6, QStringLiteral("a byte string"));
    }
    entry.sha256 = sha.toByteArray();
  }
  if (map.contains(QCborValue(7))) {
    if (!readUnsigned(map, 7, flags, failure)) {
      return failure;
    }
    if (flags > std::numeric_limits<quint32>::max()) {
      return invalidValue(7, QStringLiteral("flags exceed uint32"));
    }
    entry.flags = static_cast<quint32>(flags);
  }

  QString diagnostic;
  if (validateEntry(entry, limits.pathLimits, diagnostic) != ManifestPageError::None) {
    return decodeFailure(ManifestPageError::InvalidManifestEntry, diagnostic);
  }
  ManifestPage synthetic;
  synthetic.entries.append(std::move(entry));
  return {.page = std::move(synthetic)};
}

} // namespace

ManifestPagePlanResult ManifestPageCodec::plan(const TransferManifest &manifest, const ManifestPagingLimits &limits)
{
  if (!limitsAreValid(limits)) {
    return planFailure(ManifestPageError::InvalidLimits, QStringLiteral("manifest paging limits exceed RDFT/1"));
  }
  if (manifest.summary.id.isNull()) {
    return planFailure(ManifestPageError::InvalidManifestEntry, QStringLiteral("transferId must not be null"));
  }
  if (manifest.entries.isEmpty()) {
    return planFailure(ManifestPageError::EmptyManifest, QStringLiteral("manifest must contain at least one entry"));
  }
  if (static_cast<quint64>(manifest.entries.size()) > limits.maxEntries) {
    return planFailure(ManifestPageError::TooManyEntries, QStringLiteral("manifest entry limit exceeded"));
  }

  QList<quint64> encodedEntrySizes;
  encodedEntrySizes.reserve(manifest.entries.size());
  const ManifestEntry *previous = nullptr;
  quint64 canonicalBytes = static_cast<quint64>(arrayHeader(manifest.entries.size()).size());
  QCryptographicHash canonicalHash(QCryptographicHash::Sha256);
  const QByteArray canonicalHeader = arrayHeader(manifest.entries.size());
  canonicalHash.addData(QByteArrayView(canonicalHeader));
  QSet<QString> collisionKeys;
  QSet<QByteArray> fileIds;
  quint64 fileCount = 0;
  quint64 directoryCount = 0;
  quint64 totalBytes = 0;
  for (const PreparedManifestEntry &prepared : manifest.entries) {
    QString diagnostic;
    const ManifestPageError validation = validateEntry(prepared.entry, limits.pathLimits, diagnostic);
    if (validation != ManifestPageError::None) {
      return planFailure(validation, diagnostic);
    }
    if (previous != nullptr && !entryLess(*previous, prepared.entry)) {
      return planFailure(
          ManifestPageError::InvalidManifestOrder,
          QStringLiteral("manifest entries must be strictly sorted by UTF-8 path, type, and fileId")
      );
    }
    const PathValidationResult path =
        PathPolicy::validateRelative(prepared.entry.relativeProtocolPath, limits.pathLimits);
    if (collisionKeys.contains(path.collisionKey)) {
      return planFailure(
          ManifestPageError::ProtocolPathCollision, QStringLiteral("manifest contains a portable path collision")
      );
    }
    collisionKeys.insert(path.collisionKey);
    const QByteArray fileId = prepared.entry.id.toRfc4122();
    if (fileIds.contains(fileId)) {
      return planFailure(ManifestPageError::DuplicateFileId, QStringLiteral("manifest contains a duplicate fileId"));
    }
    fileIds.insert(fileId);
    if (prepared.entry.type == ManifestEntryType::File) {
      ++fileCount;
      if (prepared.entry.size > std::numeric_limits<quint64>::max() - totalBytes) {
        return planFailure(
            ManifestPageError::InvalidManifestEntry, QStringLiteral("manifest total byte count overflows uint64")
        );
      }
      totalBytes += prepared.entry.size;
    } else {
      ++directoryCount;
    }
    previous = &prepared.entry;
    QByteArray encoded = canonicalEntryBytes(prepared.entry);
    if (static_cast<quint64>(encoded.size()) > limits.maxPageMetadataBytes) {
      return planFailure(ManifestPageError::EntryTooLarge, QStringLiteral("one manifest entry cannot fit in a page"));
    }
    if (static_cast<quint64>(encoded.size()) > std::numeric_limits<quint64>::max() - canonicalBytes) {
      return planFailure(
          ManifestPageError::ManifestMetadataTooLarge, QStringLiteral("canonical manifest size overflow")
      );
    }
    canonicalBytes += static_cast<quint64>(encoded.size());
    canonicalHash.addData(QByteArrayView(encoded));
    encodedEntrySizes.append(static_cast<quint64>(encoded.size()));
  }
  if (canonicalBytes > limits.maxManifestMetadataBytes) {
    return planFailure(
        ManifestPageError::ManifestMetadataTooLarge, QStringLiteral("canonical manifest metadata limit exceeded")
    );
  }
  if (manifest.summary.fileCount != fileCount || manifest.summary.directoryCount != directoryCount ||
      manifest.summary.totalBytes != totalBytes) {
    return planFailure(
        ManifestPageError::InvalidManifestEntry, QStringLiteral("manifest summary counts do not match its entries")
    );
  }

  ManifestPagePlan output;
  output.transferId = manifest.summary.id;
  output.entryCount = static_cast<quint64>(manifest.entries.size());
  const quint64 conservativeIndex = limits.maxPages - 1;
  qsizetype first = 0;
  while (first < encodedEntrySizes.size()) {
    qsizetype count = 0;
    quint64 entryBytes = 0;
    while (first + count < encodedEntrySizes.size() && static_cast<quint64>(count) < limits.maxEntriesPerPage) {
      const quint64 nextBytes = encodedEntrySizes.at(first + count);
      const quint64 candidateCount = static_cast<quint64>(count) + 1;
      const quint64 envelope = pageEnvelopeBytes(conservativeIndex, limits.maxPages, candidateCount);
      if (nextBytes > std::numeric_limits<quint64>::max() - entryBytes || envelope > limits.maxPageMetadataBytes ||
          entryBytes + nextBytes > limits.maxPageMetadataBytes - envelope) {
        break;
      }
      entryBytes += nextBytes;
      ++count;
    }
    if (count == 0) {
      return planFailure(ManifestPageError::EntryTooLarge, QStringLiteral("one manifest entry cannot fit in a page"));
    }
    if (static_cast<quint64>(output.ranges.size()) >= limits.maxPages) {
      return planFailure(ManifestPageError::TooManyPages, QStringLiteral("manifest page count limit exceeded"));
    }
    output.ranges.append({.firstEntry = first, .entryCount = count});
    first += count;
  }

  quint64 totalMetadataBytes = 0;
  for (quint64 index = 0; index < output.pageCount(); ++index) {
    const ManifestPage page = pageForRange(manifest, output, index);
    QString error;
    const QByteArray encoded = encode(page, limits, &error);
    if (encoded.isEmpty()) {
      return planFailure(ManifestPageError::PageMetadataTooLarge, error);
    }
    const quint64 bytes = static_cast<quint64>(encoded.size());
    if (bytes > std::numeric_limits<quint64>::max() - totalMetadataBytes ||
        totalMetadataBytes + bytes > limits.maxManifestMetadataBytes) {
      return planFailure(
          ManifestPageError::ManifestMetadataTooLarge, QStringLiteral("paged manifest metadata limit exceeded")
      );
    }
    totalMetadataBytes += bytes;
  }
  output.totalMetadataBytes = totalMetadataBytes;

  if (canonicalHash.result() != manifest.summary.canonicalSha256) {
    return planFailure(
        ManifestPageError::DigestMismatch, QStringLiteral("manifest summary digest does not match its entries")
    );
  }
  return {.plan = std::move(output)};
}

QByteArray ManifestPageCodec::encodePage(
    const TransferManifest &manifest, const ManifestPagePlan &plan, quint64 pageIndex,
    const ManifestPagingLimits &limits, QString *error
)
{
  if (!limitsAreValid(limits) || !planMatchesManifest(manifest, plan, limits) || pageIndex >= plan.pageCount()) {
    setDiagnostic(error, QStringLiteral("manifest page plan does not match the requested page"));
    return {};
  }
  return encode(pageForRange(manifest, plan, pageIndex), limits, error);
}

QByteArray ManifestPageCodec::encode(const ManifestPage &page, const ManifestPagingLimits &limits, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (!limitsAreValid(limits)) {
    setDiagnostic(error, QStringLiteral("manifest paging limits exceed RDFT/1"));
    return {};
  }
  if (page.transferId.isNull() || page.pageCount == 0 || page.pageCount > limits.maxPages ||
      page.pageIndex >= page.pageCount) {
    setDiagnostic(error, QStringLiteral("manifest page identity or index is invalid"));
    return {};
  }
  if (static_cast<quint64>(page.entries.size()) > limits.maxEntriesPerPage) {
    setDiagnostic(error, QStringLiteral("manifest page entry limit exceeded"));
    return {};
  }

  QCborArray entries;
  for (const ManifestEntry &entry : page.entries) {
    QString diagnostic;
    if (validateEntry(entry, limits.pathLimits, diagnostic) != ManifestPageError::None) {
      setDiagnostic(error, diagnostic);
      return {};
    }
    entries.append(entryMap(entry));
  }
  QCborMap map;
  map.insert(QCborValue(1), QCborValue(page.transferId.toRfc4122()));
  insertUnsigned(map, 2, page.pageIndex);
  insertUnsigned(map, 3, page.pageCount);
  map.insert(QCborValue(4), entries);
  const QByteArray encoded = QCborValue(map).toCbor(QCborValue::SortKeysInMaps);
  if (static_cast<quint64>(encoded.size()) > limits.maxPageMetadataBytes) {
    setDiagnostic(error, QStringLiteral("manifest page metadata limit exceeded"));
    return {};
  }
  return encoded;
}

ManifestPageDecodeResult
ManifestPageCodec::decode(quint16 protocolVersion, const QByteArray &metadata, const ManifestPagingLimits &limits)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return decodeFailure(ManifestPageError::UnsupportedVersion, QStringLiteral("unsupported RDFT protocol version"));
  }
  if (!limitsAreValid(limits)) {
    return decodeFailure(ManifestPageError::InvalidLimits, QStringLiteral("manifest paging limits exceed RDFT/1"));
  }
  if (metadata.isEmpty()) {
    return decodeFailure(ManifestPageError::MalformedCbor, QStringLiteral("manifest page metadata is empty"));
  }
  if (static_cast<quint64>(metadata.size()) > limits.maxPageMetadataBytes) {
    return decodeFailure(
        ManifestPageError::PageMetadataTooLarge, QStringLiteral("manifest page metadata limit exceeded")
    );
  }

  QCborStreamReader reader(metadata);
  const QCborValue value = QCborValue::fromCbor(reader);
  if (reader.lastError() != QCborError::NoError || reader.currentOffset() != metadata.size()) {
    return decodeFailure(ManifestPageError::MalformedCbor, QStringLiteral("malformed or trailing CBOR metadata"));
  }
  if (!value.isMap()) {
    return decodeFailure(ManifestPageError::MetadataNotMap, QStringLiteral("manifest page must be one CBOR map"));
  }
  const QCborMap map = value.toMap();
  if (!mapKeysAreIntegers(map)) {
    return decodeFailure(
        ManifestPageError::NonIntegerKey, QStringLiteral("manifest page keys must be non-negative integers")
    );
  }

  ManifestPage page;
  ManifestPageDecodeResult failure;
  if (!readUuid(map, 1, page.transferId, failure) || !readUnsigned(map, 2, page.pageIndex, failure) ||
      !readUnsigned(map, 3, page.pageCount, failure)) {
    return failure;
  }
  if (page.pageCount == 0 || page.pageCount > limits.maxPages) {
    return invalidValue(3, QStringLiteral("page count is zero or exceeds the local limit"));
  }
  if (page.pageIndex >= page.pageCount) {
    return invalidValue(2, QStringLiteral("page index must be less than page count"));
  }
  const auto entriesValue = requiredValue(map, 4);
  if (!entriesValue.has_value()) {
    return decodeFailure(ManifestPageError::MissingField, QStringLiteral("required field 4 is missing"));
  }
  if (!entriesValue->isArray()) {
    return invalidType(4, QStringLiteral("an array of manifest entries"));
  }
  const QCborArray entries = entriesValue->toArray();
  if (static_cast<quint64>(entries.size()) > limits.maxEntriesPerPage ||
      static_cast<quint64>(entries.size()) > limits.maxEntries) {
    return invalidValue(4, QStringLiteral("page entry count exceeds the local limit"));
  }
  page.entries.reserve(entries.size());
  for (const QCborValue &encodedEntry : entries) {
    ManifestPageDecodeResult decoded = decodeEntry(encodedEntry, limits);
    if (!decoded.ok()) {
      return decoded;
    }
    page.entries.append(std::move(decoded.page->entries.front()));
  }
  return {.page = std::move(page)};
}

QByteArray ManifestPageCodec::encodeComplete(const ManifestComplete &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  if (message.transferId.isNull()) {
    setDiagnostic(error, QStringLiteral("manifest complete transferId is null"));
    return {};
  }
  if (message.canonicalSha256.size() != kSha256Bytes) {
    setDiagnostic(error, QStringLiteral("manifest complete digest must contain 32 bytes"));
    return {};
  }
  QCborMap map;
  map.insert(QCborValue(1), QCborValue(message.transferId.toRfc4122()));
  map.insert(QCborValue(2), QCborValue(message.canonicalSha256));
  return QCborValue(map).toCbor(QCborValue::SortKeysInMaps);
}

ManifestCompleteDecodeResult ManifestPageCodec::decodeComplete(quint16 protocolVersion, const QByteArray &metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return completeDecodeFailure(
        ManifestPageError::UnsupportedVersion, QStringLiteral("unsupported RDFT protocol version")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaxManifestPageMetadataBytes) {
    return completeDecodeFailure(
        metadata.isEmpty() ? ManifestPageError::MalformedCbor : ManifestPageError::PageMetadataTooLarge,
        QStringLiteral("manifest complete metadata is empty or exceeds the RDFT/1 bound")
    );
  }

  QCborStreamReader reader(metadata);
  const QCborValue value = QCborValue::fromCbor(reader);
  if (reader.lastError() != QCborError::NoError || reader.currentOffset() != metadata.size()) {
    return completeDecodeFailure(
        ManifestPageError::MalformedCbor, QStringLiteral("malformed or trailing manifest complete metadata")
    );
  }
  if (!value.isMap()) {
    return completeDecodeFailure(ManifestPageError::MetadataNotMap, QStringLiteral("manifest complete must be a map"));
  }
  const QCborMap map = value.toMap();
  if (!mapKeysAreIntegers(map)) {
    return completeDecodeFailure(
        ManifestPageError::NonIntegerKey, QStringLiteral("manifest complete keys must be non-negative integers")
    );
  }

  ManifestComplete message;
  ManifestPageDecodeResult failure;
  if (!readUuid(map, 1, message.transferId, failure)) {
    return completeDecodeFailure(failure.error, std::move(failure.diagnostic));
  }
  const auto digest = requiredValue(map, 2);
  if (!digest.has_value()) {
    return completeDecodeFailure(ManifestPageError::MissingField, QStringLiteral("required field 2 is missing"));
  }
  if (!digest->isByteArray()) {
    return completeDecodeFailure(
        ManifestPageError::InvalidFieldType, QStringLiteral("field 2 must be a 32-byte SHA-256 byte string")
    );
  }
  message.canonicalSha256 = digest->toByteArray();
  if (message.canonicalSha256.size() != kSha256Bytes) {
    return completeDecodeFailure(
        ManifestPageError::InvalidFieldValue, QStringLiteral("field 2 must contain exactly 32 bytes")
    );
  }
  return {.message = std::move(message)};
}

QByteArray ManifestPageCodec::canonicalSha256(const QList<ManifestEntry> &entries, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const QByteArray header = arrayHeader(static_cast<quint64>(entries.size()));
  hash.addData(QByteArrayView(header));
  const ManifestEntry *previous = nullptr;
  for (const ManifestEntry &entry : entries) {
    QString diagnostic;
    if (validateEntry(entry, PathLimits{}, diagnostic) != ManifestPageError::None) {
      setDiagnostic(error, diagnostic);
      return {};
    }
    if (previous != nullptr && !entryLess(*previous, entry)) {
      setDiagnostic(error, QStringLiteral("canonical manifest entries are not strictly sorted"));
      return {};
    }
    previous = &entry;
    const QByteArray encoded = canonicalEntryBytes(entry);
    hash.addData(QByteArrayView(encoded));
  }
  return hash.result();
}

QByteArray ManifestPageCodec::canonicalSha256(const QList<PreparedManifestEntry> &entries, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const QByteArray header = arrayHeader(static_cast<quint64>(entries.size()));
  hash.addData(QByteArrayView(header));
  const ManifestEntry *previous = nullptr;
  for (const PreparedManifestEntry &prepared : entries) {
    const ManifestEntry &entry = prepared.entry;
    QString diagnostic;
    if (validateEntry(entry, PathLimits{}, diagnostic) != ManifestPageError::None) {
      setDiagnostic(error, diagnostic);
      return {};
    }
    if (previous != nullptr && !entryLess(*previous, entry)) {
      setDiagnostic(error, QStringLiteral("canonical manifest entries are not strictly sorted"));
      return {};
    }
    previous = &entry;
    const QByteArray encoded = canonicalEntryBytes(entry);
    hash.addData(QByteArrayView(encoded));
  }
  return hash.result();
}

quint64 ManifestPageCodec::canonicalEntrySize(const ManifestEntry &entry, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  QString diagnostic;
  if (validateEntry(entry, PathLimits{}, diagnostic) != ManifestPageError::None) {
    setDiagnostic(error, diagnostic);
    return 0;
  }
  return static_cast<quint64>(canonicalEntryBytes(entry).size());
}

ManifestPageReassembler::ManifestPageReassembler(
    TransferId expectedTransferId, quint64 expectedPageCount, quint64 expectedEntryCount,
    QByteArray expectedCanonicalSha256, ManifestPagingLimits limits
)
    : m_expectedTransferId(std::move(expectedTransferId)),
      m_expectedPageCount(expectedPageCount),
      m_expectedEntryCount(expectedEntryCount),
      m_expectedCanonicalSha256(std::move(expectedCanonicalSha256)),
      m_limits(std::move(limits))
{
  if (limitsAreValid(m_limits) && m_expectedEntryCount <= m_limits.maxEntries &&
      m_expectedEntryCount <= static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    m_entries.reserve(static_cast<qsizetype>(m_expectedEntryCount));
    m_collisionKeys.reserve(static_cast<qsizetype>(m_expectedEntryCount));
    m_fileIds.reserve(static_cast<qsizetype>(m_expectedEntryCount));
  }
}

ManifestPageError
ManifestPageReassembler::addEncodedPage(quint16 protocolVersion, const QByteArray &metadata, QString *diagnostic)
{
  const ManifestPageDecodeResult decoded = ManifestPageCodec::decode(protocolVersion, metadata, m_limits);
  if (!decoded.ok()) {
    setDiagnostic(diagnostic, decoded.diagnostic);
    return decoded.error;
  }
  return addPageWithEncodedBytes(*decoded.page, static_cast<quint64>(metadata.size()), diagnostic);
}

ManifestPageError ManifestPageReassembler::addPage(const ManifestPage &page, QString *diagnostic)
{
  QString encodeError;
  const QByteArray encoded = ManifestPageCodec::encode(page, m_limits, &encodeError);
  if (encoded.isEmpty()) {
    setDiagnostic(diagnostic, encodeError);
    return ManifestPageError::InvalidManifestEntry;
  }
  return addPageWithEncodedBytes(page, static_cast<quint64>(encoded.size()), diagnostic);
}

ManifestPageError
ManifestPageReassembler::addPageWithEncodedBytes(const ManifestPage &page, quint64 encodedBytes, QString *diagnostic)
{
  if (m_finished) {
    setDiagnostic(diagnostic, QStringLiteral("manifest reassembly is already complete"));
    return ManifestPageError::AlreadyComplete;
  }
  if (!limitsAreValid(m_limits) || m_expectedTransferId.isNull() || m_expectedPageCount == 0 ||
      m_expectedPageCount > m_limits.maxPages || m_expectedEntryCount > m_limits.maxEntries ||
      m_expectedCanonicalSha256.size() != kSha256Bytes) {
    setDiagnostic(diagnostic, QStringLiteral("expected manifest parameters exceed RDFT/1 limits"));
    return ManifestPageError::InvalidLimits;
  }
  if (page.transferId != m_expectedTransferId) {
    setDiagnostic(diagnostic, QStringLiteral("manifest page transferId does not match the offer"));
    return ManifestPageError::TransferMismatch;
  }
  if (page.pageCount != m_expectedPageCount) {
    setDiagnostic(diagnostic, QStringLiteral("manifest page count does not match the offer"));
    return ManifestPageError::PageCountMismatch;
  }
  if (page.pageIndex < m_nextPageIndex) {
    setDiagnostic(diagnostic, QStringLiteral("manifest page is a duplicate"));
    return ManifestPageError::DuplicatePage;
  }
  if (page.pageIndex > m_nextPageIndex) {
    setDiagnostic(diagnostic, QStringLiteral("manifest page arrived out of order or a prior page is missing"));
    return ManifestPageError::OutOfOrderPage;
  }
  if (encodedBytes > std::numeric_limits<quint64>::max() - m_receivedMetadataBytes ||
      m_receivedMetadataBytes + encodedBytes > m_limits.maxManifestMetadataBytes) {
    setDiagnostic(diagnostic, QStringLiteral("paged manifest metadata limit exceeded"));
    return ManifestPageError::ManifestMetadataTooLarge;
  }
  if (static_cast<quint64>(page.entries.size()) > m_expectedEntryCount - entryCount()) {
    setDiagnostic(diagnostic, QStringLiteral("manifest contains more entries than offered"));
    return ManifestPageError::TooManyEntries;
  }

  const ManifestEntry *previous = m_entries.isEmpty() ? nullptr : &m_entries.constLast();
  QSet<QString> pageCollisionKeys;
  QSet<QByteArray> pageFileIds;
  for (const ManifestEntry &entry : page.entries) {
    QString entryDiagnostic;
    if (validateEntry(entry, m_limits.pathLimits, entryDiagnostic) != ManifestPageError::None) {
      setDiagnostic(diagnostic, entryDiagnostic);
      return ManifestPageError::InvalidManifestEntry;
    }
    if (previous != nullptr && !entryLess(*previous, entry)) {
      setDiagnostic(diagnostic, QStringLiteral("manifest entries are duplicated or not in canonical order"));
      return ManifestPageError::InvalidManifestOrder;
    }
    const PathValidationResult path = PathPolicy::validateRelative(entry.relativeProtocolPath, m_limits.pathLimits);
    if (m_collisionKeys.contains(path.collisionKey) || pageCollisionKeys.contains(path.collisionKey)) {
      setDiagnostic(diagnostic, QStringLiteral("manifest contains a portable path collision"));
      return ManifestPageError::ProtocolPathCollision;
    }
    const QByteArray fileId = entry.id.toRfc4122();
    if (m_fileIds.contains(fileId) || pageFileIds.contains(fileId)) {
      setDiagnostic(diagnostic, QStringLiteral("manifest contains a duplicate fileId"));
      return ManifestPageError::DuplicateFileId;
    }
    pageCollisionKeys.insert(path.collisionKey);
    pageFileIds.insert(fileId);
    previous = &entry;
  }

  for (const ManifestEntry &entry : page.entries) {
    const PathValidationResult path = PathPolicy::validateRelative(entry.relativeProtocolPath, m_limits.pathLimits);
    m_collisionKeys.insert(path.collisionKey);
    m_fileIds.insert(entry.id.toRfc4122());
    m_entries.append(entry);
  }
  m_receivedMetadataBytes += encodedBytes;
  ++m_nextPageIndex;
  return ManifestPageError::None;
}

ManifestReassemblyResult ManifestPageReassembler::finish()
{
  if (m_finished) {
    return reassemblyFailure(ManifestPageError::AlreadyComplete, QStringLiteral("manifest is already finalized"));
  }
  if (m_nextPageIndex != m_expectedPageCount) {
    return reassemblyFailure(ManifestPageError::MissingPage, QStringLiteral("one or more manifest pages are missing"));
  }
  if (entryCount() != m_expectedEntryCount) {
    return reassemblyFailure(
        ManifestPageError::EntryCountMismatch, QStringLiteral("manifest entry count does not match the offer")
    );
  }
  QString digestError;
  const QByteArray digest = ManifestPageCodec::canonicalSha256(m_entries, &digestError);
  if (digest.isEmpty() || digest != m_expectedCanonicalSha256) {
    return reassemblyFailure(
        ManifestPageError::DigestMismatch,
        digestError.isEmpty() ? QStringLiteral("manifest digest does not match the offer") : digestError
    );
  }
  m_finished = true;
  return {.entries = std::move(m_entries)};
}

ManifestReassemblyResult ManifestPageReassembler::finish(const ManifestComplete &message)
{
  if (message.transferId != m_expectedTransferId) {
    return reassemblyFailure(
        ManifestPageError::TransferMismatch, QStringLiteral("manifest complete transferId does not match the offer")
    );
  }
  if (message.canonicalSha256 != m_expectedCanonicalSha256) {
    return reassemblyFailure(
        ManifestPageError::DigestMismatch, QStringLiteral("manifest complete digest does not match the offer")
    );
  }
  return finish();
}

} // namespace relaydesk::transfer
