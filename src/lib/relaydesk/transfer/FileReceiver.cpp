// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileReceiver.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr quint32 kMaximumChunkBytes = 4U * 1024U * 1024U;
constexpr quint32 kResumeHashChunkBytes = 1U * 1024U * 1024U;
constexpr int kMaximumAutoRenameAttempts = 10'000;

FileReceiverResult failure(FileReceiverError error, QString diagnostic, PathError pathError = PathError::None)
{
  return {.error = error, .pathError = pathError, .diagnostic = std::move(diagnostic)};
}

DurableCheckpointResult checkpointFailure(DurableCheckpointError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool syncToStableStorage(QFile &file)
{
  const int descriptor = file.handle();
  if (descriptor < 0) {
    return false;
  }
#ifdef Q_OS_WIN
  return ::_commit(descriptor) == 0;
#else
  return ::fsync(descriptor) == 0;
#endif
}

QString autoRenameCandidate(const QString &requestedTarget, int index)
{
  if (index == 0) {
    return requestedTarget;
  }
  const QFileInfo info(requestedTarget);
  const QString fileName = info.fileName();
  const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
  const bool hasExtension = dot > 0 && dot + 1 < fileName.size();
  const QString stem = hasExtension ? fileName.first(dot) : fileName;
  const QString suffix = hasExtension ? fileName.sliced(dot) : QString{};
  return QDir(info.absolutePath()).filePath(QStringLiteral("%1 (%2)%3").arg(stem).arg(index).arg(suffix));
}

} // namespace

FileReceiver::FileReceiver() = default;

FileReceiver::~FileReceiver()
{
  if (m_partFile.isOpen()) {
    m_partFile.close();
  }
}

FileReceiverResult FileReceiver::begin(const FileReceiveRequest &request)
{
  if (request.begin.startOffset != 0) {
    return failure(
        FileReceiverError::ResumeStateMismatch,
        QStringLiteral("a non-zero FILE_BEGIN offset requires explicit validated resume state")
    );
  }
  return beginInternal(request, nullptr);
}

FileReceiverResult FileReceiver::resume(const FileReceiveRequest &request, const ResumeState &state)
{
  return beginInternal(request, &state);
}

