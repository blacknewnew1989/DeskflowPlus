/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include "relaydesk/app/IncomingFileReceiverWorker.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/ResumeMessageCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferRecoveryStore.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QStorageInfo>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QWaitCondition>
#include <QtConcurrentRun>

#include <atomic>
#include <limits>
#include <utility>
#include <variant>

namespace deskflow::relaydesk {

struct IncomingTransferRuntime::Session
{
  Session(
      DeviceId peerDeviceId, QString peerName, bool trusted, bool allowsAutoAccept,
      ::relaydesk::transfer::TransferOffer incomingOffer,
      ::relaydesk::transfer::NegotiatedCapabilities capabilities
  )
      : peer(std::move(peerDeviceId)), peerDisplayName(std::move(peerName)), peerTrusted(trusted),
        peerAllowsAutoAccept(allowsAutoAccept), offer(std::move(incomingOffer)), capabilities(std::move(capabilities)),
        stateMachine(this->capabilities)
  {
  }

  DeviceId peer;
  QString peerDisplayName;
  bool peerTrusted = false;
  bool peerAllowsAutoAccept = false;
  ::relaydesk::transfer::TransferOffer offer;
  ::relaydesk::transfer::NegotiatedCapabilities capabilities;
  ::relaydesk::transfer::TransferOfferStateMachine stateMachine;
  bool acceptPreflightPending = false;
  ::relaydesk::transfer::ReceiveOptions receiveOptions;
  std::shared_ptr<ReceivePipeline> pipeline;
  bool resumeNegotiated = false;
  std::optional<::relaydesk::transfer::ResumeResponseMessage> lastResumeResponse;
  quint64 pipelineGeneration = 0;
  std::optional<::relaydesk::transfer::IncomingConflictPrompt> pendingConflict;
  ::relaydesk::transfer::TransferSnapshot pipelineSnapshot{
      .id = offer.transferId,
      .peerId = peer,
      .peerDisplayName = peerDisplayName,
      .displayName = offer.displayName,
      .direction = ::relaydesk::transfer::TransferDirection::Receiving,
      .state = ::relaydesk::transfer::TransferState::WaitingForAcceptance,
      .progress = {
          .totalBytes = offer.totalBytes,
          .totalFiles = offer.fileCount,
      },
      .canCancel = true,
      .createdUtc = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(offer.createdAtMs), Qt::UTC),
  };
};

struct IncomingTransferRuntime::AcceptPreflightResult
{
  FileSafetyResult safety;
  quint64 freeBytes = 0;
};

struct IncomingTransferRuntime::RecoveryHydrationState
{
  QList<::relaydesk::transfer::IncomingRecoveryState> pending;
  quint64 epoch = 0;
  bool scanStarted = false;
  bool validationRunning = false;
};

struct CancelCleanupResult
{
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return diagnostic.isEmpty();
  }
};

struct IncomingRecoveryValidationResult
{
  std::optional<::relaydesk::transfer::ResumeState> resumeState;
  quint64 completedBytes = 0;
  quint64 completedFiles = 0;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return resumeState.has_value() && diagnostic.isEmpty();
  }
};

