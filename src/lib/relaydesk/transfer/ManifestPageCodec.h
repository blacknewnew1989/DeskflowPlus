// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace relaydesk::transfer {

inline constexpr quint64 kDefaultManifestPageMetadataBytes = 1U * 1024U * 1024U;
inline constexpr quint64 kMaxManifestPageMetadataBytes = 1U * 1024U * 1024U;
inline constexpr quint64 kDefaultManifestEntriesPerPage = 4'096;
inline constexpr quint64 kMaxManifestEntriesPerPage = 100'000;
inline constexpr quint64 kDefaultManifestPageCount = 100'000;
inline constexpr quint64 kMaxManifestPageCount = 100'000;
inline constexpr quint64 kDefaultPagedManifestEntries = 100'000;
inline constexpr quint64 kMaxPagedManifestEntries = 100'000;
inline constexpr quint64 kDefaultPagedManifestMetadataBytes = 64U * 1024U * 1024U;
inline constexpr quint64 kMaxPagedManifestMetadataBytes = 64U * 1024U * 1024U;

struct ManifestPagingLimits
{
  quint64 maxPageMetadataBytes = kDefaultManifestPageMetadataBytes;
  quint64 maxEntriesPerPage = kDefaultManifestEntriesPerPage;
  quint64 maxPages = kDefaultManifestPageCount;
  quint64 maxEntries = kDefaultPagedManifestEntries;
  quint64 maxManifestMetadataBytes = kDefaultPagedManifestMetadataBytes;
  PathLimits pathLimits;
};

struct ManifestPage
{
  TransferId transferId;
  quint64 pageIndex = 0;
  quint64 pageCount = 0;
  QList<ManifestEntry> entries;

  [[nodiscard]] bool operator==(const ManifestPage &) const = default;
};

struct ManifestComplete
{
  TransferId transferId;
  QByteArray canonicalSha256;

  [[nodiscard]] bool operator==(const ManifestComplete &) const = default;
};

struct ManifestPageRange
{
  qsizetype firstEntry = 0;
  qsizetype entryCount = 0;

  [[nodiscard]] bool operator==(const ManifestPageRange &) const = default;
};

struct ManifestPagePlan
{
  TransferId transferId;
  QList<ManifestPageRange> ranges;
  quint64 entryCount = 0;
  quint64 totalMetadataBytes = 0;

  [[nodiscard]] quint64 pageCount() const noexcept
  {
    return static_cast<quint64>(ranges.size());
  }
};

enum class ManifestPageError
{
  None,
  UnsupportedVersion,
  InvalidLimits,
  EmptyManifest,
  TooManyEntries,
  TooManyPages,
  EntryTooLarge,
  PageMetadataTooLarge,
  ManifestMetadataTooLarge,
  InvalidManifestOrder,
  InvalidManifestEntry,
  MalformedCbor,
  MetadataNotMap,
  NonIntegerKey,
  MissingField,
  InvalidFieldType,
  InvalidFieldValue,
  TransferMismatch,
  PageCountMismatch,
  DuplicatePage,
  OutOfOrderPage,
  MissingPage,
  EntryCountMismatch,
  DigestMismatch,
  ProtocolPathCollision,
  DuplicateFileId,
  AlreadyComplete,
};

struct ManifestPagePlanResult
{
  std::optional<ManifestPagePlan> plan;
  ManifestPageError error = ManifestPageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return plan.has_value() && error == ManifestPageError::None;
  }
};

struct ManifestPageDecodeResult
{
  std::optional<ManifestPage> page;
  ManifestPageError error = ManifestPageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return page.has_value() && error == ManifestPageError::None;
  }
};

struct ManifestCompleteDecodeResult
{
  std::optional<ManifestComplete> message;
  ManifestPageError error = ManifestPageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == ManifestPageError::None;
  }
};

struct ManifestReassemblyResult
{
  std::optional<QList<ManifestEntry>> entries;
  ManifestPageError error = ManifestPageError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return entries.has_value() && error == ManifestPageError::None;
  }
};

class ManifestPageCodec final
{
public:
  [[nodiscard]] static ManifestPagePlanResult
  plan(const TransferManifest &manifest, const ManifestPagingLimits &limits = {});

  [[nodiscard]] static QByteArray encodePage(
      const TransferManifest &manifest, const ManifestPagePlan &plan, quint64 pageIndex,
      const ManifestPagingLimits &limits = {}, QString *error = nullptr
  );

  [[nodiscard]] static QByteArray
  encode(const ManifestPage &page, const ManifestPagingLimits &limits = {}, QString *error = nullptr);

  [[nodiscard]] static ManifestPageDecodeResult
  decode(quint16 protocolVersion, const QByteArray &metadata, const ManifestPagingLimits &limits = {});

  [[nodiscard]] static QByteArray encodeComplete(const ManifestComplete &message, QString *error = nullptr);
  [[nodiscard]] static ManifestCompleteDecodeResult decodeComplete(quint16 protocolVersion, const QByteArray &metadata);

  // Incremental SHA-256 over the deterministic canonical CBOR entry array.
  // No complete encoded manifest buffer is constructed.
  [[nodiscard]] static QByteArray canonicalSha256(const QList<ManifestEntry> &entries, QString *error = nullptr);
  [[nodiscard]] static QByteArray
  canonicalSha256(const QList<PreparedManifestEntry> &entries, QString *error = nullptr);
  [[nodiscard]] static quint64 canonicalEntrySize(const ManifestEntry &entry, QString *error = nullptr);
};

class ManifestPageReassembler final
{
public:
  ManifestPageReassembler(
      TransferId expectedTransferId, quint64 expectedPageCount, quint64 expectedEntryCount,
      QByteArray expectedCanonicalSha256, ManifestPagingLimits limits = {}
  );

  [[nodiscard]] ManifestPageError
  addEncodedPage(quint16 protocolVersion, const QByteArray &metadata, QString *diagnostic = nullptr);
  [[nodiscard]] ManifestPageError addPage(const ManifestPage &page, QString *diagnostic = nullptr);
  [[nodiscard]] ManifestReassemblyResult finish();
  [[nodiscard]] ManifestReassemblyResult finish(const ManifestComplete &message);

  [[nodiscard]] quint64 nextPageIndex() const noexcept
  {
    return m_nextPageIndex;
  }

  [[nodiscard]] quint64 entryCount() const noexcept
  {
    return static_cast<quint64>(m_entries.size());
  }

private:
  [[nodiscard]] ManifestPageError
  addPageWithEncodedBytes(const ManifestPage &page, quint64 encodedBytes, QString *diagnostic);

  TransferId m_expectedTransferId;
  quint64 m_expectedPageCount = 0;
  quint64 m_expectedEntryCount = 0;
  QByteArray m_expectedCanonicalSha256;
  ManifestPagingLimits m_limits;
  QList<ManifestEntry> m_entries;
  QSet<QString> m_collisionKeys;
  QSet<QByteArray> m_fileIds;
  quint64 m_nextPageIndex = 0;
  quint64 m_receivedMetadataBytes = 0;
  bool m_finished = false;
};

} // namespace relaydesk::transfer