FileReceiverResult FileReceiver::beginInternal(const FileReceiveRequest &request, const ResumeState *resumeState)
{
  if (m_snapshot.state != FileReceiverState::Idle) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not idle"));
  }
  if (request.conflictPolicy != ConflictPolicy::AutoRename) {
    return failure(
        FileReceiverError::UnsupportedConflictPolicy,
        QStringLiteral("this receiver slice supports only the default auto-rename policy")
    );
  }
  if (request.entry.type != ManifestEntryType::File || request.begin.fileId != request.entry.id ||
      request.begin.size != request.entry.size ||
      request.begin.size > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
      request.begin.startOffset > request.begin.size || request.begin.chunkBytes == 0 ||
      request.begin.chunkBytes > kMaximumChunkBytes || request.entry.sha256.size() != kSha256Bytes ||
      request.begin.expectedSha256 != request.entry.sha256) {
    return failure(
        FileReceiverError::InvalidRequest, QStringLiteral("FILE_BEGIN does not match the accepted manifest entry")
    );
  }

  QString requestedTarget;
  const auto targetPath = PathPolicy::joinLexicallyUnderRoot(
      request.receiveRoot, request.entry.relativeProtocolPath, requestedTarget, request.pathLimits
  );
  if (!targetPath.ok) {
    return failure(FileReceiverError::UnsafePath, targetPath.diagnostic, targetPath.error);
  }

  const QString stagingRelative = QStringLiteral(".incoming/%1/%2.part")
                                      .arg(
                                          request.begin.transferId.toString(), request.begin.fileId.toString()
                                      );
  QString partPath;
  const auto stagingPath =
      PathPolicy::joinLexicallyUnderRoot(request.receiveRoot, stagingRelative, partPath, request.pathLimits);
  if (!stagingPath.ok) {
    return failure(FileReceiverError::UnsafePath, stagingPath.diagnostic, stagingPath.error);
  }
  if (resumeState == nullptr && QFileInfo::exists(partPath)) {
    return failure(
        FileReceiverError::StagingExists,
        QStringLiteral("a staging file already exists; resume state is required before it can be reused")
    );
  }
  if (!QDir().mkpath(QFileInfo(partPath).absolutePath())) {
    return failure(FileReceiverError::DirectoryCreateFailed, QStringLiteral("could not create the staging directory"));
  }

  m_hash.reset();
  m_partFile.setFileName(partPath);
  if (resumeState == nullptr) {
    if (!m_partFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      return failure(
          FileReceiverError::StagingOpenFailed, QStringLiteral("could not exclusively create the staging file")
      );
    }
  } else {
    const ResumeFileState *matching = nullptr;
    for (const auto &file : resumeState->files) {
      if (file.fileId == request.begin.fileId) {
        if (matching != nullptr) {
          return failure(
              FileReceiverError::ResumeStateMismatch, QStringLiteral("resume state contains duplicate file IDs")
          );
        }
        matching = &file;
      }
    }
    if (resumeState->transferId != request.begin.transferId || resumeState->peerDeviceId.value().isNull() ||
        resumeState->direction != ResumeDirection::Receiving || resumeState->manifestSha256.size() != kSha256Bytes ||
        request.manifestSha256.size() != kSha256Bytes || resumeState->manifestSha256 != request.manifestSha256 ||
        !resumeState->updatedUtc.isValid() || resumeState->updatedUtc.toMSecsSinceEpoch() <= 0 || matching == nullptr ||
        matching->relativeProtocolPath != targetPath.normalized || matching->totalBytes != request.entry.size ||
        matching->durableOffset != request.begin.startOffset || matching->partRelativePath != stagingRelative) {
      return failure(
          FileReceiverError::ResumeStateMismatch,
          QStringLiteral("resume state does not match the accepted manifest and FILE_BEGIN")
      );
    }
    QFileInfo partInfo(partPath);
    partInfo.refresh();
    if (!partInfo.exists() || partInfo.isSymLink() || !partInfo.isFile() || partInfo.size() < 0 ||
        static_cast<quint64>(partInfo.size()) != request.begin.startOffset) {
      return failure(
          FileReceiverError::StagingSizeMismatch,
          QStringLiteral("staging file size does not match the durable resume offset")
      );
    }
    if (!m_partFile.open(QIODevice::ReadWrite)) {
      return failure(FileReceiverError::StagingOpenFailed, QStringLiteral("could not reopen the staging file"));
    }
    quint64 remaining = request.begin.startOffset;
    while (remaining > 0) {
      const quint64 wanted = std::min<quint64>(remaining, kResumeHashChunkBytes);
      const QByteArray prefix = m_partFile.read(static_cast<qint64>(wanted));
      if (static_cast<quint64>(prefix.size()) != wanted) {
        m_partFile.close();
        return failure(
            FileReceiverError::StagingReadFailed, QStringLiteral("could not rehash the complete staging prefix")
        );
      }
      m_hash.addData(QByteArrayView(prefix));
      remaining -= wanted;
    }
    if (!m_partFile.seek(static_cast<qint64>(request.begin.startOffset))) {
      m_partFile.close();
      return failure(FileReceiverError::StagingReadFailed, QStringLiteral("could not seek to the resume offset"));
    }
  }

  m_expectedSha256 = request.entry.sha256;
  m_chunkBytes = request.begin.chunkBytes;
  m_requestedTarget = std::move(requestedTarget);
  m_conflictPolicy = request.conflictPolicy;
  m_snapshot = FileReceiverSnapshot{
      .state = FileReceiverState::Receiving,
      .transferId = request.begin.transferId,
      .fileId = request.begin.fileId,
      .expectedSize = request.begin.size,
      .receivedBytes = request.begin.startOffset,
      .nextSequence = 0,
      .relativeProtocolPath = targetPath.normalized,
      .partPath = std::move(partPath),
      .partRelativePath = stagingRelative,
  };
  return {};
}