namespace {

constexpr quint64 kMaximumQueuedReceiveBytes = 32U * 1024U * 1024U;

quint64 frameBytes(const ::relaydesk::transfer::Frame &frame)
{
  return static_cast<quint64>(::relaydesk::transfer::kFixedHeaderBytes) +
         static_cast<quint64>(frame.metadata.size()) + static_cast<quint64>(frame.payload.size());
}

::relaydesk::transfer::TransferErrorCode receiverErrorCode(
    ::relaydesk::transfer::FileReceiverError error
)
{
  using namespace ::relaydesk::transfer;
  switch (error) {
  case FileReceiverError::UnsafePath:
    return TransferErrorCode::UnsafePath;
  case FileReceiverError::HashMismatch:
    return TransferErrorCode::HashMismatch;
  case FileReceiverError::WriteFailed:
    return TransferErrorCode::DiskFull;
  default:
    return TransferErrorCode::InternalError;
  }
}

bool isReceivePipelineActive(::relaydesk::transfer::TransferState state)
{
  return state == ::relaydesk::transfer::TransferState::Queued ||
         state == ::relaydesk::transfer::TransferState::Transferring ||
         state == ::relaydesk::transfer::TransferState::Paused ||
         state == ::relaydesk::transfer::TransferState::Resuming;
}

::relaydesk::transfer::IncomingConflictDecision defaultConflictDecision(
    ::relaydesk::transfer::ConflictPolicy policy
)
{
  using namespace ::relaydesk::transfer;
  switch (policy) {
  case ConflictPolicy::AutoRename:
    return IncomingConflictDecision::AutoRename;
  case ConflictPolicy::Skip:
    return IncomingConflictDecision::Skip;
  case ConflictPolicy::Overwrite:
  case ConflictPolicy::Ask:
    return IncomingConflictDecision::Overwrite;
  }
  return IncomingConflictDecision::Overwrite;
}

std::optional<::relaydesk::transfer::TransferId> decodedDataTransferId(
    const ::relaydesk::transfer::Frame &frame, QString *diagnostic
)
{
  using namespace ::relaydesk::transfer;
  if (frame.type == MessageType::ManifestPage) {
    const auto decoded = ManifestPageCodec::decode(frame.version, frame.metadata);
    if (!decoded.ok()) {
      if (diagnostic != nullptr) {
        *diagnostic = decoded.diagnostic;
      }
      return std::nullopt;
    }
    return decoded.page->transferId;
  }
  if (frame.type == MessageType::ManifestComplete) {
    const auto decoded = ManifestPageCodec::decodeComplete(frame.version, frame.metadata);
    if (!decoded.ok()) {
      if (diagnostic != nullptr) {
        *diagnostic = decoded.diagnostic;
      }
      return std::nullopt;
    }
    return decoded.message->transferId;
  }
  if (frame.type == MessageType::FileBegin || frame.type == MessageType::FileChunk ||
      frame.type == MessageType::FileEnd) {
    const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
    if (!decoded.ok()) {
      if (diagnostic != nullptr) {
        *diagnostic = decoded.diagnostic;
      }
      return std::nullopt;
    }
    return std::visit([](const auto &message) { return message.transferId; }, *decoded.message);
  }
  if (diagnostic != nullptr) {
    *diagnostic = QStringLiteral("frame type is not a valid cancelled transfer tail frame");
  }
  return std::nullopt;
}

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
        .diagnostic = QStringLiteral("manifest directory is not a normalized child of the receive root"),
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
            .diagnostic = QStringLiteral("manifest directory path contains a non-directory"),
        };
      }
    } else if (!QDir().mkdir(current)) {
      return {
          .error = FileSafetyError::DestinationInvalid,
          .diagnostic = QStringLiteral("could not create a verified manifest directory"),
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

void setDiagnostic(QString *output, QString diagnostic)
{
  if (output != nullptr) {
    *output = std::move(diagnostic);
  }
}

QString logicalDestination(const QString &receiveRoot)
{
  const QString name = QDir::cleanPath(receiveRoot).section(QLatin1Char('/'), -1);
  return name.trimmed().isEmpty() ? QStringLiteral("RelayDesk") : name;
}

} // namespace

class IncomingTransferRuntime::ReceivePipeline final
{
public:
  ReceivePipeline(
      IncomingTransferRuntime &runtime, DeviceId peer,
      ::relaydesk::transfer::TransferOffer offer,
      ::relaydesk::transfer::ReceiveOptions options, IPlatformFileSafety &fileSafety,
      QThreadPool &pool,
      std::optional<::relaydesk::transfer::ResumeState> resumeState = std::nullopt,
      quint64 generation = 0,
      std::optional<::relaydesk::transfer::IncomingRecoveryState> recoveryDescriptor = std::nullopt,
      ::relaydesk::transfer::TransferRecoveryStore *recoveryStore = nullptr
  )
      : m_runtime(&runtime), m_peer(std::move(peer)), m_offer(std::move(offer)),
        m_options(std::move(options)), m_fileSafety(fileSafety),
        m_resumeStore(QDir(m_options.destinationRoot).filePath(QStringLiteral(".incoming/resume-active"))),
        m_resumeState(std::move(resumeState)), m_generation(generation),
        m_recoveryDescriptor(std::move(recoveryDescriptor)), m_recoveryStore(recoveryStore), m_reassembler(
            m_offer.transferId, m_offer.manifestPageCount,
            m_offer.fileCount + m_offer.directoryCount, m_offer.manifestSha256
        )
  {
    m_future = QtConcurrent::run(&pool, [this]() { run(); });
  }

  ~ReceivePipeline()
  {
    stop();
    m_future.waitForFinished();
  }

  [[nodiscard]] bool enqueue(const ::relaydesk::transfer::Frame &frame, QString *diagnostic)
  {
    const quint64 bytes = frameBytes(frame);
    const QMutexLocker lock(&m_mutex);
    if (m_stopping) {
      setDiagnostic(diagnostic, QStringLiteral("incoming receive worker is stopping"));
      return false;
    }
    if (bytes > kMaximumQueuedReceiveBytes || m_queuedBytes > kMaximumQueuedReceiveBytes - bytes) {
      setDiagnostic(diagnostic, QStringLiteral("incoming receive queue exceeded its bounded memory limit"));
      return false;
    }
    m_frames.enqueue(frame);
    m_queuedBytes += bytes;
    m_ready.wakeOne();
    return true;
  }

  void stop()
  {
    const QMutexLocker lock(&m_mutex);
    m_stopping = true;
    m_frames.clear();
    m_queuedBytes = 0;
    m_ready.wakeAll();
  }

  void pause()
  {
    const QMutexLocker lock(&m_mutex);
    m_paused = true;
    // A conflict wait shares this condition variable. Waking here lets the
    // worker re-check cancellation and keeps pause from being mistaken for a
    // decision while it remains blocked on the prompt.
    m_ready.wakeAll();
  }

  void resume()
  {
    const QMutexLocker lock(&m_mutex);
    m_paused = false;
    m_ready.wakeAll();
  }

  [[nodiscard]] bool resolveConflict(
      const QUuid &conflictId, ::relaydesk::transfer::IncomingConflictDecision decision
  )
  {
    const QMutexLocker lock(&m_mutex);
    if (!m_pendingConflict.has_value() || m_pendingConflict->conflictId != conflictId || m_stopping) {
      return false;
    }
    m_conflictDecision = decision;
    m_ready.wakeAll();
    return true;
  }

  void waitForFinished()
  {
    m_future.waitForFinished();
  }

private:
  void run()
  {
    for (;;) {
      ::relaydesk::transfer::Frame frame;
      {
        QMutexLocker lock(&m_mutex);
        while ((m_frames.isEmpty() || m_paused) && !m_stopping) {
          m_ready.wait(&m_mutex);
        }
        if (m_stopping) {
          break;
        }
        frame = m_frames.dequeue();
        m_queuedBytes -= frameBytes(frame);
      }
      if (!process(frame)) {
        const QMutexLocker lock(&m_mutex);
        m_stopping = true;
        break;
      }
    }
  }

  [[nodiscard]] bool process(const ::relaydesk::transfer::Frame &frame)
  {
    using namespace ::relaydesk::transfer;
    QString diagnostic;
    if (frame.type == MessageType::ManifestPage) {
      const auto error = m_reassembler.addEncodedPage(frame.version, frame.metadata, &diagnostic);
      if (error != ManifestPageError::None) {
        fail(TransferErrorCode::InternalError, diagnostic);
        return false;
      }
      return true;
    }
    if (frame.type == MessageType::ManifestComplete) {
      const auto decoded = ManifestPageCodec::decodeComplete(frame.version, frame.metadata);
      if (!decoded.ok()) {
        fail(TransferErrorCode::InternalError, decoded.diagnostic);
        return false;
      }
      auto complete = m_reassembler.finish(*decoded.message);
      if (!complete.ok()) {
        fail(TransferErrorCode::HashMismatch, complete.diagnostic);
        return false;
      }
      m_entries = std::move(*complete.entries);
      m_manifestReady = prepareDirectories(diagnostic);
      if (m_manifestReady) {
        m_manifestReady = prepareResumeState(diagnostic);
      }
      if (m_manifestReady) {
        m_manifestReady = persistRecoveryDescriptor(diagnostic);
      }
      if (!m_manifestReady) {
        fail(TransferErrorCode::InternalError, diagnostic);
      }
      if (m_manifestReady && m_offer.fileCount == 0) {
        for (const auto &entry : m_entries) {
          if (entry.type == ManifestEntryType::Directory) {
            recordCompletedPath(entry.relativeProtocolPath);
            break;
          }
        }
        publishCompleted();
        return false;
      }
      return true;
    }
    if (!m_manifestReady) {
      fail(TransferErrorCode::InternalError, QStringLiteral("file frame arrived before complete manifest"));
      return false;
    }
    const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
    if (!decoded.ok()) {
      fail(TransferErrorCode::InternalError, decoded.diagnostic);
      return false;
    }
    if (const auto *begin = std::get_if<FileBeginMessage>(&*decoded.message)) {
      if (m_receiver != nullptr || m_skippedBegin.has_value()) {
        fail(TransferErrorCode::InternalError, QStringLiteral("FILE_BEGIN arrived while a file is active"));
        return false;
      }
      const auto entry = entryFor(begin->fileId);
      if (!entry.has_value()) {
        fail(TransferErrorCode::InternalError, QStringLiteral("FILE_BEGIN is absent from accepted manifest"));
        return false;
      }
      m_receiver = std::make_unique<IncomingFileReceiverWorker>(m_fileSafety);
      m_activeStreamId = frame.streamId;
      const FileReceiveRequest request{
          .receiveRoot = m_options.destinationRoot,
          .entry = *entry,
          .begin = *begin,
          .manifestSha256 = m_offer.manifestSha256,
          .conflictPolicy = m_options.conflictPolicy,
      };
      m_activeDecision = defaultConflictDecision(m_options.conflictPolicy);
      const auto *resumeFile = resumeFileFor(begin->fileId);
      const QString partPath = QDir(m_options.destinationRoot).filePath(
          QStringLiteral(".incoming/%1/%2.part")
              .arg(begin->transferId.toString(), begin->fileId.toString())
      );
      const bool reusePart = resumeFile != nullptr && QFileInfo::exists(partPath);
      const auto result = reusePart ? m_receiver->resume(request, *m_resumeState)
                                    : m_receiver->begin(request);
      auto resolved = result;
      if (const auto prompt = m_receiver->pendingConflict(); prompt.has_value()) {
        const auto decision = waitForConflictDecision(*prompt);
        if (!decision.has_value()) {
          return false;
        }
        if (*decision == IncomingConflictDecision::CancelTransfer) {
          invoke([id = m_offer.transferId](IncomingTransferRuntime &runtime) {
            Q_EMIT runtime.incomingConflictCancelRequested(id);
          });
          return false;
        }
        m_activeDecision = *decision;
        resolved = m_receiver->resolveConflict(*decision);
      }
      if (!resolved.ok()) {
        sendFileResult(resolved);
        fail(receiverErrorCode(resolved.error), resolved.diagnostic);
        return false;
      }
      if (m_receiver->disposition() == IncomingFileDisposition::Skip) {
        m_activeDecision = IncomingConflictDecision::Skip;
        m_receiver.reset();
        m_skippedBegin = *begin;
        m_skippedEntry = *entry;
        m_skippedBytes = 0;
        m_skippedNextSequence = 0;
        publishProgress(m_completedBytes, m_completedFiles, entry->relativeProtocolPath);
        return true;
      }
      publishProgress(
          m_completedBytes + m_receiver->snapshot().receivedBytes, m_completedFiles,
          entry->relativeProtocolPath
      );
      return true;
    }
    if (const auto *chunk = std::get_if<FileChunkMessage>(&*decoded.message)) {
      if (m_skippedBegin.has_value()) {
        const quint64 payloadBytes = static_cast<quint64>(frame.payload.size());
        if (!m_skippedEntry.has_value() || chunk->transferId != m_offer.transferId ||
            chunk->fileId != m_skippedBegin->fileId || chunk->offset != m_skippedBytes ||
            chunk->sequence != m_skippedNextSequence || frame.payload.isEmpty() ||
            payloadBytes > m_skippedBegin->chunkBytes ||
            payloadBytes > m_skippedBegin->size - m_skippedBytes) {
          fail(TransferErrorCode::InternalError, QStringLiteral("skipped file stream is invalid"));
          return false;
        }
        m_skippedBytes += payloadBytes;
        ++m_skippedNextSequence;
        return true;
      }
      if (m_receiver == nullptr) {
        fail(TransferErrorCode::InternalError, QStringLiteral("FILE_CHUNK arrived without FILE_BEGIN"));
        return false;
      }
      const auto result = m_receiver->append(*chunk, QByteArrayView(frame.payload));
      if (!result.ok()) {
        sendFileResult(result);
        fail(receiverErrorCode(result.error), result.diagnostic);
        return false;
      }
      const auto snapshot = m_receiver->snapshot();
      if (snapshot.receivedBytes < snapshot.expectedSize) {
        const auto checkpoint = m_receiver->checkpoint(m_resumeStore, *m_resumeState);
        if (!checkpoint.ok()) {
          fail(TransferErrorCode::InternalError, checkpoint.diagnostic);
          return false;
        }
        sendCheckpoint(*checkpoint.message);
      }
      publishProgress(
          m_completedBytes + snapshot.receivedBytes, m_completedFiles,
          snapshot.relativeProtocolPath
      );
      return true;
    }
    if (const auto *end = std::get_if<FileEndMessage>(&*decoded.message)) {
      if (m_skippedBegin.has_value()) {
        if (!m_skippedEntry.has_value() || end->transferId != m_offer.transferId ||
            end->fileId != m_skippedBegin->fileId || end->size != m_skippedBegin->size ||
            m_skippedBytes != m_skippedBegin->size || end->sha256 != m_skippedEntry->sha256) {
          fail(TransferErrorCode::InternalError, QStringLiteral("skipped FILE_END is invalid"));
          return false;
        }
        const FileReceiverResult skipped{
            .error = FileReceiverError::TargetExists,
            .diagnostic = QStringLiteral("TARGET_SKIPPED"),
            .fileResult = FileResultMessage{
                .transferId = end->transferId,
                .fileId = end->fileId,
                .code = FileResultCode::TargetExists,
                .diagnostic = QStringLiteral("TARGET_SKIPPED"),
            },
        };
        if (!persistCompletedFile(end->fileId, {}, IncomingConflictDecision::Skip, diagnostic)) {
          fail(TransferErrorCode::InternalError, diagnostic);
          return false;
        }
        sendFileResult(skipped);
        m_completedBytes += m_skippedEntry->size;
        ++m_completedFiles;
        m_completedAtReceiveRoot = true;
        publishProgress(m_completedBytes, m_completedFiles, m_skippedEntry->relativeProtocolPath);
        m_skippedBegin.reset();
        m_skippedEntry.reset();
        if (m_completedFiles == m_offer.fileCount) {
          publishCompleted();
          return false;
        }
        return true;
      }
      if (m_receiver == nullptr) {
        fail(TransferErrorCode::InternalError, QStringLiteral("FILE_END arrived without FILE_BEGIN"));
        return false;
      }
      const auto result = m_receiver->finish(*end);
      if (!result.ok()) {
        const auto snapshot = m_receiver->snapshot();
        const bool skippedRace = m_options.conflictPolicy == ConflictPolicy::Skip &&
                                 result.fileResult.has_value() &&
                                 result.fileResult->code == FileResultCode::TargetExists;
        if (skippedRace && snapshot.fileId.has_value() &&
            persistCompletedFile(*snapshot.fileId, {}, IncomingConflictDecision::Skip, diagnostic)) {
          sendFileResult(result);
          m_completedBytes += snapshot.expectedSize;
          ++m_completedFiles;
          m_completedAtReceiveRoot = true;
          publishProgress(m_completedBytes, m_completedFiles, snapshot.relativeProtocolPath);
          m_receiver.reset();
          if (m_completedFiles == m_offer.fileCount) {
            publishCompleted();
            return false;
          }
          return true;
        }
        sendFileResult(result);
        fail(receiverErrorCode(result.error), result.diagnostic);
        return false;
      }
      const auto snapshot = m_receiver->snapshot();
      if (!persistCompletedFile(
              snapshot.fileId.value(), snapshot.committedPath,
              m_activeDecision.value_or(defaultConflictDecision(m_options.conflictPolicy)), diagnostic
          )) {
        fail(TransferErrorCode::InternalError, diagnostic);
        return false;
      }
      sendFileResult(result);
      m_completedBytes += snapshot.expectedSize;
      ++m_completedFiles;
      recordCompletedPath(snapshot.committedPath);
      publishProgress(m_completedBytes, m_completedFiles, snapshot.relativeProtocolPath);
      m_receiver.reset();
      if (m_completedFiles == m_offer.fileCount) {
        publishCompleted();
        return false;
      }
      return true;
    }
    fail(TransferErrorCode::InternalError, QStringLiteral("unsupported frame reached incoming file worker"));
    return false;
  }

  [[nodiscard]] bool prepareDirectories(QString &diagnostic)
  {
    using namespace ::relaydesk::transfer;
    for (const auto &entry : m_entries) {
      if (entry.type != ManifestEntryType::Directory) {
        continue;
      }
      QString absolutePath;
      const auto joined = PathPolicy::joinLexicallyUnderRoot(
          m_options.destinationRoot, entry.relativeProtocolPath, absolutePath
      );
      if (!joined.ok) {
        diagnostic = joined.diagnostic;
        return false;
      }
      const auto safe = ensureSafeDirectoryPath(
          m_fileSafety, m_options.destinationRoot, absolutePath
      );
      if (!safe.ok()) {
        diagnostic = safe.diagnostic;
        return false;
      }
    }
    return true;
  }

  void recordCompletedPath(const QString &absoluteOrRelativePath)
  {
    QString relativePath = absoluteOrRelativePath;
    if (QDir::isAbsolutePath(relativePath)) {
      relativePath = QDir(m_options.destinationRoot).relativeFilePath(relativePath);
    }
    relativePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const auto validated = ::relaydesk::transfer::PathPolicy::validateRelative(relativePath);
    if (!validated.ok) {
      return;
    }
    m_completedRelativePath = validated.normalized;
  }

  [[nodiscard]] std::optional<::relaydesk::transfer::ManifestEntry>
  entryFor(const ::relaydesk::transfer::FileId &fileId) const
  {
    for (const auto &entry : m_entries) {
      if (entry.id == fileId && entry.type == ::relaydesk::transfer::ManifestEntryType::File) {
        return entry;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] bool prepareResumeState(QString &diagnostic)
  {
    using namespace ::relaydesk::transfer;
    if (!m_resumeState.has_value()) {
      ResumeState state{
          .transferId = m_offer.transferId,
          .peerDeviceId = m_peer,
          .manifestSha256 = m_offer.manifestSha256,
          .direction = ResumeDirection::Receiving,
          .updatedUtc = QDateTime::currentDateTimeUtc(),
      };
      for (const auto &entry : m_entries) {
        if (entry.type != ManifestEntryType::File) {
          continue;
        }
        state.files.append({
            .fileId = entry.id,
            .relativeProtocolPath = entry.relativeProtocolPath,
            .totalBytes = entry.size,
            .partRelativePath =
                QStringLiteral(".incoming/%1/%2.part")
                    .arg(m_offer.transferId.toString(), entry.id.toString()),
        });
      }
      m_resumeState = std::move(state);
      const auto saved = m_resumeStore.save(*m_resumeState);
      if (!saved.ok()) {
        diagnostic = saved.diagnostic;
        return false;
      }
      return true;
    }

    if (m_resumeState->transferId != m_offer.transferId || m_resumeState->peerDeviceId != m_peer ||
        m_resumeState->manifestSha256 != m_offer.manifestSha256 ||
        m_resumeState->direction != ResumeDirection::Receiving) {
      diagnostic = QStringLiteral("stored resume state is not bound to this peer and manifest");
      return false;
    }
    qsizetype matchedFiles = 0;
    bool incompleteSeen = false;
    for (const auto &entry : m_entries) {
      if (entry.type != ManifestEntryType::File) {
        continue;
      }
      const auto *resolved = resolvedTargetFor(entry.id);
      if (resolved != nullptr) {
        if (resolved->size != entry.size || resolved->sha256 != entry.sha256 ||
            resolved->decision == IncomingConflictDecision::CancelTransfer) {
          diagnostic = QStringLiteral("stored resolved target does not match the accepted manifest");
          return false;
        }
        m_completedBytes += entry.size;
        ++m_completedFiles;
        continue;
      }
      const auto *file = resumeFileFor(entry.id);
      const QString expectedPart = QStringLiteral(".incoming/%1/%2.part")
                                       .arg(m_offer.transferId.toString(), entry.id.toString());
      if (file == nullptr) {
        if (entry.size == 0) {
          m_resumeState->files.append({
              .fileId = entry.id,
              .relativeProtocolPath = entry.relativeProtocolPath,
              .totalBytes = 0,
              .partRelativePath =
                  QStringLiteral(".incoming/%1/%2.part")
                      .arg(m_offer.transferId.toString(), entry.id.toString()),
          });
          continue;
        }
        diagnostic = QStringLiteral("stored resume state is missing a non-empty manifest file");
        return false;
      }
      ++matchedFiles;
      if (file->relativeProtocolPath != entry.relativeProtocolPath ||
          file->totalBytes != entry.size || file->durableOffset > entry.size ||
          file->partRelativePath != expectedPart ||
          (incompleteSeen && file->durableOffset != 0)) {
        diagnostic = QStringLiteral("stored resume offsets do not match the accepted manifest order");
        return false;
      }
      if (file->durableOffset == file->totalBytes) {
        m_completedBytes += file->totalBytes;
        ++m_completedFiles;
      } else {
        incompleteSeen = true;
      }
    }
    if (matchedFiles != m_resumeState->files.size()) {
      diagnostic = QStringLiteral("stored resume state has a different file set");
      return false;
    }
    return true;
  }

  [[nodiscard]] bool persistRecoveryDescriptor(QString &diagnostic)
  {
    using namespace ::relaydesk::transfer;

    if (!m_recoveryDescriptor.has_value()) {
      return true;
    }
    if (m_recoveryStore == nullptr) {
      diagnostic = QStringLiteral("incoming recovery store is unavailable");
      return false;
    }
    QList<PreparedManifestEntry> prepared;
    prepared.reserve(m_entries.size());
    for (const auto &entry : std::as_const(m_entries)) {
      const auto path = PathPolicy::validateRelative(entry.relativeProtocolPath);
      if (!path.ok) {
        diagnostic = path.diagnostic;
        return false;
      }
      prepared.append({
          .canonicalSourcePath = QDir::rootPath(),
          .protocolCollisionKey = path.collisionKey,
          .entry = entry,
      });
    }
    const TransferManifestSummary summary{
        .id = m_offer.transferId,
        .displayName = m_offer.displayName,
        .totalBytes = m_offer.totalBytes,
        .fileCount = m_offer.fileCount,
        .directoryCount = m_offer.directoryCount,
        .canonicalSha256 = m_offer.manifestSha256,
    };
    const auto plan = ManifestPageCodec::plan({.entries = prepared, .summary = summary});
    if (!plan.ok() || plan.plan->pageCount() != m_offer.manifestPageCount) {
      diagnostic = plan.ok() ? QStringLiteral("incoming recovery page count changed") : plan.diagnostic;
      return false;
    }
    m_recoveryDescriptor->entries = m_entries;
    m_recoveryDescriptor->pagePlan = {
        .entryCount = plan.plan->entryCount,
        .pageCount = plan.plan->pageCount(),
        .totalMetadataBytes = plan.plan->totalMetadataBytes,
    };
    const auto saved = m_recoveryStore->saveIncoming(*m_recoveryDescriptor);
    if (!saved.ok()) {
      diagnostic = saved.diagnostic;
      return false;
    }
    return true;
  }

  [[nodiscard]] bool persistCompletedFile(
      const ::relaydesk::transfer::FileId &fileId, QString targetPath,
      ::relaydesk::transfer::IncomingConflictDecision decision, QString &diagnostic
  )
  {
    using namespace ::relaydesk::transfer;

    auto *resumeFile = resumeFileFor(fileId);
    const auto entry = entryFor(fileId);
    if (resumeFile == nullptr || !entry.has_value() || decision == IncomingConflictDecision::CancelTransfer) {
      diagnostic = QStringLiteral("completed file is absent from resume state");
      return false;
    }
    QString relativeTargetPath;
    if (decision != IncomingConflictDecision::Skip) {
      relativeTargetPath = std::move(targetPath);
      if (QDir::isAbsolutePath(relativeTargetPath)) {
        relativeTargetPath = QDir(m_options.destinationRoot).relativeFilePath(relativeTargetPath);
      }
      relativeTargetPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
      const auto checked = PathPolicy::validateRelative(relativeTargetPath);
      if (!checked.ok) {
        diagnostic = checked.diagnostic;
        return false;
      }
      relativeTargetPath = checked.normalized;
    }
    for (const auto &resolved : std::as_const(m_resumeState->resolvedTargets)) {
      if (resolved.fileId == fileId) {
        diagnostic = QStringLiteral("completed file already has a resolved target");
        return false;
      }
    }
    for (auto iterator = m_resumeState->files.begin(); iterator != m_resumeState->files.end(); ++iterator) {
      if (iterator->fileId == fileId) {
        m_resumeState->files.erase(iterator);
        break;
      }
    }
    m_resumeState->resolvedTargets.append({
        .fileId = fileId,
        .relativeTargetPath = std::move(relativeTargetPath),
        .size = entry->size,
        .sha256 = entry->sha256,
        .decision = decision,
    });
    m_resumeState->updatedUtc = QDateTime::currentDateTimeUtc();
    const auto persisted = m_resumeStore.save(*m_resumeState);
    if (!persisted.ok()) {
      diagnostic = persisted.diagnostic;
      return false;
    }
    return true;
  }

  [[nodiscard]] ::relaydesk::transfer::ResumeFileState *
  resumeFileFor(const ::relaydesk::transfer::FileId &fileId)
  {
    if (!m_resumeState.has_value()) {
      return nullptr;
    }
    ::relaydesk::transfer::ResumeFileState *matching = nullptr;
    for (auto &file : m_resumeState->files) {
      if (file.fileId == fileId) {
        if (matching != nullptr) {
          return nullptr;
        }
        matching = &file;
      }
    }
    return matching;
  }

  [[nodiscard]] const ::relaydesk::transfer::ResumeFileState *
  resumeFileFor(const ::relaydesk::transfer::FileId &fileId) const
  {
    return const_cast<ReceivePipeline *>(this)->resumeFileFor(fileId);
  }

  [[nodiscard]] const ::relaydesk::transfer::ResolvedTargetState *
  resolvedTargetFor(const ::relaydesk::transfer::FileId &fileId) const
  {
    if (!m_resumeState.has_value()) {
      return nullptr;
    }
    const ::relaydesk::transfer::ResolvedTargetState *matching = nullptr;
    for (const auto &target : m_resumeState->resolvedTargets) {
      if (target.fileId == fileId) {
        if (matching != nullptr) {
          return nullptr;
        }
        matching = &target;
      }
    }
    return matching;
  }

  void sendCheckpoint(const ::relaydesk::transfer::FileCheckpointMessage &checkpoint)
  {
    using namespace ::relaydesk::transfer;
    QString diagnostic;
    Frame response{
        .type = MessageType::FileCheckpoint,
        .flags = Response,
        .streamId = m_activeStreamId,
        .metadata = FileMessageCodec::encode(FileControlMessage{checkpoint}, &diagnostic),
    };
    invoke([id = m_offer.transferId, generation = m_generation, peer = m_peer,
            response = std::move(response)](IncomingTransferRuntime &runtime) mutable {
      const auto *session = runtime.m_sessions.value(id, nullptr);
      if (session != nullptr && session->pipelineGeneration == generation &&
          isReceivePipelineActive(session->pipelineSnapshot.state)) {
        Q_EMIT runtime.responseReady(peer, std::move(response));
      }
    });
  }

  void sendFileResult(const ::relaydesk::transfer::FileReceiverResult &result)
  {
    using namespace ::relaydesk::transfer;
    if (!result.fileResult.has_value()) {
      return;
    }
    QString diagnostic;
    Frame response{
        .type = MessageType::FileResult,
        .flags = Response | Final,
        .streamId = m_activeStreamId,
        .metadata = FileMessageCodec::encode(FileControlMessage{*result.fileResult}, &diagnostic),
    };
    invoke(
        [id = m_offer.transferId, generation = m_generation, peer = m_peer,
         response = std::move(response)](IncomingTransferRuntime &runtime) mutable {
          const auto *session = runtime.m_sessions.value(id, nullptr);
          if (session == nullptr || session->pipelineGeneration != generation ||
              !isReceivePipelineActive(session->pipelineSnapshot.state)) {
            return;
          }
          Q_EMIT runtime.responseReady(peer, std::move(response));
        }
    );
  }

  void publishProgress(quint64 bytes, quint64 files, QString path)
  {
    invoke([id = m_offer.transferId, generation = m_generation, bytes, files,
            path = std::move(path)](IncomingTransferRuntime &runtime) {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr || session->pipelineGeneration != generation) {
        return;
      }
      auto snapshot = session->pipelineSnapshot;
      if (!isReceivePipelineActive(snapshot.state)) {
        return;
      }
      if (snapshot.state == ::relaydesk::transfer::TransferState::Paused) {
        return;
      }
      snapshot.state = ::relaydesk::transfer::TransferState::Transferring;
      snapshot.progress.completedBytes = bytes;
      snapshot.progress.completedFiles = files;
      snapshot.currentRelativeDisplayPath = path;
      session->pipelineSnapshot = snapshot;
      Q_EMIT runtime.transferChanged(snapshot);
    });
  }

  void publishCompleted()
  {
    const auto removed = m_resumeStore.remove(m_offer.transferId);
    if (!removed.ok()) {
      fail(::relaydesk::transfer::TransferErrorCode::InternalError, removed.diagnostic);
      return;
    }
    if (m_recoveryStore != nullptr) {
      const auto recoveryRemoved = m_recoveryStore->removeIncoming(m_offer.transferId);
      if (!recoveryRemoved.ok()) {
        fail(::relaydesk::transfer::TransferErrorCode::InternalError, recoveryRemoved.diagnostic);
        return;
      }
    }
    const auto completedRelativePath =
        m_completedAtReceiveRoot ? QStringLiteral(".") : m_completedRelativePath;
    invoke([id = m_offer.transferId, generation = m_generation,
            completedRelativePath](IncomingTransferRuntime &runtime) {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr || session->pipelineGeneration != generation) {
        return;
      }
      auto snapshot = session->pipelineSnapshot;
      if (!isReceivePipelineActive(snapshot.state)) {
        return;
      }
      snapshot.state = ::relaydesk::transfer::TransferState::Completed;
      snapshot.progress.completedBytes = snapshot.progress.totalBytes;
      snapshot.progress.completedFiles = snapshot.progress.totalFiles;
      snapshot.currentRelativeDisplayPath = completedRelativePath;
      snapshot.canCancel = false;
      snapshot.finishedUtc = QDateTime::currentDateTimeUtc();
      session->pipelineSnapshot = snapshot;
      Q_EMIT runtime.transferChanged(snapshot);
    });
  }

  void fail(::relaydesk::transfer::TransferErrorCode code, QString diagnostic)
  {
    invoke([id = m_offer.transferId, generation = m_generation, code,
            diagnostic = std::move(diagnostic)](
               IncomingTransferRuntime &runtime
           ) mutable {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr || session->pipelineGeneration != generation) {
        return;
      }
      auto snapshot = session->pipelineSnapshot;
      if (!isReceivePipelineActive(snapshot.state)) {
        return;
      }
      snapshot.state = ::relaydesk::transfer::TransferState::Failed;
      snapshot.errorCode = code;
      snapshot.canCancel = false;
      snapshot.canRetry = false;
      snapshot.finishedUtc = QDateTime::currentDateTimeUtc();
      session->pipelineSnapshot = snapshot;
      Q_EMIT runtime.transferChanged(snapshot);
      Q_EMIT runtime.pipelineFailed(id, code, std::move(diagnostic));
    });
  }

  [[nodiscard]] std::optional<::relaydesk::transfer::IncomingConflictDecision>
  waitForConflictDecision(const ::relaydesk::transfer::IncomingConflictPrompt &prompt)
  {
    {
      const QMutexLocker lock(&m_mutex);
      m_pendingConflict = prompt;
      m_conflictDecision.reset();
    }
    invoke([id = m_offer.transferId, generation = m_generation, prompt](IncomingTransferRuntime &runtime) {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr || session->pipelineGeneration != generation ||
          !isReceivePipelineActive(session->pipelineSnapshot.state)) {
        return;
      }
      session->pendingConflict = prompt;
      Q_EMIT runtime.incomingConflictDecisionRequired(prompt);
    });
    QMutexLocker lock(&m_mutex);
    while ((!m_conflictDecision.has_value() || m_paused) && !m_stopping) {
      m_ready.wait(&m_mutex);
    }
    if (m_stopping) {
      return std::nullopt;
    }
    const auto decision = m_conflictDecision;
    m_conflictDecision.reset();
    m_pendingConflict.reset();
    return decision;
  }

  template <typename Callback> void invoke(Callback callback)
  {
    QPointer<IncomingTransferRuntime> runtime = m_runtime;
    QMetaObject::invokeMethod(m_runtime, [runtime, callback = std::move(callback)]() mutable {
      if (runtime != nullptr) {
        callback(*runtime);
      }
    });
  }

  QPointer<IncomingTransferRuntime> m_runtime;
  DeviceId m_peer;
  ::relaydesk::transfer::TransferOffer m_offer;
  ::relaydesk::transfer::ReceiveOptions m_options;
  IPlatformFileSafety &m_fileSafety;
  ::relaydesk::transfer::ResumeStore m_resumeStore;
  std::optional<::relaydesk::transfer::ResumeState> m_resumeState;
  quint64 m_generation = 0;
  std::optional<::relaydesk::transfer::IncomingRecoveryState> m_recoveryDescriptor;
  ::relaydesk::transfer::TransferRecoveryStore *m_recoveryStore = nullptr;
  QString m_completedRelativePath;
  bool m_completedAtReceiveRoot = false;
  ::relaydesk::transfer::ManifestPageReassembler m_reassembler;
  QList<::relaydesk::transfer::ManifestEntry> m_entries;
  std::unique_ptr<IncomingFileReceiverWorker> m_receiver;
  std::optional<::relaydesk::transfer::FileBeginMessage> m_skippedBegin;
  std::optional<::relaydesk::transfer::ManifestEntry> m_skippedEntry;
  std::optional<::relaydesk::transfer::IncomingConflictDecision> m_activeDecision;
  quint64 m_skippedBytes = 0;
  quint64 m_skippedNextSequence = 0;
  quint64 m_completedBytes = 0;
  quint64 m_completedFiles = 0;
  quint32 m_activeStreamId = 0;
  bool m_manifestReady = false;
  QMutex m_mutex;
  QWaitCondition m_ready;
  QQueue<::relaydesk::transfer::Frame> m_frames;
  quint64 m_queuedBytes = 0;
  bool m_stopping = false;
  bool m_paused = false;
  std::optional<::relaydesk::transfer::IncomingConflictPrompt> m_pendingConflict;
  std::optional<::relaydesk::transfer::IncomingConflictDecision> m_conflictDecision;
  QFuture<void> m_future;
};

