// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace relaydesk::transfer {

inline constexpr quint64 kLegacyResumeStateSchemaVersion = 1;
inline constexpr quint64 kResumeStateSchemaVersion = 2;
inline constexpr quint64 kDefaultMaximumResumeStateBytes = 64U * 1024U * 1024U;
inline constexpr quint64 kDefaultMaximumResumeFiles = 100'000;

enum class ResumeDirection
{
  Sending,
  Receiving,
};

struct ResumeFileState
{
  FileId fileId;
  QString relativeProtocolPath;
  quint64 durableOffset = 0;
  quint64 totalBytes = 0;
  QString partRelativePath;

  [[nodiscard]] bool operator==(const ResumeFileState &) const = default;
};

struct ResolvedTargetState
{
  FileId fileId;
  QString relativeTargetPath;
  quint64 size = 0;
  QByteArray sha256;
  IncomingConflictDecision decision = IncomingConflictDecision::Overwrite;

  [[nodiscard]] bool operator==(const ResolvedTargetState &) const = default;
};

struct ResumeState
{
  TransferId transferId;
  deskflow::relaydesk::DeviceId peerDeviceId;
  QByteArray manifestSha256;
  ResumeDirection direction = ResumeDirection::Receiving;
  QList<ResumeFileState> files;
  QList<ResolvedTargetState> resolvedTargets;
  QDateTime updatedUtc;

  [[nodiscard]] bool operator==(const ResumeState &) const = default;
};

struct ResumeStoreLimits
{
  quint64 maximumEncodedBytes = kDefaultMaximumResumeStateBytes;
  quint64 maximumFiles = kDefaultMaximumResumeFiles;
  PathLimits pathLimits;
};

enum class ResumeStoreError
{
  None,
  InvalidStoreDirectory,
  DirectoryCreateFailed,
  InvalidState,
  InvalidPath,
  TooManyFiles,
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

struct ResumeStoreOperationResult
{
  ResumeStoreError error = ResumeStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ResumeStoreError::None;
  }
};

struct ResumeStoreLoadResult
{
  std::optional<ResumeState> state;
  quint64 schemaVersion = 0;
  ResumeStoreError error = ResumeStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return state.has_value() && error == ResumeStoreError::None;
  }
};

struct ResumeStoreScanIssue
{
  QString path;
  ResumeStoreError error = ResumeStoreError::None;
  QString diagnostic;
};

struct ResumeStoreScanResult
{
  QList<ResumeState> states;
  QList<ResumeStoreScanIssue> issues;
  ResumeStoreError error = ResumeStoreError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ResumeStoreError::None;
  }
};

class ResumeStore final
{
public:
  explicit ResumeStore(QString activeDirectory, ResumeStoreLimits limits = {});

  [[nodiscard]] ResumeStoreOperationResult save(const ResumeState &state) const;
  [[nodiscard]] ResumeStoreLoadResult load(const TransferId &transferId) const;
  [[nodiscard]] ResumeStoreScanResult scan() const;
  [[nodiscard]] ResumeStoreOperationResult remove(const TransferId &transferId) const;

  [[nodiscard]] QString statePath(const TransferId &transferId) const;

private:
  QString m_activeDirectory;
  ResumeStoreLimits m_limits;
};

} // namespace relaydesk::transfer