FileReceiverResult FileReceiver::append(const FileChunkMessage &chunk, QByteArrayView payload)
{
  if (m_snapshot.state != FileReceiverState::Receiving) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not accepting chunks"));
  }
  if (!m_snapshot.transferId.has_value() || chunk.transferId != *m_snapshot.transferId) {
    return fail(FileReceiverError::TransferIdMismatch, FileResultCode::IoError, QStringLiteral("TRANSFER_ID_MISMATCH"));
  }
  if (!m_snapshot.fileId.has_value() || chunk.fileId != *m_snapshot.fileId) {
    return fail(FileReceiverError::FileIdMismatch, FileResultCode::IoError, QStringLiteral("FILE_ID_MISMATCH"));
  }
  if (chunk.offset != m_snapshot.receivedBytes) {
    return fail(FileReceiverError::OffsetMismatch, FileResultCode::IoError, QStringLiteral("OFFSET_MISMATCH"));
  }
  if (chunk.sequence != m_snapshot.nextSequence) {
    return fail(FileReceiverError::SequenceMismatch, FileResultCode::IoError, QStringLiteral("SEQUENCE_MISMATCH"));
  }
  if (payload.isEmpty()) {
    return fail(FileReceiverError::EmptyChunk, FileResultCode::IoError, QStringLiteral("EMPTY_FILE_CHUNK"));
  }
  if (payload.size() > m_chunkBytes) {
    return fail(FileReceiverError::ChunkTooLarge, FileResultCode::IoError, QStringLiteral("FILE_CHUNK_TOO_LARGE"));
  }
  const quint64 payloadBytes = static_cast<quint64>(payload.size());
  if (payloadBytes > m_snapshot.expectedSize - m_snapshot.receivedBytes) {
    return fail(FileReceiverError::SizeOverflow, FileResultCode::SizeMismatch, QStringLiteral("FILE_SIZE_OVERFLOW"));
  }

  const qint64 written = m_partFile.write(payload.data(), payload.size());
  if (written != payload.size()) {
    const auto code =
        m_partFile.error() == QFileDevice::ResourceError ? FileResultCode::DiskFull : FileResultCode::IoError;
    return fail(FileReceiverError::WriteFailed, code, QStringLiteral("STAGING_WRITE_FAILED"));
  }
  m_hash.addData(payload);
  m_snapshot.receivedBytes += payloadBytes;
  ++m_snapshot.nextSequence;
  return {};
}

FileReceiverResult FileReceiver::finish(const FileEndMessage &end)
{
  const auto staged = finishStaging(end);
  if (!staged.ok()) {
    return staged;
  }
  if (!QDir().mkpath(QFileInfo(m_requestedTarget).absolutePath())) {
    return failCommit(
        FileReceiverError::DirectoryCreateFailed, FileResultCode::IoError,
        QStringLiteral("TARGET_DIRECTORY_CREATE_FAILED")
    );
  }

  const QString committedPath = chooseAutoRenameTarget(m_requestedTarget);
  if (committedPath.isEmpty()) {
    return failCommit(
        FileReceiverError::TargetExists, FileResultCode::TargetExists,
        QStringLiteral("TARGET_NAME_UNAVAILABLE")
    );
  }
  if (!QFile::rename(m_snapshot.partPath, committedPath)) {
    return failCommit(
        FileReceiverError::CommitFailed, FileResultCode::IoError,
        QStringLiteral("ATOMIC_COMMIT_FAILED")
    );
  }
  return confirmCommitted(committedPath);
}