IncomingTransferRuntime::IncomingTransferRuntime(
    IPlatformFileSafety &fileSafety, QThreadPool &workerPool, TrustChecker trustChecker, QObject *parent
)
    : QObject(parent), m_fileSafety(fileSafety), m_workerPool(workerPool), m_trustChecker(std::move(trustChecker))
{
}

void IncomingTransferRuntime::configureRecovery(
    DeviceId localDeviceId, PeerFingerprintProvider peerFingerprintProvider,
    ::relaydesk::transfer::TransferRecoveryStore *recoveryStore
)
{
  m_localDeviceId = std::move(localDeviceId);
  m_peerFingerprintProvider = std::move(peerFingerprintProvider);
  m_recoveryStore = recoveryStore;
  if (m_recoveryStore != nullptr) {
    m_recoveryHydration = std::make_unique<RecoveryHydrationState>();
  }
}

void IncomingTransferRuntime::startRecoveryScan()
{
  using namespace ::relaydesk::transfer;

  if (m_recoveryStore == nullptr || m_recoveryHydration == nullptr || m_recoveryHydration->scanStarted) {
    return;
  }
  m_recoveryHydration->scanStarted = true;
  const quint64 epoch = ++m_recoveryHydration->epoch;
  auto *store = m_recoveryStore;
  auto *watcher = new QFutureWatcher<TransferRecoveryStoreScanResult<IncomingRecoveryState>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, epoch, watcher] {
    auto result = watcher->result();
    watcher->deleteLater();
    if (m_recoveryHydration == nullptr || epoch != m_recoveryHydration->epoch) {
      return;
    }
    if (!result.ok()) {
      Q_EMIT recoveryIssue(QStringLiteral("Incoming recovery scan failed: %1").arg(result.diagnostic));
      return;
    }
    for (const auto &issue : std::as_const(result.issues)) {
      Q_EMIT recoveryIssue(
          QStringLiteral("Incoming recovery state was skipped (%1): %2").arg(issue.path, issue.diagnostic)
      );
    }
    m_recoveryHydration->pending = std::move(result.states);
    hydrateNextRecoveryState();
  });
  watcher->setFuture(QtConcurrent::run(&m_workerPool, [store] { return store->scanIncoming(); }));
}

