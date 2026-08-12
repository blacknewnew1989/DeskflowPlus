// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileReceiver.h"

#include <QDir>
#include <QFileInfo>

#include <limits>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr quint32 kMaximumChunkBytes = 4U * 1024U * 1024U;
constexpr int kMaximumAutoRenameAttempts = 10'000;

FileReceiverResult failure(FileReceiverError error, QString diagnostic, PathError pathError = PathError::None)
{
  return {.error = error, .pathError = pathError, .diagnostic = std::move(diagnostic)};
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
  if (m_snapshot.state != FileReceiverState::Idle) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not idle"));
  }
  if (request.conflictPolicy != ConflictPolicy::AutoRename) {
    return failure(
        FileReceiverError::UnsupportedConflictPolicy,
        QStringLiteral("this receiver slice supports only the default auto-rename policy")
    );
  }
  if (request.entry.type != ManifestEntryType::File || request.entry.id.isNull() || request.begin.transferId.isNull() ||
      request.begin.fileId != request.entry.id || request.begin.size != request.entry.size ||
      request.begin.size > static_cast<quint64>(std::numeric_limits<qint64>::max()) || request.begin.startOffset != 0 ||
      request.begin.chunkBytes == 0 || request.begin.chunkBytes > kMaximumChunkBytes ||
      request.entry.sha256.size() != kSha256Bytes || request.begin.expectedSha256 != request.entry.sha256) {
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
                                          request.begin.transferId.toString(QUuid::WithoutBraces),
                                          request.begin.fileId.toString(QUuid::WithoutBraces)
                                      );
  QString partPath;
  const auto stagingPath =
      PathPolicy::joinLexicallyUnderRoot(request.receiveRoot, stagingRelative, partPath, request.pathLimits);
  if (!stagingPath.ok) {
    return failure(FileReceiverError::UnsafePath, stagingPath.diagnostic, stagingPath.error);
  }
  if (QFileInfo::exists(partPath)) {
    return failure(
        FileReceiverError::StagingExists,
        QStringLiteral("a staging file already exists; resume state is required before it can be reused")
    );
  }
  if (!QDir().mkpath(QFileInfo(partPath).absolutePath())) {
    return failure(FileReceiverError::DirectoryCreateFailed, QStringLiteral("could not create the staging directory"));
  }

  m_partFile.setFileName(partPath);
  if (!m_partFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return failure(
        FileReceiverError::StagingOpenFailed, QStringLiteral("could not exclusively create the staging file")
    );
  }

  m_hash.reset();
  m_expectedSha256 = request.entry.sha256;
  m_chunkBytes = request.begin.chunkBytes;
  m_requestedTarget = std::move(requestedTarget);
  m_conflictPolicy = request.conflictPolicy;
  m_snapshot = FileReceiverSnapshot{
      .state = FileReceiverState::Receiving,
      .transferId = request.begin.transferId,
      .fileId = request.begin.fileId,
      .expectedSize = request.begin.size,
      .relativeProtocolPath = targetPath.normalized,
      .partPath = std::move(partPath),
  };
  return {};
}

FileReceiverResult FileReceiver::append(const FileChunkMessage &chunk, QByteArrayView payload)
{
  if (m_snapshot.state != FileReceiverState::Receiving) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not accepting chunks"));
  }
  if (chunk.transferId != m_snapshot.transferId) {
    return fail(FileReceiverError::TransferIdMismatch, FileResultCode::IoError, QStringLiteral("TRANSFER_ID_MISMATCH"));
  }
  if (chunk.fileId != m_snapshot.fileId) {
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
  if (m_snapshot.state != FileReceiverState::Receiving) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("file receiver is not awaiting FILE_END"));
  }
  if (end.transferId != m_snapshot.transferId) {
    return fail(FileReceiverError::TransferIdMismatch, FileResultCode::IoError, QStringLiteral("TRANSFER_ID_MISMATCH"));
  }
  if (end.fileId != m_snapshot.fileId) {
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
  if (!QDir().mkpath(QFileInfo(m_requestedTarget).absolutePath())) {
    return fail(
        FileReceiverError::DirectoryCreateFailed, FileResultCode::IoError,
        QStringLiteral("TARGET_DIRECTORY_CREATE_FAILED")
    );
  }

  const QString committedPath = chooseAutoRenameTarget(m_requestedTarget);
  if (committedPath.isEmpty()) {
    return fail(
        FileReceiverError::TargetExists, FileResultCode::TargetExists, QStringLiteral("TARGET_NAME_UNAVAILABLE")
    );
  }
  if (!QFile::rename(m_snapshot.partPath, committedPath)) {
    return fail(FileReceiverError::CommitFailed, FileResultCode::IoError, QStringLiteral("ATOMIC_COMMIT_FAILED"));
  }

  m_snapshot.committedPath = committedPath;
  m_snapshot.state = FileReceiverState::Completed;
  return result(FileResultCode::Ok);
}

FileReceiverResult FileReceiver::cancel(bool keepPartial)
{
  if (m_snapshot.state == FileReceiverState::Completed) {
    return failure(FileReceiverError::InvalidState, QStringLiteral("a committed file cannot be cancelled"));
  }
  if (m_snapshot.state == FileReceiverState::Cancelled) {
    return m_snapshot.transferId.isNull() ? FileReceiverResult{}
                                          : result(FileResultCode::Cancelled, QStringLiteral("CANCELLED"));
  }
  if (m_partFile.isOpen()) {
    m_partFile.close();
  }
  if (!keepPartial && !m_snapshot.partPath.isEmpty()) {
    QFile::remove(m_snapshot.partPath);
  }
  m_snapshot.state = FileReceiverState::Cancelled;
  return m_snapshot.transferId.isNull() ? FileReceiverResult{}
                                        : result(FileResultCode::Cancelled, QStringLiteral("CANCELLED"));
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
  if (!m_snapshot.transferId.isNull() && !m_snapshot.fileId.isNull()) {
    receiverResult.fileResult = FileResultMessage{
        .transferId = m_snapshot.transferId,
        .fileId = m_snapshot.fileId,
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
