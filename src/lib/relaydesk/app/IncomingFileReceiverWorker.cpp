/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingFileReceiverWorker.h"

#include "relaydesk/transfer/PathPolicy.h"

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

  QString targetPath;
  const auto target = PathPolicy::joinLexicallyUnderRoot(
      request.receiveRoot, request.entry.relativeProtocolPath, targetPath, request.pathLimits
  );
  if (!target.ok) {
    return {
        .error = FileReceiverError::UnsafePath,
        .pathError = target.error,
        .diagnostic = target.diagnostic,
    };
  }
  const auto safeTarget = m_fileSafety.verifyNoLinkTraversal(
      {.receiveRoot = request.receiveRoot, .candidatePath = targetPath}
  );
  if (!safeTarget.ok()) {
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
    return safetyFailure(safeStaging);
  }
  return m_receiver.begin(request);
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
  return isOwningThread() ? m_receiver.finish(end) : wrongThread();
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