void IncomingTransferRuntime::stopRecoveryScan()
{
  if (m_recoveryHydration == nullptr) {
    return;
  }
  ++m_recoveryHydration->epoch;
  m_recoveryHydration->scanStarted = false;
  m_recoveryHydration->validationRunning = false;
  m_recoveryHydration->pending.clear();
}

void IncomingTransferRuntime::hydrateNextRecoveryState()
{
  using namespace ::relaydesk::transfer;

  if (m_recoveryHydration == nullptr || m_recoveryHydration->validationRunning ||
      m_recoveryHydration->pending.isEmpty()) {
    return;
  }
  auto state = std::make_shared<IncomingRecoveryState>(m_recoveryHydration->pending.takeFirst());
  if (m_sessions.contains(state->transferId)) {
    QTimer::singleShot(0, this, [this] { hydrateNextRecoveryState(); });
    return;
  }
  const auto fingerprint = m_peerFingerprintProvider ? m_peerFingerprintProvider(state->peerDeviceId) : std::nullopt;
  if (!m_localDeviceId.has_value() || state->localDeviceId != *m_localDeviceId ||
      (m_trustChecker && !m_trustChecker(state->peerDeviceId)) || !fingerprint.has_value() ||
      *fingerprint != state->peerFingerprintSha256) {
    Q_EMIT recoveryIssue(QStringLiteral("Incoming recovery state failed local identity or trust validation"));
    QTimer::singleShot(0, this, [this] { hydrateNextRecoveryState(); });
    return;
  }
  const quint64 epoch = m_recoveryHydration->epoch;
  auto *fileSafety = &m_fileSafety;
  m_recoveryHydration->validationRunning = true;
  auto *watcher = new QFutureWatcher<IncomingRecoveryValidationResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, epoch, state, watcher] {
    auto result = watcher->result();
    watcher->deleteLater();
    if (m_recoveryHydration == nullptr || epoch != m_recoveryHydration->epoch) {
      return;
    }
    m_recoveryHydration->validationRunning = false;
    const auto fingerprint = m_peerFingerprintProvider ? m_peerFingerprintProvider(state->peerDeviceId) : std::nullopt;
    if (!result.ok() || !fingerprint.has_value() || *fingerprint != state->peerFingerprintSha256 ||
        (m_trustChecker && !m_trustChecker(state->peerDeviceId))) {
      Q_EMIT recoveryIssue(
          result.ok() ? QStringLiteral("Incoming recovery peer changed during validation")
                      : QStringLiteral("Incoming recovery state was not hydrated: %1").arg(result.diagnostic)
      );
      QTimer::singleShot(0, this, [this] { hydrateNextRecoveryState(); });
      return;
    }
    auto *session = new Session(
        state->peerDeviceId, state->peerDisplayName, true, false, state->offer,
        state->negotiatedCapabilities
    );
    const auto received = session->stateMachine.receiveIncoming(state->offer);
    const bool accepted = received.ok() && session->stateMachine.acceptIncoming(
                                              state->receiveOptions.conflictPolicy,
                                              logicalDestination(state->receiveOptions.destinationRoot),
                                              std::numeric_limits<quint64>::max(),
                                              state->receiveOptions.acceptanceOrigin
                                          ).ok();
    if (!accepted) {
      delete session;
      Q_EMIT recoveryIssue(QStringLiteral("Incoming recovery state could not restore accepted offer"));
      QTimer::singleShot(0, this, [this] { hydrateNextRecoveryState(); });
      return;
    }
    session->receiveOptions = state->receiveOptions;
    session->resumeNegotiated = state->negotiatedCapabilities.features.contains(QStringLiteral("resume.v1"));
    session->pipelineSnapshot.state = TransferState::Interrupted;
    session->pipelineSnapshot.progress.completedBytes = result.completedBytes;
    session->pipelineSnapshot.progress.completedFiles = result.completedFiles;
    session->pipelineSnapshot.canPause = false;
    session->pipelineSnapshot.canResume = true;
    session->pipelineSnapshot.canCancel = true;
    m_sessions.insert(state->transferId, session);
    Q_EMIT transferAdded(session->pipelineSnapshot);
    QTimer::singleShot(0, this, [this] { hydrateNextRecoveryState(); });
  });
  watcher->setFuture(QtConcurrent::run(&m_workerPool, [state, fileSafety] {
    IncomingRecoveryValidationResult result;
    const auto safety = fileSafety->verifyReceiveRoot({.receiveRoot = state->receiveOptions.destinationRoot});
    if (!safety.ok()) {
      result.diagnostic = safety.diagnostic;
      return result;
    }
    ResumeStore store(
        QDir(state->receiveOptions.destinationRoot).filePath(QStringLiteral(".incoming/resume-active"))
    );
    const QString resumeStatePath = store.statePath(state->transferId);
    const auto resumeNoLinks = fileSafety->verifyNoLinkTraversal(
        {.receiveRoot = state->receiveOptions.destinationRoot, .candidatePath = resumeStatePath}
    );
    if (!resumeNoLinks.ok()) {
      result.diagnostic = resumeNoLinks.diagnostic;
      return result;
    }
    auto loaded = store.load(state->transferId);
    if (!loaded.ok() || loaded.state->peerDeviceId != state->peerDeviceId ||
        loaded.state->manifestSha256 != state->offer.manifestSha256 ||
        loaded.state->direction != ResumeDirection::Receiving) {
      result.diagnostic = loaded.ok() ? QStringLiteral("incoming resume state binding changed") : loaded.diagnostic;
      return result;
    }
    if (loaded.schemaVersion == kLegacyResumeStateSchemaVersion) {
      result.diagnostic = QStringLiteral("legacy v1 resume state is not eligible for process hydration");
      return result;
    }
    QHash<QByteArray, qsizetype> fileIndexes;
    for (qsizetype index = 0; index < loaded.state->files.size(); ++index) {
      auto &file = loaded.state->files[index];
      const QByteArray id = file.fileId.toBytes();
      if (fileIndexes.contains(id)) {
        result.diagnostic = QStringLiteral("incoming resume state has duplicate files");
        return result;
      }
      fileIndexes.insert(id, index);
    }
    QHash<QByteArray, qsizetype> targetIndexes;
    for (qsizetype index = 0; index < loaded.state->resolvedTargets.size(); ++index) {
      const QByteArray id = loaded.state->resolvedTargets[index].fileId.toBytes();
      if (targetIndexes.contains(id) || fileIndexes.contains(id)) {
        result.diagnostic = QStringLiteral("incoming resume state has duplicate resolved targets");
        return result;
      }
      targetIndexes.insert(id, index);
    }
    qsizetype matchedFiles = 0;
    qsizetype matchedTargets = 0;
    bool incompleteSeen = false;
    for (const auto &entry : std::as_const(state->entries)) {
      if (entry.type != ManifestEntryType::File) {
        continue;
      }
      const auto targetIndex = targetIndexes.constFind(entry.id.toBytes());
      const ResolvedTargetState *resolved = targetIndex == targetIndexes.cend()
                                                  ? nullptr
                                                  : &loaded.state->resolvedTargets[*targetIndex];
      if (resolved != nullptr) {
        ++matchedTargets;
        if (resolved->size != entry.size || resolved->sha256 != entry.sha256 ||
            resolved->decision == IncomingConflictDecision::CancelTransfer) {
          result.diagnostic = QStringLiteral("incoming resolved target binding changed");
          return result;
        }
        if (resolved->decision != IncomingConflictDecision::Skip) {
          QString absoluteTarget;
          const auto joined = PathPolicy::joinLexicallyUnderRoot(
              state->receiveOptions.destinationRoot, resolved->relativeTargetPath, absoluteTarget
          );
          if (!joined.ok) {
            result.diagnostic = joined.diagnostic;
            return result;
          }
          const auto noLinks = fileSafety->verifyNoLinkTraversal(
              {.receiveRoot = state->receiveOptions.destinationRoot, .candidatePath = absoluteTarget}
          );
          QFileInfo targetInfo(absoluteTarget);
          if (!noLinks.ok() || !targetInfo.exists() || targetInfo.isSymLink() || !targetInfo.isFile() ||
              targetInfo.size() < 0 || static_cast<quint64>(targetInfo.size()) != resolved->size) {
            result.diagnostic = noLinks.ok() ? QStringLiteral("incoming resolved target is missing or changed")
                                             : noLinks.diagnostic;
            return result;
          }
          QFile target(absoluteTarget);
          if (!target.open(QIODevice::ReadOnly)) {
            result.diagnostic = target.errorString();
            return result;
          }
          QCryptographicHash hash(QCryptographicHash::Sha256);
          while (!target.atEnd()) {
            const QByteArray chunk = target.read(1024 * 1024);
            if (chunk.isEmpty() && target.error() != QFileDevice::NoError) {
              result.diagnostic = target.errorString();
              return result;
            }
            hash.addData(chunk);
          }
          if (hash.result() != resolved->sha256) {
            result.diagnostic = QStringLiteral("incoming resolved target hash changed");
            return result;
          }
        }
        result.completedBytes += entry.size;
        ++result.completedFiles;
        continue;
      }
      const auto fileIndex = fileIndexes.constFind(entry.id.toBytes());
      ResumeFileState *matching =
          fileIndex == fileIndexes.cend() ? nullptr : &loaded.state->files[*fileIndex];
      if (matching == nullptr) {
        result.diagnostic = QStringLiteral("incoming resume state is missing a file");
        return result;
      }
      ++matchedFiles;
      if (matching->relativeProtocolPath != entry.relativeProtocolPath || matching->totalBytes != entry.size ||
          matching->durableOffset > entry.size || (incompleteSeen && matching->durableOffset != 0)) {
        result.diagnostic = QStringLiteral("incoming resume offsets changed");
        return result;
      }
      incompleteSeen = true;
      if (matching->durableOffset != 0) {
        QString absolutePart;
        const auto joined = PathPolicy::joinLexicallyUnderRoot(
            state->receiveOptions.destinationRoot, matching->partRelativePath, absolutePart
        );
        const auto noLinks = joined.ok ? fileSafety->verifyNoLinkTraversal(
                                            {.receiveRoot = state->receiveOptions.destinationRoot,
                                             .candidatePath = absolutePart}
                                        )
                                       : FileSafetyResult{
                                             .error = FileSafetyError::DestinationInvalid,
                                             .diagnostic = joined.diagnostic,
                                         };
        const QFileInfo part(absolutePart);
        if (!noLinks.ok() || !part.exists() || part.isSymLink() || part.size() < 0 ||
            static_cast<quint64>(part.size()) != matching->durableOffset) {
          result.diagnostic = noLinks.ok() ? QStringLiteral("incoming partial file does not match durable offset")
                                           : noLinks.diagnostic;
          return result;
        }
      }
    }
    if (matchedFiles != loaded.state->files.size() || matchedTargets != loaded.state->resolvedTargets.size()) {
      result.diagnostic = QStringLiteral("incoming resume state has a different file set");
      return result;
    }
    result.resumeState = std::move(*loaded.state);
    return result;
  }));
}

