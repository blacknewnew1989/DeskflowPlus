// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace relaydesk::transfer {

inline constexpr quint64 kTransferRecoverySchemaVersion = 1;
inline constexpr quint64 kDefaultMaximumRecoveryStateBytes = 64U * 1024U * 1024U;
inline constexpr quint64 kDefaultMaximumRecoveryEntries = 100'000;

struct RecoverySource
{
  QString canonicalPath;
  QString relativeProtocolPath;
  ManifestEntryType type = ManifestEntryType::File;

  [[nodiscard]] bool operator==(const RecoverySource &) const = default;
};

struct ManifestPagePlanBinding
{
  quint64 entryCount = 0;
  quint64 pageCount = 0;
  quint64 totalMetadataBytes = 0;

  [[nodiscard]] bool operator==(const ManifestPagePlanBinding &) const = default;
};

struct RecoveryProgress
{
  quint64 completedBytes = 0;
  quint64 completedFiles = 0;
  quint64 currentEntry = 0;

  [[nodiscard]] bool operator==(const RecoveryProgress &) const = default;
};

struct OutgoingRecoveryState
{
  TransferId transferId;
  deskflow::relaydesk::DeviceId localDeviceId;
  deskflow::relaydesk::DeviceId peerDeviceId;
  QByteArray peerFingerprintSha256;
  QDateTime createdUtc;
  SendOptions sendOptions;
  QList<RecoverySource> sourceRoots;
  QList<PreparedManifestEntry> entries;
  TransferManifestSummary summary;
  ManifestPagePlanBinding pagePlan;
  ConflictPolicy effectiveConflictPolicy = ConflictPolicy::AutoRename;
  RecoveryProgress progress;

  [[nodiscard]] bool operator==(const OutgoingRecoveryState &) const = default;
};

struct IncomingRecoveryState
{
  TransferId transferId;
  deskflow::relaydesk::DeviceId localDeviceId;
  deskflow::relaydesk::DeviceId peerDeviceId;
  QByteArray peerFingerprintSha256;
  QString peerDisplayName;
  TransferOffer offer;
  ReceiveOptions receiveOptions;
  QList<ManifestEntry> entries;
  ManifestPagePlanBinding pagePlan;
  NegotiatedCapabilities negotiatedCapabilities;

  [[nodiscard]] bool operator==(const IncomingRecoveryState &) const = default;
};

enum class TransferRecoveryStoreError
{
  None,
  InvalidStoreDirectory,
  DirectoryCreateFailed,
  InvalidState,
  InvalidPath,
  TooManyEntries,
  StateTooLarge,
  OpenFailed,
  ReadFailed,
  WriteFailed,
  CommitFailed,
  NotFound,
  MalformedCbor,
  UnsupportedSchema,
  InvalidFields,
  TransferIdMismatch,
  RemoveFailed,
};

struct TransferRecoveryStoreOperationResult
{
  TransferRecoveryStoreError error = TransferRecoveryStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TransferRecoveryStoreError::None;
  }
};

template <typename State> struct TransferRecoveryStoreLoadResult
{
  std::optional<State> state;
  TransferRecoveryStoreError error = TransferRecoveryStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return state.has_value() && error == TransferRecoveryStoreError::None;
  }
};

struct TransferRecoveryStoreScanIssue
{
  QString path;
  TransferRecoveryStoreError error = TransferRecoveryStoreError::None;
  QString diagnostic;
};

template <typename State> struct TransferRecoveryStoreScanResult
{
  QList<State> states;
  QList<TransferRecoveryStoreScanIssue> issues;
  TransferRecoveryStoreError error = TransferRecoveryStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TransferRecoveryStoreError::None;
  }
};

struct TransferRecoveryStoreLimits
{
  quint64 maximumEncodedBytes = kDefaultMaximumRecoveryStateBytes;
  quint64 maximumEntries = kDefaultMaximumRecoveryEntries;
  PathLimits pathLimits;
};

class TransferRecoveryStore final
{
public:
  explicit TransferRecoveryStore(QString rootDirectory, TransferRecoveryStoreLimits limits = {});

  [[nodiscard]] TransferRecoveryStoreOperationResult saveOutgoing(const OutgoingRecoveryState &state) const;
  [[nodiscard]] TransferRecoveryStoreLoadResult<OutgoingRecoveryState> loadOutgoing(const TransferId &transferId) const;
  [[nodiscard]] TransferRecoveryStoreScanResult<OutgoingRecoveryState> scanOutgoing() const;
  [[nodiscard]] TransferRecoveryStoreOperationResult removeOutgoing(const TransferId &transferId) const;

  [[nodiscard]] TransferRecoveryStoreOperationResult saveIncoming(const IncomingRecoveryState &state) const;
  [[nodiscard]] TransferRecoveryStoreLoadResult<IncomingRecoveryState> loadIncoming(const TransferId &transferId) const;
  [[nodiscard]] TransferRecoveryStoreScanResult<IncomingRecoveryState> scanIncoming() const;
  [[nodiscard]] TransferRecoveryStoreOperationResult removeIncoming(const TransferId &transferId) const;

  [[nodiscard]] QString outgoingStatePath(const TransferId &transferId) const;
  [[nodiscard]] QString incomingStatePath(const TransferId &transferId) const;

private:
  QString m_rootDirectory;
  TransferRecoveryStoreLimits m_limits;
};

} // namespace relaydesk::transfer
