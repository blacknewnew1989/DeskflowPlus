// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace relaydesk::transfer {

enum class ManifestEntryType : quint8
{
  File = 0,
  Directory = 1,
};

struct ManifestEntry
{
  FileId id;
  QString relativeProtocolPath;
  ManifestEntryType type = ManifestEntryType::File;
  quint64 size = 0;
  QDateTime modifiedUtc;
  QByteArray sha256;
  quint32 flags = 0;

  [[nodiscard]] bool operator==(const ManifestEntry &) const = default;
};

struct TransferManifestSummary
{
  TransferId id;
  QString displayName;
  quint64 totalBytes = 0;
  quint64 fileCount = 0;
  quint64 directoryCount = 0;
  // SHA-256 over deterministic CBOR of the canonical manifest-entry array.
  // The entry map uses wire keys 1..7 and SortKeysInMaps. Local paths,
  // displayName, and transferId are deliberately excluded.
  QByteArray canonicalSha256;

  [[nodiscard]] bool operator==(const TransferManifestSummary &) const = default;
};

struct SingleFileManifest
{
  QString canonicalSourcePath;
  QString protocolCollisionKey;
  ManifestEntry entry;
  TransferManifestSummary summary;

  [[nodiscard]] bool operator==(const SingleFileManifest &) const = default;
};

} // namespace relaydesk::transfer