IncomingTransferRuntime::~IncomingTransferRuntime()
{
  for (auto *session : std::as_const(m_sessions)) {
    if (session != nullptr && session->pipeline != nullptr) {
      session->pipeline->stop();
    }
  }
  qDeleteAll(m_sessions);
  m_sessions.clear();
}

bool IncomingTransferRuntime::receiveOffer(
    const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted,
    const ::relaydesk::transfer::NegotiatedCapabilities &capabilities,
    const ::relaydesk::transfer::TransferOffer &offer, QString *diagnostic
)
{
  return receiveOffer(
      peerDeviceId, std::move(peerDisplayName), peerTrusted, false, capabilities, offer, diagnostic
  );
}

bool IncomingTransferRuntime::receiveOffer(
    const DeviceId &peerDeviceId, QString peerDisplayName, bool peerTrusted, bool peerAllowsAutoAccept,
    const ::relaydesk::transfer::NegotiatedCapabilities &capabilities,
    const ::relaydesk::transfer::TransferOffer &offer, QString *diagnostic
)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (QThread::currentThread() != thread()) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer runtime must be called on its owning thread"));
    return false;
  }
  if (!capabilities.localCanReceiveFiles) {
    setDiagnostic(diagnostic, QStringLiteral("incoming offer requires negotiated file.receive.v1"));
    return false;
  }
  if (m_sessions.contains(offer.transferId)) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer ID is already known"));
    return false;
  }

  auto *session = new Session(
      peerDeviceId, std::move(peerDisplayName), peerTrusted, peerAllowsAutoAccept, offer, capabilities
  );
  session->resumeNegotiated = capabilities.features.contains(QStringLiteral("resume.v1"));
  const auto received = session->stateMachine.receiveIncoming(offer);
  if (!received.ok()) {
    setDiagnostic(diagnostic, received.diagnostic);
    delete session;
    return false;
  }
  m_sessions.insert(offer.transferId, session);
  Q_EMIT incomingOffer({
      .peerDeviceId = session->peer,
      .peerDisplayName = session->peerDisplayName,
      .offer = session->offer,
      .peerTrusted = session->peerTrusted,
      .mayAutoAccept = session->peerTrusted && session->peerAllowsAutoAccept,
  });
  return true;
}