FileReceiverResult FileReceiver::finishStaging(const FileEndMessage &end)
{
  if (m_snapshot.state != FileReceiverState::Receiving) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not awaiting FILE_END"));
  }
  if (!m_snapshot.transferId.has_value() || end.transferId != *m_snapshot.transferId) {
    return fail(FileReceiverError::TransferIdMismatch, FileResultCode::IoError, QStringLiteral("TRANSFER_ID_MISMATCH"));
  }
  if (!m_snapshot.fileId.has_value() || end.fileId != *m_snapshot.fileId) {
    return fail(FileReceiverError::FileIdMismatch, FileResultCode::IoError, QStringLiteral("FILE_ID_MISMATCH"));
  }
  if (end.size != m_snapshot.expectedSize || m_snapshot.receivedBytes != m_snapshot.expectedSize) {
    return fail(FileReceiverError::SizeMismatch, FileResultCode::SizeMismatch, QStringLiteral("FILE_SIZE_MISMATCH"));
  }
  if (end.sha256 != m_expectedSha256) {
    return fail(FileReceiverError::HashMismatch, FileResultCode::HashMismatch, QStringLiteral("FILE_HASH_MISMATCH"));
  }
  if (!m_partFile.flush()) {
    return fail(FileReceiverError::WriteFailed, FileResultCode::IoError, QStringLiteral("STAGING_FLUSH_FAILED"));
  }
  m_partFile.close();
  if (m_hash.result() != m_expectedSha256) {
    return fail(FileReceiverError::HashMismatch, FileResultCode::HashMismatch, QStringLiteral("FILE_HASH_MISMATCH"));
  }
  return {};
}

FileReceiverResult FileReceiver::confirmCommitted(QString committedPath)
{
  if (m_snapshot.state != FileReceiverState::Receiving || committedPath.isEmpty() ||
      QFileInfo::exists(m_snapshot.partPath)) {
    return failCommit(
        FileReceiverError::CommitFailed, FileResultCode::IoError,
        QStringLiteral("ATOMIC_COMMIT_CONFIRMATION_FAILED")
    );
  }
  m_snapshot.committedPath = std::move(committedPath);
  m_snapshot.state = FileReceiverState::Completed;
  return result(FileResultCode::Ok);
}

FileReceiverResult FileReceiver::failCommit(
    FileReceiverError error, FileResultCode code, QString diagnostic
)
{
  return fail(error, code, std::move(diagnostic));
}

FileReceiverResult FileReceiver::cancel(PartialDisposition disposition)
{
  if (m_snapshot.state == FileReceiverState::Completed) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("a committed file cannot be cancelled"));
  }
  if (m_snapshot.state == FileReceiverState::Cancelled) {
    return m_snapshot.transferId.has_value() ? result(FileResultCode::Cancelled, QStringLiteral("CANCELLED"))
                                             : FileReceiverResult{};
  }
  if (m_partFile.isOpen()) {
    m_partFile.close();
  }
  if (disposition == PartialDisposition::Remove && !m_snapshot.partPath.isEmpty()) {
    QFile::remove(m_snapshot.partPath);
  }
  m_snapshot.state = FileReceiverState::Cancelled;
  return m_snapshot.transferId.has_value() ? result(FileResultCode::Cancelled, QStringLiteral("CANCELLED"))
                                           : FileReceiverResult{};
}

