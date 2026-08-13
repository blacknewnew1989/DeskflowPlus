/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/IncomingTransferRuntime.h"

#include "relaydesk/app/IncomingFileReceiverWorker.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"

#include <QDir>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QStorageInfo>
#include <QThread>
#include <QThreadPool>
#include <QWaitCondition>
#include <QtConcurrentRun>

#include <atomic>
#include <limits>
#include <utility>

namespace deskflow::relaydesk {

struct IncomingTransferRuntime::Session
{
  Session(
      DeviceId peerDeviceId, QString peerName, bool trusted,
      ::relaydesk::transfer::TransferOffer incomingOffer,
      ::relaydesk::transfer::NegotiatedCapabilities capabilities
  )
      : peer(std::move(peerDeviceId)), peerDisplayName(std::move(peerName)), peerTrusted(trusted),
        offer(std::move(incomingOffer)), stateMachine(std::move(capabilities))
  {
  }

  DeviceId peer;
  QString peerDisplayName;
  bool peerTrusted = false;
  ::relaydesk::transfer::TransferOffer offer;
  ::relaydesk::transfer::TransferOfferStateMachine stateMachine;
  bool acceptPreflightPending = false;
  ::relaydesk::transfer::ReceiveOptions receiveOptions;
  std::shared_ptr<ReceivePipeline> pipeline;
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
         state == ::relaydesk::transfer::TransferState::Transferring;
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
      QThreadPool &pool
  )
      : m_runtime(&runtime), m_peer(std::move(peer)), m_offer(std::move(offer)),
        m_options(std::move(options)), m_fileSafety(fileSafety), m_reassembler(
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

private:
  void run()
  {
    for (;;) {
      ::relaydesk::transfer::Frame frame;
      {
        QMutexLocker lock(&m_mutex);
        while (m_frames.isEmpty() && !m_stopping) {
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
      if (!m_manifestReady) {
        fail(TransferErrorCode::UnsafePath, diagnostic);
      }
      if (m_manifestReady && m_offer.fileCount == 0) {
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
      if (m_receiver != nullptr) {
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
      const auto result = m_receiver->begin({
          .receiveRoot = m_options.destinationRoot,
          .entry = *entry,
          .begin = *begin,
          .manifestSha256 = m_offer.manifestSha256,
          .conflictPolicy = m_options.conflictPolicy,
      });
      if (!result.ok()) {
        sendFileResult(result);
        fail(receiverErrorCode(result.error), result.diagnostic);
        return false;
      }
      publishProgress(m_completedBytes, m_completedFiles, entry->relativeProtocolPath);
      return true;
    }
    if (const auto *chunk = std::get_if<FileChunkMessage>(&*decoded.message)) {
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
      publishProgress(
          m_completedBytes + snapshot.receivedBytes, m_completedFiles,
          snapshot.relativeProtocolPath
      );
      return true;
    }
    if (const auto *end = std::get_if<FileEndMessage>(&*decoded.message)) {
      if (m_receiver == nullptr) {
        fail(TransferErrorCode::InternalError, QStringLiteral("FILE_END arrived without FILE_BEGIN"));
        return false;
      }
      const auto result = m_receiver->finish(*end);
      sendFileResult(result);
      if (!result.ok()) {
        fail(receiverErrorCode(result.error), result.diagnostic);
        return false;
      }
      const auto snapshot = m_receiver->snapshot();
      m_completedBytes += snapshot.expectedSize;
      ++m_completedFiles;
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
      const auto safe = m_fileSafety.verifyNoLinkTraversal(
          {.receiveRoot = m_options.destinationRoot, .candidatePath = absolutePath}
      );
      if (!safe.ok()) {
        diagnostic = safe.diagnostic;
        return false;
      }
      if (!QDir().mkpath(absolutePath)) {
        diagnostic = QStringLiteral("could not create accepted manifest directory");
        return false;
      }
    }
    return true;
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
        [id = m_offer.transferId, peer = m_peer,
         response = std::move(response)](IncomingTransferRuntime &runtime) mutable {
          const auto *session = runtime.m_sessions.value(id, nullptr);
          if (session == nullptr || !isReceivePipelineActive(session->pipelineSnapshot.state)) {
            return;
          }
          Q_EMIT runtime.responseReady(peer, std::move(response));
        }
    );
  }

  void publishProgress(quint64 bytes, quint64 files, QString path)
  {
    invoke([id = m_offer.transferId, bytes, files, path = std::move(path)](IncomingTransferRuntime &runtime) {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr) {
        return;
      }
      auto snapshot = session->pipelineSnapshot;
      if (!isReceivePipelineActive(snapshot.state)) {
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
    invoke([id = m_offer.transferId](IncomingTransferRuntime &runtime) {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr) {
        return;
      }
      auto snapshot = session->pipelineSnapshot;
      if (!isReceivePipelineActive(snapshot.state)) {
        return;
      }
      snapshot.state = ::relaydesk::transfer::TransferState::Completed;
      snapshot.progress.completedBytes = snapshot.progress.totalBytes;
      snapshot.progress.completedFiles = snapshot.progress.totalFiles;
      snapshot.canCancel = false;
      snapshot.finishedUtc = QDateTime::currentDateTimeUtc();
      session->pipelineSnapshot = snapshot;
      Q_EMIT runtime.transferChanged(snapshot);
    });
  }

  void fail(::relaydesk::transfer::TransferErrorCode code, QString diagnostic)
  {
    invoke([id = m_offer.transferId, code, diagnostic = std::move(diagnostic)](
               IncomingTransferRuntime &runtime
           ) mutable {
      auto *session = runtime.m_sessions.value(id, nullptr);
      if (session == nullptr) {
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
    });
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
  ::relaydesk::transfer::ManifestPageReassembler m_reassembler;
  QList<::relaydesk::transfer::ManifestEntry> m_entries;
  std::unique_ptr<IncomingFileReceiverWorker> m_receiver;
  quint64 m_completedBytes = 0;
  quint64 m_completedFiles = 0;
  quint32 m_activeStreamId = 0;
  bool m_manifestReady = false;
  QMutex m_mutex;
  QWaitCondition m_ready;
  QQueue<::relaydesk::transfer::Frame> m_frames;
  quint64 m_queuedBytes = 0;
  bool m_stopping = false;
  QFuture<void> m_future;
};

IncomingTransferRuntime::IncomingTransferRuntime(
    IPlatformFileSafety &fileSafety, QThreadPool &workerPool, QObject *parent
)
    : QObject(parent), m_fileSafety(fileSafety), m_workerPool(workerPool)
{
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
      peerDeviceId, std::move(peerDisplayName), peerTrusted, offer, capabilities
  );
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
      .mayAutoAccept = false,
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
  if (session->acceptPreflightPending || !snapshot.has_value() ||
      snapshot->state != OfferState::AwaitingLocalDecision || invalidDestination ||
      invalidAutomaticAcceptance) {
    publishOperation(
        transferId, TransferOperation::Accept, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState,
        session->acceptPreflightPending
            ? QStringLiteral("Receive-root preflight is already pending")
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

bool IncomingTransferRuntime::enqueueFrame(
    const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
    QString *diagnostic
)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  Session *session = nullptr;
  for (auto *candidate : std::as_const(m_sessions)) {
    if (candidate == nullptr || candidate->peer != peerDeviceId || candidate->pipeline == nullptr ||
        !isReceivePipelineActive(candidate->pipelineSnapshot.state)) {
      continue;
    }
    if (session != nullptr) {
      setDiagnostic(
          diagnostic,
          QStringLiteral("peer has multiple active incoming sessions; frame routing is ambiguous")
      );
      return false;
    }
    session = candidate;
  }
  if (session == nullptr) {
    setDiagnostic(diagnostic, QStringLiteral("incoming frame has no accepted receive session"));
    return false;
  }
  return session->pipeline->enqueue(frame, diagnostic);
}

void IncomingTransferRuntime::peerDisconnected(const DeviceId &peerDeviceId)
{
  for (auto *session : std::as_const(m_sessions)) {
    if (session == nullptr || session->peer != peerDeviceId || session->pipeline == nullptr ||
        !isReceivePipelineActive(session->pipelineSnapshot.state)) {
      continue;
    }
    session->pipeline->stop();
    session->pipelineSnapshot.state = ::relaydesk::transfer::TransferState::Interrupted;
    session->pipelineSnapshot.canCancel = true;
    Q_EMIT transferChanged(session->pipelineSnapshot);
  }
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
  session->pipeline = std::make_shared<ReceivePipeline>(
      *this, session->peer, session->offer, options, m_fileSafety, m_workerPool
  );
  Q_EMIT transferAdded(session->pipelineSnapshot);
  Q_EMIT transferAccepted(session->peer, *snapshot->acceptance);
  publishOperation(transferId, TransferOperation::Accept, TransferOperationOutcome::Applied);
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