void IncomingTransferRuntime::accept(
    const ::relaydesk::transfer::TransferId &transferId,
    const ::relaydesk::transfer::ReceiveOptions &options
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Incoming transfer ID is unknown")
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  if (snapshot.has_value() && snapshot->state == OfferState::Accepted) {
    publishOperation(transferId, TransferOperation::Accept, TransferOperationOutcome::Idempotent);
    return;
  }
  const bool invalidDestination =
      !options.destinationRoot.isEmpty() && !QDir::isAbsolutePath(options.destinationRoot);
  const bool invalidAutomaticAcceptance =
      options.acceptanceOrigin == AcceptanceOrigin::TrustedDevicePolicy && !session->peerTrusted;
  const bool trustRevoked = session->peerTrusted && !isCurrentlyTrusted(*session);
  if (session->acceptPreflightPending || !snapshot.has_value() ||
      snapshot->state != OfferState::AwaitingLocalDecision || invalidDestination ||
      invalidAutomaticAcceptance || trustRevoked) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState,
        session->acceptPreflightPending
            ? QStringLiteral("Receive-root preflight is already pending")
            : trustRevoked
              ? QStringLiteral("Incoming transfer peer is no longer trusted")
            : QStringLiteral("Incoming transfer cannot be accepted with the supplied state and options")
    );
    return;
  }
  if (options.destinationRoot.isEmpty()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Receive root is empty")
    );
    return;
  }

  session->acceptPreflightPending = true;
  auto *watcher = new QFutureWatcher<AcceptPreflightResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, options, watcher]() mutable {
    auto result = watcher->result();
    watcher->deleteLater();
    finishAcceptPreflight(transferId, std::move(options), std::move(result));
  });
  watcher->setFuture(QtConcurrent::run(
      &m_workerPool, [fileSafety = &m_fileSafety, root = options.destinationRoot]() {
    AcceptPreflightResult result;
    result.safety = fileSafety->verifyReceiveRoot({.receiveRoot = root});
    if (!result.safety.ok()) {
      return result;
    }
    const QStorageInfo storage(root);
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0) {
      result.freeBytes = static_cast<quint64>(storage.bytesAvailable());
    } else {
      result.freeBytes = std::numeric_limits<quint64>::max();
    }
    return result;
  }
  ));
}

