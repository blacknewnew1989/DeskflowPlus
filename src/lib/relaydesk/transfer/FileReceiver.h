// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/ResumeStore.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <optional>

namespace relaydesk::transfer {

enum class FileReceiverState
{
  Idle,
  Receiving,
  Completed,
  Failed,
  Cancelled,
};

enum class FileReceiverError
{
  None,
  InvalidState,
  InvalidRequest,
  UnsafePath,
  UnsupportedConflictPolicy,
  StagingExists,
  DirectoryCreateFailed,
  StagingOpenFailed,
  TransferIdMismatch,
  FileIdMismatch,
  OffsetMismatch,
  SequenceMismatch,
  EmptyChunk,
  ChunkTooLarge,
  SizeOverflow,
  WriteFailed,
  SizeMismatch,
  HashMismatch,
  TargetExists,
  CommitFailed,
  ResumeStateMismatch,
  StagingSizeMismatch,
  StagingReadFailed,
};

struct FileReceiveRequest
{
  QString receiveRoot;
  ManifestEntry entry;
  FileBeginMessage begin;
  QByteArray manifestSha256;
  ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;
  PathLimits pathLimits;
};

struct FileReceiverResult
{
  FileReceiverError error = FileReceiverError::None;
  PathError pathError = PathError::None;
  QString diagnostic;
  std::optional<FileResultMessage> fileResult;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == FileReceiverError::None;
  }
};

struct FileReceiverSnapshot
{
  FileReceiverState state = FileReceiverState::Idle;
  TransferId transferId;
  FileId fileId;
  quint64 expectedSize = 0;
  quint64 receivedBytes = 0;
  quint64 nextSequence = 0;
  QString relativeProtocolPath;
  QString partPath;
  QString partRelativePath;
  QString committedPath;

  [[nodiscard]] bool operator==(const FileReceiverSnapshot &) const = default;
};

enum class DurableCheckpointError
{
  None,
  InvalidReceiverState,
  ResumeStateMismatch,
  FlushFailed,
  SyncFailed,
  PersistFailed,
};

struct DurableCheckpointResult
{
  std::optional<FileCheckpointMessage> message;
  DurableCheckpointError error = DurableCheckpointError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == DurableCheckpointError::None;
  }
};

class FileReceiver final
{
public:
  FileReceiver();
  ~FileReceiver();

  FileReceiver(const FileReceiver &) = delete;
  FileReceiver &operator=(const FileReceiver &) = delete;

  [[nodiscard]] FileReceiverResult begin(const FileReceiveRequest &request);
  [[nodiscard]] FileReceiverResult resume(const FileReceiveRequest &request, const ResumeState &state);
  [[nodiscard]] FileReceiverResult append(const FileChunkMessage &chunk, QByteArrayView payload);
  [[nodiscard]] FileReceiverResult finish(const FileEndMessage &end);
  [[nodiscard]] FileReceiverResult cancel(bool keepPartial);
  [[nodiscard]] DurableCheckpointResult
  checkpoint(const ResumeStore &store, ResumeState &state, QDateTime updatedUtc = QDateTime::currentDateTimeUtc());

  [[nodiscard]] FileReceiverSnapshot snapshot() const;

private:
  [[nodiscard]] FileReceiverResult fail(FileReceiverError error, FileResultCode code, QString diagnostic);
  [[nodiscard]] FileReceiverResult beginInternal(const FileReceiveRequest &request, const ResumeState *resumeState);
  [[nodiscard]] FileReceiverResult result(FileResultCode code, QString diagnostic = {}) const;
  [[nodiscard]] QString chooseAutoRenameTarget(const QString &requestedTarget) const;
  void resetSession();

  FileReceiverSnapshot m_snapshot;
  QFile m_partFile;
  QByteArray m_expectedSha256;
  quint32 m_chunkBytes = 0;
  QString m_requestedTarget;
  ConflictPolicy m_conflictPolicy = ConflictPolicy::AutoRename;
  QCryptographicHash m_hash{QCryptographicHash::Sha256};
};

} // namespace relaydesk::transfer