DurableCheckpointResult FileReceiver::checkpoint(const ResumeStore &store, ResumeState &state, QDateTime updatedUtc)
{
  if (m_snapshot.state != FileReceiverState::Receiving || !m_partFile.isOpen()) {
    return checkpointFailure(
        DurableCheckpointError::InvalidReceiverState, QStringLiteral("file receiver is not checkpointable")
    );
  }
  if (!m_snapshot.transferId.has_value() || state.transferId != *m_snapshot.transferId ||
      state.direction != ResumeDirection::Receiving ||
      state.manifestSha256.size() != kSha256Bytes) {
    return checkpointFailure(
        DurableCheckpointError::ResumeStateMismatch, QStringLiteral("resume state does not match the receiver")
    );
  }

  auto matching = state.files.end();
  for (auto iterator = state.files.begin(); iterator != state.files.end(); ++iterator) {
    if (m_snapshot.fileId.has_value() && iterator->fileId == *m_snapshot.fileId) {
      if (matching != state.files.end()) {
        return checkpointFailure(
            DurableCheckpointError::ResumeStateMismatch, QStringLiteral("resume state contains duplicate file IDs")
        );
      }
      matching = iterator;
    }
  }
  if (matching == state.files.end() || matching->relativeProtocolPath != m_snapshot.relativeProtocolPath ||
      matching->totalBytes != m_snapshot.expectedSize || matching->partRelativePath != m_snapshot.partRelativePath ||
      matching->durableOffset > m_snapshot.receivedBytes) {
    return checkpointFailure(
        DurableCheckpointError::ResumeStateMismatch, QStringLiteral("resume file state does not match the receiver")
    );
  }
  if (!updatedUtc.isValid() || updatedUtc.toMSecsSinceEpoch() <= 0) {
    return checkpointFailure(
        DurableCheckpointError::ResumeStateMismatch, QStringLiteral("checkpoint timestamp is invalid")
    );
  }

  if (!m_partFile.flush()) {
    return checkpointFailure(DurableCheckpointError::FlushFailed, QStringLiteral("could not flush the staging file"));
  }
  if (!syncToStableStorage(m_partFile)) {
    return checkpointFailure(
        DurableCheckpointError::SyncFailed, QStringLiteral("could not sync the staging file to stable storage")
    );
  }

  ResumeState candidate = state;
  for (auto &file : candidate.files) {
    if (m_snapshot.fileId.has_value() && file.fileId == *m_snapshot.fileId) {
      file.durableOffset = m_snapshot.receivedBytes;
      break;
    }
  }
  candidate.updatedUtc = updatedUtc.toUTC();
  const auto persisted = store.save(candidate);
  if (!persisted.ok()) {
    return checkpointFailure(
        DurableCheckpointError::PersistFailed,
        persisted.diagnostic.isEmpty() ? QStringLiteral("could not persist durable checkpoint") : persisted.diagnostic
    );
  }

  state = std::move(candidate);
  return {
      .message = FileCheckpointMessage{
          .transferId = *m_snapshot.transferId,
          .fileId = *m_snapshot.fileId,
          .durableOffset = m_snapshot.receivedBytes,
      },
  };
}

FileReceiverSnapshot FileReceiver::snapshot() const
{
  return m_snapshot;
}

FileReceiverResult FileReceiver::fail(FileReceiverError error, FileResultCode code, QString diagnostic)
{
  if (m_partFile.isOpen()) {
    m_partFile.close();
  }
  m_snapshot.state = FileReceiverState::Failed;
  auto failed = result(code, diagnostic);
  failed.error = error;
  failed.diagnostic = std::move(diagnostic);
  return failed;
}

FileReceiverResult FileReceiver::result(FileResultCode code, QString diagnostic) const
{
  FileReceiverResult receiverResult;
  if (m_snapshot.transferId.has_value() && m_snapshot.fileId.has_value()) {
    receiverResult.fileResult = FileResultMessage{
        .transferId = *m_snapshot.transferId,
        .fileId = *m_snapshot.fileId,
        .code = code,
        .diagnostic = std::move(diagnostic),
    };
  }
  return receiverResult;
}

QString FileReceiver::chooseAutoRenameTarget(const QString &requestedTarget) const
{
  if (m_conflictPolicy != ConflictPolicy::AutoRename) {
    return {};
  }
  for (int index = 0; index < kMaximumAutoRenameAttempts; ++index) {
    const QString candidate = autoRenameCandidate(requestedTarget, index);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

void FileReceiver::resetSession()
{
  m_snapshot = {};
  m_expectedSha256.clear();
  m_chunkBytes = 0;
  m_requestedTarget.clear();
  m_conflictPolicy = ConflictPolicy::AutoRename;
  m_hash.reset();
}

} // namespace relaydesk::transfer