void IncomingTransferRuntime::reject(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::RejectReason reason
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Incoming transfer ID is unknown")
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  if (snapshot.has_value() && snapshot->state == OfferState::Rejected) {
    publishOperation(transferId, TransferOperation::Reject, TransferOperationOutcome::Idempotent);
    return;
  }
  if (session->acceptPreflightPending || !snapshot.has_value() ||
      snapshot->state != OfferState::AwaitingLocalDecision) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Incoming transfer cannot be rejected now")
    );
    return;
  }
  const auto rejected = session->stateMachine.rejectIncoming(reason);
  if (!rejected.ok()) {
    publishOperation(
        transferId, TransferOperation::Reject, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, rejected.diagnostic
    );
    return;
  }
  const auto updated = session->stateMachine.snapshot();
  Q_ASSERT(updated.has_value() && updated->rejection.has_value());
  Q_EMIT transferRejected(session->peer, *updated->rejection);
  publishOperation(transferId, TransferOperation::Reject, TransferOperationOutcome::Applied);
}

void IncomingTransferRuntime::resolveIncomingConflict(
    const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId,
    ::relaydesk::transfer::IncomingConflictDecision decision
)
{
  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr || session->pipeline == nullptr || !session->pendingConflict.has_value() ||
      session->pendingConflict->conflictId != conflictId) {
    return;
  }
  // Consume the public identity before waking the worker. Repeated and stale
  // calls are then harmless even if a queued worker callback runs later.
  session->pendingConflict.reset();
  if (!session->pipeline->resolveConflict(conflictId, decision)) {
    return;
  }
}

bool IncomingTransferRuntime::hasPendingIncomingConflict(
    const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId
) const
{
  const auto *session = m_sessions.value(transferId, nullptr);
  return session != nullptr && session->pendingConflict.has_value() &&
         session->pendingConflict->conflictId == conflictId;
}

bool IncomingTransferRuntime::receiveCommand(
    const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame, QString *diagnostic
)
{
  using namespace ::relaydesk::transfer;

  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (QThread::currentThread() != thread()) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer runtime must be called on its owning thread"));
    return false;
  }
  if (frame.streamId != 0 || !frame.payload.isEmpty()) {
    setDiagnostic(diagnostic, QStringLiteral("transfer command must use stream zero without payload"));
    return false;
  }
  const auto decoded = TransferCommandCodec::decode(frame.version, frame.type, frame.metadata);
  if (!decoded.ok()) {
    setDiagnostic(diagnostic, decoded.diagnostic);
    return false;
  }

  const auto transferId = std::visit([](const auto &command) { return command.transferId; }, *decoded.message);
  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr || session->peer != peerDeviceId) {
    setDiagnostic(diagnostic, QStringLiteral("transfer command does not match an active incoming transfer"));
    return false;
  }

  const auto publish = [this, session] {
    Q_EMIT transferChanged(session->pipelineSnapshot);
  };
  if (std::holds_alternative<TransferPauseMessage>(*decoded.message)) {
    if (session->pipeline == nullptr ||
        (session->pipelineSnapshot.state != TransferState::Transferring &&
         session->pipelineSnapshot.state != TransferState::Queued)) {
      setDiagnostic(diagnostic, QStringLiteral("incoming transfer cannot be paused in its current state"));
      return false;
    }
    session->pipeline->pause();
    session->pipelineSnapshot.state = TransferState::Paused;
    session->pipelineSnapshot.canPause = false;
    session->pipelineSnapshot.canResume = true;
    publish();
    return true;
  }
  if (std::holds_alternative<TransferResumeMessage>(*decoded.message)) {
    if (session->pipeline == nullptr || session->pipelineSnapshot.state != TransferState::Paused) {
      setDiagnostic(diagnostic, QStringLiteral("incoming transfer cannot be resumed in its current state"));
      return false;
    }
    session->pipeline->resume();
    session->pipelineSnapshot.state = TransferState::Transferring;
    session->pipelineSnapshot.canPause = true;
    session->pipelineSnapshot.canResume = false;
    publish();
    return true;
  }

  const auto &cancel = std::get<TransferCancelMessage>(*decoded.message);
  if (session->pipelineSnapshot.state == TransferState::Cancelling ||
      session->pipelineSnapshot.state == TransferState::Cancelled) {
    return true;
  }
  if (session->pipeline == nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer cannot be cancelled in its current state"));
    return false;
  }
  const auto pipeline = std::move(session->pipeline);
  session->pendingConflict.reset();
  pipeline->stop();
  const quint64 generation = ++session->pipelineGeneration;
  session->pipelineSnapshot.state = TransferState::Cancelling;
  session->pipelineSnapshot.canPause = false;
  session->pipelineSnapshot.canResume = false;
  session->pipelineSnapshot.canCancel = false;
  publish();

  const QString destinationRoot = session->receiveOptions.destinationRoot;
  auto *recoveryStore = m_recoveryStore;
  const auto pipelineHolder = std::make_shared<std::shared_ptr<ReceivePipeline>>(pipeline);
  auto *watcher = new QFutureWatcher<CancelCleanupResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, generation, watcher]() {
    const auto result = watcher->result();
    watcher->deleteLater();
    auto *current = m_sessions.value(transferId, nullptr);
    if (current == nullptr || current->pipelineGeneration != generation ||
        current->pipelineSnapshot.state != ::relaydesk::transfer::TransferState::Cancelling) {
      return;
    }
    if (!result.ok()) {
      current->pipelineSnapshot.state = ::relaydesk::transfer::TransferState::Failed;
      current->pipelineSnapshot.errorCode = ::relaydesk::transfer::TransferErrorCode::InternalError;
      current->pipelineSnapshot.canRetry = false;
      current->pipelineSnapshot.finishedUtc = QDateTime::currentDateTimeUtc();
      Q_EMIT transferChanged(current->pipelineSnapshot);
      Q_EMIT pipelineFailed(transferId, current->pipelineSnapshot.errorCode, result.diagnostic);
      return;
    }
    current->pipelineSnapshot.state = ::relaydesk::transfer::TransferState::Cancelled;
    current->pipelineSnapshot.finishedUtc = QDateTime::currentDateTimeUtc();
    Q_EMIT transferChanged(current->pipelineSnapshot);
  });
  watcher->setFuture(QtConcurrent::run(
      &m_workerPool,
      [pipelineHolder, destinationRoot, transferId, keepPartial = cancel.keepPartial, recoveryStore]() {
        (*pipelineHolder)->waitForFinished();
        pipelineHolder->reset();
        CancelCleanupResult result;
        if (keepPartial) {
          return result;
        }
        const auto stagingRoot = QDir(destinationRoot).filePath(
            QStringLiteral(".incoming/%1").arg(transferId.toString())
        );
        if (QFileInfo::exists(stagingRoot) && !QDir(stagingRoot).removeRecursively()) {
          result.diagnostic = QStringLiteral("could not remove cancelled transfer staging directory");
          return result;
        }
        ResumeStore store(QDir(destinationRoot).filePath(QStringLiteral(".incoming/resume-active")));
        const auto removed = store.remove(transferId);
        if (!removed.ok()) {
          result.diagnostic = removed.diagnostic;
          return result;
        }
        if (recoveryStore != nullptr) {
          const auto recoveryRemoved = recoveryStore->removeIncoming(transferId);
          if (!recoveryRemoved.ok()) {
            result.diagnostic = recoveryRemoved.diagnostic;
          }
        }
        return result;
      }
  ));
  return true;
}

bool IncomingTransferRuntime::applyLocalCommand(
    const ::relaydesk::transfer::TransferCommandMessage &command, DeviceId *peerDeviceId,
    QString *diagnostic, ::relaydesk::transfer::TransferOperationOutcome *outcome
)
{
  using namespace ::relaydesk::transfer;

  const auto transferId = std::visit([](const auto &typed) { return typed.transferId; }, command);
  const auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer is unknown"));
    return false;
  }
  if (outcome != nullptr) {
    *outcome = std::holds_alternative<TransferCancelMessage>(command) &&
                       (session->pipelineSnapshot.state == TransferState::Cancelling ||
                        session->pipelineSnapshot.state == TransferState::Cancelled)
                   ? TransferOperationOutcome::Idempotent
                   : TransferOperationOutcome::Applied;
  }
  QString encodeDiagnostic;
  Frame frame{
      .type = messageType(command),
      .flags = AckRequired,
      .metadata = TransferCommandCodec::encode(command, &encodeDiagnostic),
  };
  if (frame.metadata.isEmpty()) {
    setDiagnostic(diagnostic, std::move(encodeDiagnostic));
    return false;
  }
  if (!receiveCommand(session->peer, frame, diagnostic)) {
    return false;
  }
  if (peerDeviceId != nullptr) {
    *peerDeviceId = session->peer;
  }
  return true;
}

bool IncomingTransferRuntime::validateLocalCommand(
    const ::relaydesk::transfer::TransferCommandMessage &command, DeviceId *peerDeviceId,
    QString *diagnostic
) const
{
  using namespace ::relaydesk::transfer;
  const auto transferId = std::visit([](const auto &typed) { return typed.transferId; }, command);
  const auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer is unavailable"));
    return false;
  }
  const auto state = session->pipelineSnapshot.state;
  if (std::holds_alternative<TransferCancelMessage>(command) &&
      (state == TransferState::Cancelling || state == TransferState::Cancelled)) {
    if (peerDeviceId != nullptr) {
      *peerDeviceId = session->peer;
    }
    return true;
  }
  if (session->pipeline == nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer is unavailable"));
    return false;
  }
  const bool valid = std::holds_alternative<TransferPauseMessage>(command)
                         ? (state == TransferState::Queued || state == TransferState::Transferring)
                         : std::holds_alternative<TransferResumeMessage>(command)
                               ? state == TransferState::Paused
                               : state != TransferState::Cancelling && !TransferControlStateMachine::isTerminal(state);
  if (!valid) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer cannot apply this control in its current state"));
    return false;
  }
  if (peerDeviceId != nullptr) {
    *peerDeviceId = session->peer;
  }
  return true;
}

