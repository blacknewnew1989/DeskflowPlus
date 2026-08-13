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

IncomingFileReceiverWorker::IncomingFileReceiverWorker(IPlatformFileSafety &fileSafety)
    : m_fileSafety(fileSafety), m_ownerThread(QThread::currentThread())
{
}

::relaydesk::transfer::FileReceiverResult IncomingFileReceiverWorker::begin(
    const ::relaydesk::transfer::FileReceiveRequest &request
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

  const auto decision = m_conflicts.resolve({
      .targetRoot = request.receiveRoot,
      .relativeProtocolPath = request.entry.relativeProtocolPath,
      .policy = request.conflictPolicy,
      .pathLimits = request.pathLimits,
  });
  const auto *target = std::get_if<UseTarget>(&decision);
  if (target == nullptr) {
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
  if (!QDir().mkpath(QFileInfo(target->absolutePath).absolutePath())) {
    (void)m_conflicts.release(target->reservationId);
    return {
        .error = FileReceiverError::DirectoryCreateFailed,
        .diagnostic = QStringLiteral("could not create the reserved target directory"),
    };
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
  const auto safeStaging = m_fileSafety.verifyNoLinkTraversal(
      {.receiveRoot = request.receiveRoot, .candidatePath = stagingPath}
  );
  if (!safeStaging.ok()) {
    (void)m_conflicts.release(target->reservationId);
    return safetyFailure(safeStaging);
  }
  const auto begun = m_receiver.begin(request);
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
  const auto commit = m_fileSafety.commitStagedFile({
      .receiveRoot = m_receiveRoot,
      .stagingPath = m_receiver.m_snapshot.partPath,
      .destinationPath = m_target->absolutePath,
      .disposition = static_cast<CommitDisposition>(m_target->commitDisposition),
  });
  const QString committedPath = m_target->absolutePath;
  (void)m_conflicts.release(m_target->reservationId);
  m_target.reset();
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
