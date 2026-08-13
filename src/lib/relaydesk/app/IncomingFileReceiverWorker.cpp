/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingFileReceiverWorker.h"

#include "relaydesk/transfer/PathPolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>

namespace deskflow::relaydesk {

namespace {

FileSafetyResult ensureSafeDirectoryPath(
    IPlatformFileSafety &fileSafety, const QString &receiveRoot, const QString &absoluteDirectory
)
{
  QDir root(receiveRoot);
  QString relative = root.relativeFilePath(absoluteDirectory);
  relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (relative == QStringLiteral(".")) {
    return {};
  }
  const auto path = ::relaydesk::transfer::PathPolicy::validateRelative(relative);
  if (!path.ok || path.normalized != relative) {
    return {
        .error = FileSafetyError::DestinationInvalid,
        .diagnostic = QStringLiteral("receive directory is not a normalized child of the receive root"),
    };
  }
  QString current = QDir::cleanPath(receiveRoot);
  for (const auto &component : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    current = QDir(current).absoluteFilePath(component);
    auto safe = fileSafety.verifyNoLinkTraversal(
        {.receiveRoot = receiveRoot, .candidatePath = current}
    );
    if (!safe.ok()) {
      return safe;
    }
    const QFileInfo existing(current);
    if (existing.exists()) {
      if (!existing.isDir()) {
        return {
            .error = FileSafetyError::DestinationInvalid,
            .diagnostic = QStringLiteral("receive path parent exists but is not a directory"),
        };
      }
    } else if (!QDir().mkdir(current)) {
      return {
          .error = FileSafetyError::DestinationInvalid,
          .diagnostic = QStringLiteral("could not create a verified receive directory"),
      };
    }
    safe = fileSafety.verifyNoLinkTraversal(
        {.receiveRoot = receiveRoot, .candidatePath = current}
    );
    if (!safe.ok()) {
      return safe;
    }
  }
  return {};
}

} // namespace

IncomingFileReceiverWorker::IncomingFileReceiverWorker(IPlatformFileSafety &fileSafety)
    : m_fileSafety(fileSafety), m_ownerThread(QThread::currentThread())
{
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::begin(
    const ::relaydesk::transfer::FileReceiveRequest &request
)
{
  return beginInternal(request, nullptr);
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::resume(
    const ::relaydesk::transfer::FileReceiveRequest &request,
    const ::relaydesk::transfer::ResumeState &state
)
{
  return beginInternal(request, &state);
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::beginInternal(
    const ::relaydesk::transfer::FileReceiveRequest &request,
    const ::relaydesk::transfer::ResumeState *resumeState
)
{
  using namespace ::relaydesk::transfer;

  if (!isOwningThread()) {
    return wrongThread();
  }
  const auto root = m_fileSafety.verifyReceiveRoot({.receiveRoot = request.receiveRoot});
  if (!root.ok()) {
    return safetyFailure(root);
  }

  m_disposition = IncomingFileDisposition::Receive;
  m_conflictRequest = {
      .targetRoot = request.receiveRoot,
      .relativeProtocolPath = request.entry.relativeProtocolPath,
      .policy = request.conflictPolicy,
      .pathLimits = request.pathLimits,
  };
  const auto decision = m_conflicts.resolve(m_conflictRequest);
  const auto *target = std::get_if<UseTarget>(&decision);
  if (target == nullptr) {
    if (std::holds_alternative<SkipTarget>(decision)) {
      m_disposition = IncomingFileDisposition::Skip;
      m_receiveRoot = request.receiveRoot;
      return {};
    }
    if (const auto *failure = std::get_if<ConflictFailure>(&decision)) {
      return {
          .error = failure->error == ConflictResolverError::UnsafePath
                       ? FileReceiverError::UnsafePath
                       : FileReceiverError::UnsupportedConflictPolicy,
          .pathError = failure->pathError,
          .diagnostic = failure->diagnostic,
      };
    }
    return {
        .error = FileReceiverError::UnsupportedConflictPolicy,
        .diagnostic = QStringLiteral("conflict policy requires a decision before receiving file bytes"),
    };
  }
  const auto targetDirectory = ensureSafeDirectoryPath(
      m_fileSafety, request.receiveRoot, QFileInfo(target->absolutePath).absolutePath()
  );
  if (!targetDirectory.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return safetyFailure(targetDirectory);
  }
  const auto safeTarget = m_fileSafety.verifyNoLinkTraversal(
      {.receiveRoot = request.receiveRoot, .candidatePath = target->absolutePath}
  );
  if (!safeTarget.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return safetyFailure(safeTarget);
  }

  const QString stagingRelative = QStringLiteral(".incoming/%1/%2.part")
                                      .arg(
                                          request.begin.transferId.toString(),
                                          request.begin.fileId.toString()
                                      );
  QString stagingPath;
  const auto staging = PathPolicy::joinLexicallyUnderRoot(
      request.receiveRoot, stagingRelative, stagingPath, request.pathLimits
  );
  if (!staging.ok) {
    (void)m_conflicts.release(target->reservationId);
    return {
        .error = FileReceiverError::UnsafePath,
        .pathError = staging.error,
        .diagnostic = staging.diagnostic,
    };
  }
  const auto stagingDirectory = ensureSafeDirectoryPath(
      m_fileSafety, request.receiveRoot, QFileInfo(stagingPath).absolutePath()
  );
  if (!stagingDirectory.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return safetyFailure(stagingDirectory);
  }
  const auto safeStaging = m_fileSafety.verifyNoLinkTraversal(
      {.receiveRoot = request.receiveRoot, .candidatePath = stagingPath}
  );
  if (!safeStaging.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return safetyFailure(safeStaging);
  }
  auto receiverRequest = request;
  // ConflictResolver and IPlatformFileSafety own the final target. The shared
  // FileReceiver remains the staging/hash engine and must not resolve it again.
  receiverRequest.conflictPolicy = ConflictPolicy::AutoRename;
  const auto begun = resumeState == nullptr ? m_receiver.begin(receiverRequest)
                                            : m_receiver.resume(receiverRequest, *resumeState);
  if (!begun.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return begun;
  }
  m_target = *target;
  m_receiveRoot = request.receiveRoot;
  return begun;
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::append(
    const ::relaydesk::transfer::FileChunkMessage &chunk, QByteArrayView payload
)
{
  return isOwningThread() ? m_receiver.append(chunk, payload) : wrongThread();
}

::relaydesk::transfer::FileReceiverResult
IncomingFileReceiverWorker::finish(const ::relaydesk::transfer::FileEndMessage &end)
{
  using namespace ::relaydesk::transfer;

  if (!isOwningThread()) {
    return wrongThread();
  }
  if (!m_target.has_value() || m_receiveRoot.isEmpty()) {
    return {
        .error = FileReceiverError::InvalidState,
        .diagnostic = QStringLiteral("file receiver has no reserved commit target"),
    };
  }
  const auto staged = m_receiver.finishStaging(end);
  if (!staged.ok()) {
    (void)m_conflicts.release(m_target->reservationId);
    m_target.reset();
    return staged;
  }

  static_assert(
      static_cast<int>(TargetCommitDisposition::FailIfExists) ==
      static_cast<int>(CommitDisposition::FailIfExists)
  );
  static_assert(
      static_cast<int>(TargetCommitDisposition::ReplaceExisting) ==
      static_cast<int>(CommitDisposition::ReplaceExisting)
  );
  FileSafetyResult commit;
  QString committedPath;
  for (quint32 attempt = 0; attempt < m_conflictRequest.maximumRenameAttempts; ++attempt) {
    commit = m_fileSafety.commitStagedFile({
        .receiveRoot = m_receiveRoot,
        .stagingPath = m_receiver.m_snapshot.partPath,
        .destinationPath = m_target->absolutePath,
        .disposition = static_cast<CommitDisposition>(m_target->commitDisposition),
    });
    if (commit.ok()) {
      committedPath = m_target->absolutePath;
      (void)m_conflicts.release(m_target->reservationId);
      m_target.reset();
      break;
    }
    const bool retryCollision =
        commit.error == FileSafetyError::DestinationExists &&
        (m_conflictRequest.policy == ConflictPolicy::AutoRename ||
         m_conflictRequest.policy == ConflictPolicy::Overwrite);
    if (!retryCollision) {
      break;
    }
    const QString collidedPath = m_target->absolutePath;
    const auto retried = m_conflicts.retry(m_conflictRequest, m_target->reservationId);
    m_target.reset();
    const auto *retryTarget = std::get_if<UseTarget>(&retried);
    if (retryTarget == nullptr) {
      if (const auto *failure = std::get_if<ConflictFailure>(&retried)) {
        commit = {
            .error = failure->error == ConflictResolverError::UnsafePath
                         ? FileSafetyError::DestinationInvalid
                         : FileSafetyError::CommitFailed,
            .diagnostic = failure->diagnostic,
        };
      }
      break;
    }
    if (retryTarget->absolutePath == collidedPath) {
      (void)m_conflicts.release(retryTarget->reservationId);
      commit.diagnostic = QStringLiteral("platform reported a destination collision that is not visible to retry");
      break;
    }
    const auto safeTarget = m_fileSafety.verifyNoLinkTraversal(
        {.receiveRoot = m_receiveRoot, .candidatePath = retryTarget->absolutePath}
    );
    if (!safeTarget.ok()) {
      (void)m_conflicts.release(retryTarget->reservationId);
      commit = safeTarget;
      break;
    }
    m_target = *retryTarget;
  }
  if (m_target.has_value()) {
    (void)m_conflicts.release(m_target->reservationId);
    m_target.reset();
  }
  if (!commit.ok()) {
    const auto receiverError = commit.error == FileSafetyError::DestinationExists
                                   ? FileReceiverError::TargetExists
                                   : commit.error == FileSafetyError::LinkTraversalDetected ||
                                             commit.error == FileSafetyError::DestinationInvalid
                                         ? FileReceiverError::UnsafePath
                                         : FileReceiverError::CommitFailed;
    const auto resultCode = commit.error == FileSafetyError::DestinationExists
                                ? FileResultCode::TargetExists
                                : commit.error == FileSafetyError::LinkTraversalDetected ||
                                          commit.error == FileSafetyError::DestinationInvalid
                                      ? FileResultCode::PathInvalid
                                      : FileResultCode::IoError;
    return m_receiver.failCommit(receiverError, resultCode, commit.diagnostic);
  }
  return m_receiver.confirmCommitted(committedPath);
}

::relaydesk::transfer::FileReceiverSnapshot IncomingFileReceiverWorker::snapshot() const
{
  return m_receiver.snapshot();
}

IncomingFileDisposition IncomingFileReceiverWorker::disposition() const noexcept
{
  return m_disposition;
}

::relaydesk::transfer::DurableCheckpointResult IncomingFileReceiverWorker::checkpoint(
    const ::relaydesk::transfer::ResumeStore &store,
    ::relaydesk::transfer::ResumeState &state
)
{
  if (!isOwningThread()) {
    return {
        .error = ::relaydesk::transfer::DurableCheckpointError::InvalidReceiverState,
        .diagnostic = QStringLiteral("file receiver must stay on its constructing disk worker"),
    };
  }
  return m_receiver.checkpoint(store, state);
}

bool IncomingFileReceiverWorker::isOwningThread() const noexcept
{
  return QThread::currentThread() == m_ownerThread;
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::wrongThread() const
{
  return {
      .error = ::relaydesk::transfer::FileReceiverError::InvalidState,
      .diagnostic = QStringLiteral("file receiver must stay on its constructing disk worker"),
  };
}

::relaydesk::transfer::FileReceiverResult
IncomingFileReceiverWorker::safetyFailure(const FileSafetyResult &result) const
{
  using ::relaydesk::transfer::FileReceiverError;

  const auto error = result.error == FileSafetyError::LinkTraversalDetected ||
                             result.error == FileSafetyError::DestinationInvalid
                         ? FileReceiverError::UnsafePath
                         : FileReceiverError::InvalidRequest;
  return {.error = error, .diagnostic = result.diagnostic};
}

} // namespace deskflow::relaydesk