bool IncomingTransferRuntime::enqueueFrame(
    const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
    QString *diagnostic
)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  const auto transferId = decodedDataTransferId(frame, diagnostic);
  if (!transferId.has_value()) {
    return false;
  }
  auto *session = m_sessions.value(*transferId, nullptr);
  if (session == nullptr || session->peer != peerDeviceId) {
    setDiagnostic(diagnostic, QStringLiteral("incoming frame does not match an accepted receive session"));
    return false;
  }
  if (session->pipelineSnapshot.state == ::relaydesk::transfer::TransferState::Cancelling ||
      session->pipelineSnapshot.state == ::relaydesk::transfer::TransferState::Cancelled) {
      return true;
  }
  if (session->pipeline == nullptr || !isReceivePipelineActive(session->pipelineSnapshot.state)) {
    setDiagnostic(diagnostic, QStringLiteral("incoming frame has no active receive pipeline"));
    return false;
  }
  return session->pipeline->enqueue(frame, diagnostic);
}

bool IncomingTransferRuntime::receiveResumeQuery(
    const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
    QString *diagnostic
)
{
  using namespace ::relaydesk::transfer;

  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (QThread::currentThread() != thread()) {
    setDiagnostic(diagnostic, QStringLiteral("incoming transfer runtime must be called on its owning thread"));
    return false;
  }
  const auto decoded = ResumeMessageCodec::decode(frame.version, frame.type, frame.metadata);
  const auto *query = decoded.ok() ? std::get_if<ResumeQueryMessage>(&*decoded.message) : nullptr;
  if (query == nullptr) {
    setDiagnostic(diagnostic, decoded.diagnostic);
    return false;
  }
  auto *session = m_sessions.value(query->transferId, nullptr);
  if (session == nullptr || session->peer != peerDeviceId || !session->resumeNegotiated ||
      query->manifestSha256 != session->offer.manifestSha256) {
    setDiagnostic(diagnostic, QStringLiteral("resume query is not bound to an interrupted negotiated transfer"));
    return false;
  }

  if (session->pipelineSnapshot.state == TransferState::Resuming &&
      session->lastResumeResponse.has_value()) {
    QString encodeDiagnostic;
    Frame response{
        .type = MessageType::ResumeResponse,
        .flags = Response,
        .metadata = ResumeMessageCodec::encode(
            ResumeControlMessage{*session->lastResumeResponse}, &encodeDiagnostic
        ),
    };
    if (response.metadata.isEmpty()) {
      setDiagnostic(diagnostic, std::move(encodeDiagnostic));
      return false;
    }
    Q_EMIT responseReady(peerDeviceId, std::move(response));
    return true;
  }
  if (session->pipelineSnapshot.state != TransferState::Interrupted || session->pipeline != nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("resume query requires an interrupted receive pipeline"));
    return false;
  }

  ResumeStore store(
      QDir(session->receiveOptions.destinationRoot).filePath(QStringLiteral(".incoming/resume-active"))
  );
  const QString resumeStatePath = store.statePath(query->transferId);
  const auto resumeNoLinks = m_fileSafety.verifyNoLinkTraversal(
      {.receiveRoot = session->receiveOptions.destinationRoot, .candidatePath = resumeStatePath}
  );
  if (!resumeNoLinks.ok()) {
    setDiagnostic(diagnostic, resumeNoLinks.diagnostic);
    return false;
  }
  const auto loaded = store.load(query->transferId);
  if (!loaded.ok() || loaded.state->peerDeviceId != peerDeviceId) {
    setDiagnostic(
        diagnostic,
        loaded.ok() ? QStringLiteral("resume state belongs to a different peer") : loaded.diagnostic
    );
    return false;
  }
  const auto built = ResumeNegotiator::buildResponse(store, *query);
  if (!built.ok()) {
    setDiagnostic(diagnostic, built.diagnostic);
    return false;
  }
  QString encodeDiagnostic;
  Frame response{
      .type = MessageType::ResumeResponse,
      .flags = Response,
      .metadata = ResumeMessageCodec::encode(
          ResumeControlMessage{*built.response}, &encodeDiagnostic
      ),
  };
  if (response.metadata.isEmpty()) {
    setDiagnostic(diagnostic, std::move(encodeDiagnostic));
    return false;
  }

  session->lastResumeResponse = *built.response;
  session->pipelineSnapshot.state = TransferState::Resuming;
  session->pipeline = std::make_shared<ReceivePipeline>(
      *this, session->peer, session->offer, session->receiveOptions, m_fileSafety,
      m_workerPool, *loaded.state, session->pipelineGeneration, std::nullopt, m_recoveryStore
  );
  Q_EMIT transferChanged(session->pipelineSnapshot);
  Q_EMIT responseReady(peerDeviceId, std::move(response));
  return true;
}

void IncomingTransferRuntime::peerDisconnected(const DeviceId &peerDeviceId)
{
  for (auto *session : std::as_const(m_sessions)) {
    if (session == nullptr || session->peer != peerDeviceId) {
      continue;
    }
    session->pendingConflict.reset();
    if (session->pipeline == nullptr ||
        !isReceivePipelineActive(session->pipelineSnapshot.state)) {
      continue;
    }
    session->pipeline->stop();
    ++session->pipelineGeneration;
    session->pipeline.reset();
    session->lastResumeResponse.reset();
    session->pipelineSnapshot.state = ::relaydesk::transfer::TransferState::Interrupted;
    session->pipelineSnapshot.canCancel = true;
    Q_EMIT transferChanged(session->pipelineSnapshot);
  }
}

bool IncomingTransferRuntime::contains(
    const ::relaydesk::transfer::TransferId &transferId
) const
{
  return m_sessions.contains(transferId);
}

QList<::relaydesk::transfer::TransferSnapshot> IncomingTransferRuntime::activeTransfers() const
{
  QList<::relaydesk::transfer::TransferSnapshot> result;
  for (const auto *session : m_sessions) {
    if (session != nullptr &&
        (session->pipeline != nullptr ||
         session->pipelineSnapshot.state == ::relaydesk::transfer::TransferState::Cancelling ||
         session->pipelineSnapshot.state == ::relaydesk::transfer::TransferState::Interrupted) &&
        !::relaydesk::transfer::TransferControlStateMachine::isTerminal(
            session->pipelineSnapshot.state
        )) {
      result.append(session->pipelineSnapshot);
    }
  }
  return result;
}

void IncomingTransferRuntime::finishAcceptPreflight(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::ReceiveOptions options, AcceptPreflightResult result
)
{
  using namespace ::relaydesk::transfer;

  auto *session = m_sessions.value(transferId, nullptr);
  if (session == nullptr || !session->acceptPreflightPending) {
    return;
  }
  session->acceptPreflightPending = false;
  if (session->peerTrusted && !isCurrentlyTrusted(*session)) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Incoming transfer peer is no longer trusted")
    );
    return;
  }
  if (!result.safety.ok()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, result.safety.diagnostic
    );
    return;
  }
  const auto accepted = session->stateMachine.acceptIncoming(
      options.conflictPolicy, logicalDestination(options.destinationRoot), result.freeBytes,
      options.acceptanceOrigin
  );
  if (!accepted.ok()) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, accepted.diagnostic
    );
    return;
  }
  const auto snapshot = session->stateMachine.snapshot();
  Q_ASSERT(snapshot.has_value() && snapshot->acceptance.has_value());
  session->receiveOptions = options;
  session->pipelineSnapshot.state = TransferState::Queued;
  std::optional<IncomingRecoveryState> recoveryDescriptor;
  if (m_recoveryStore != nullptr && m_localDeviceId.has_value() && m_peerFingerprintProvider) {
    const auto peerFingerprint = m_peerFingerprintProvider(session->peer);
    if (peerFingerprint.has_value()) {
      recoveryDescriptor = IncomingRecoveryState{
          .transferId = session->offer.transferId,
          .localDeviceId = *m_localDeviceId,
          .peerDeviceId = session->peer,
          .peerFingerprintSha256 = *peerFingerprint,
          .peerDisplayName = session->peerDisplayName,
          .offer = session->offer,
          .receiveOptions = options,
          .negotiatedCapabilities = session->capabilities,
      };
    }
  }
  session->pipeline = std::make_shared<ReceivePipeline>(
      *this, session->peer, session->offer, options, m_fileSafety, m_workerPool,
      std::nullopt, session->pipelineGeneration, std::move(recoveryDescriptor), m_recoveryStore
  );
  Q_EMIT transferAdded(session->pipelineSnapshot);
  Q_EMIT transferAccepted(session->peer, *snapshot->acceptance);
  publishOperation(transferId, TransferOperation::Accept, TransferOperationOutcome::Applied);
}

bool IncomingTransferRuntime::isCurrentlyTrusted(const Session &session) const
{
  return !m_trustChecker || m_trustChecker(session.peer);
}

void IncomingTransferRuntime::publishOperation(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::TransferOperation operation,
    ::relaydesk::transfer::TransferOperationOutcome outcome,
    ::relaydesk::transfer::TransferOperationError error, QString diagnostic
)
{
  Q_EMIT transferOperationFinished({
      .transferId = transferId,
      .operation = operation,
      .outcome = outcome,
      .error = error,
      .diagnostic = std::move(diagnostic),
  });
}

} // namespace deskflow::relaydesk
