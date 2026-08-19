// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
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

struct PreparedManifestEntry
{
  // Local-only snapshot used by the sender. It is deliberately excluded from
  // the canonical wire digest.
  QString canonicalSourcePath;
  QString protocolCollisionKey;
  ManifestEntry entry;

  [[nodiscard]] bool operator==(const PreparedManifestEntry &) const = default;
};

struct ManifestBuildWarning
{
  QString relativeProtocolPath;
  QString diagnostic;

  [[nodiscard]] bool operator==(const ManifestBuildWarning &) const = default;
};

struct TransferManifest
{
  // Sorted by UTF-8 protocol path, entry type, then fileId bytes.
  QList<PreparedManifestEntry> entries;
  QList<ManifestBuildWarning> warnings;
  TransferManifestSummary summary;

  [[nodiscard]] bool operator==(const TransferManifest &) const = default;
};

enum class TransferStartError : quint32
{
  None = 0,
  WrongThread = 1,
  InvalidRequest = 2,
  NotRunning = 3,
  PeerUnavailable = 4,
};

struct TransferStartResult
{
  std::optional<TransferId> transferId;
  TransferStartError error = TransferStartError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return transferId.has_value() && error == TransferStartError::None;
  }

  [[nodiscard]] bool operator==(const TransferStartResult &) const = default;
};

enum class TransferOperation : quint8
{
  Accept = 1,
  Reject = 2,
  Pause = 3,
  Resume = 4,
  Cancel = 5,
  Retry = 6,
};

enum class TransferOperationOutcome : quint8
{
  Applied = 0,
  Idempotent = 1,
  Rejected = 2,
};

enum class TransferOperationError : quint32
{
  None = 0,
  UnknownTransfer = 1,
  UnsupportedOperation = 2,
  InvalidState = 3,
  StartFailed = 4,
  TransportFailed = 5,
};

struct TransferOperationResult
{
  TransferId transferId;
  TransferOperation operation = TransferOperation::Pause;
  TransferOperationOutcome outcome = TransferOperationOutcome::Rejected;
  TransferOperationError error = TransferOperationError::InvalidState;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return outcome != TransferOperationOutcome::Rejected && error == TransferOperationError::None;
  }

  [[nodiscard]] bool changed() const noexcept
  {
    return outcome == TransferOperationOutcome::Applied && error == TransferOperationError::None;
  }

  [[nodiscard]] bool operator==(const TransferOperationResult &) const = default;
};

struct SendOptions
{
  ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;

  [[nodiscard]] bool operator==(const SendOptions &) const = default;
};

enum class AcceptanceOrigin : quint8
{
  UserDecision = 0,
  TrustedDevicePolicy = 1,
};

enum class PartialDisposition : quint8
{
  Keep = 0,
  Remove = 1,
};

struct ReceiveOptions
{
  QString destinationRoot;
  ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;
  PartialDisposition failurePartialDisposition = PartialDisposition::Keep;
  AcceptanceOrigin acceptanceOrigin = AcceptanceOrigin::UserDecision;

  [[nodiscard]] bool operator==(const ReceiveOptions &) const = default;
};

struct TransferCancelOptions
{
  TransferCancelReason reason = TransferCancelReason::UserRequested;
  PartialDisposition partialDisposition = PartialDisposition::Keep;

  [[nodiscard]] bool operator==(const TransferCancelOptions &) const = default;
};

struct IncomingOffer
{
  deskflow::relaydesk::DeviceId peerDeviceId;
  QString peerDisplayName;
  // Preserve the validated wire offer consumed by TransferOfferStateMachine;
  // UI callers must not rebuild protocol fields from a display-only summary.
  TransferOffer offer;
  bool peerTrusted = false;
  bool mayAutoAccept = false;

  [[nodiscard]] bool operator==(const IncomingOffer &) const = default;
};

enum class IncomingConflictDecision : quint8
{
  Overwrite = 0,
  AutoRename = 1,
  Skip = 2,
  CancelTransfer = 3,
};

// This is deliberately a protocol-relative path. The receiver's destination
// remains private to the receiving device and must never cross the service API.
struct IncomingConflictPrompt
{
  TransferId transferId;
  QUuid conflictId;
  QString relativeProtocolPath;
  bool existingIsDirectory = false;

  [[nodiscard]] bool operator==(const IncomingConflictPrompt &) const = default;
};

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::SendOptions)
Q_DECLARE_METATYPE(relaydesk::transfer::ReceiveOptions)
Q_DECLARE_METATYPE(relaydesk::transfer::PartialDisposition)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferCancelOptions)
Q_DECLARE_METATYPE(relaydesk::transfer::AcceptanceOrigin)
Q_DECLARE_METATYPE(relaydesk::transfer::IncomingOffer)
Q_DECLARE_METATYPE(relaydesk::transfer::IncomingConflictDecision)
Q_DECLARE_METATYPE(relaydesk::transfer::IncomingConflictPrompt)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferStartError)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferStartResult)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferOperation)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferOperationOutcome)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferOperationError)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferOperationResult)
