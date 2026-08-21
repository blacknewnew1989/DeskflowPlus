/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/IncomingTransferRuntime.h"
#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/filetransport/FileTlsFrameSink.h"
#include "relaydesk/transfer/ControlMessageCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/ResumeMessageCodec.h"
#include "relaydesk/transfer/TransferCommandCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferOfferStateMachine.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#if defined(Q_OS_WIN)
#include "relaydesk/platform/WindowsFileSafety.h"
#elif defined(Q_OS_MACOS)
#include "relaydesk/platform/MacFileSafety.h"
#endif

#include <QAbstractSocket>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace deskflow::relaydesk {
namespace {
using ::relaydesk::transfer::CapabilityCodec;
using ::relaydesk::transfer::CapabilityNegotiator;
using ::relaydesk::transfer::Frame;
using ::relaydesk::transfer::MessageType;
using ::relaydesk::transfer::SenderBackpressureLimits;
using ::relaydesk::transfer::SenderFrameSinkResult;
using ::relaydesk::transfer::SenderFrameSinkStatus;
using ::relaydesk::transfer::SenderPumpResult;
using ::relaydesk::transfer::TransferFrameSink;
using ::relaydesk::transfer::TransferErrorCode;
using ::relaydesk::transfer::TransferSenderPump;

quint64 frameMemoryBytes(const Frame &frame) noexcept
{
  const quint64 metadataBytes = static_cast<quint64>(frame.metadata.size());
  const quint64 payloadBytes = static_cast<quint64>(frame.payload.size());
  const quint64 fixedBytes = static_cast<quint64>(::relaydesk::transfer::kFixedHeaderBytes);
  const quint64 maximum = std::numeric_limits<quint64>::max();
  if (metadataBytes > maximum - fixedBytes || payloadBytes > maximum - fixedBytes - metadataBytes) {
    return maximum;
  }
  return fixedBytes + metadataBytes + payloadBytes;
}

SenderBackpressureLimits senderBackpressureLimits(const FileTlsConnection &connection) noexcept
{
  const quint64 highWaterBytes = connection.writeHighWaterBytes();
  return {
      .highWaterBytes = highWaterBytes,
      .lowWaterBytes = highWaterBytes == 0
                           ? 0
                           : std::min(::relaydesk::transfer::kDefaultSenderLowWaterBytes, highWaterBytes - 1),
  };
}

// The worker sees only this bounded immutable-frame bridge. The app/socket
// owning thread drains its one accepted frame through FileTlsFrameSink.
class SenderFrameBridge final : public TransferFrameSink
{
public:
  explicit SenderFrameBridge(SenderBackpressureLimits limits) : m_limits(limits)
  {
  }

  [[nodiscard]] quint64 queuedBytes() const noexcept override
  {
    const QMutexLocker lock(&m_mutex);
    const quint64 maximum = std::numeric_limits<quint64>::max();
    return m_frameBytes > maximum - m_transportQueuedBytes ? maximum : m_transportQueuedBytes + m_frameBytes;
  }

  [[nodiscard]] SenderFrameSinkResult submit(const Frame &frame) override
  {
    const quint64 bytes = frameMemoryBytes(frame);
    const QMutexLocker lock(&m_mutex);
    if (m_frame.has_value()) {
      return {
          .status = SenderFrameSinkStatus::Failed,
          .diagnostic = QStringLiteral("sender frame bridge already contains an undrained frame"),
      };
    }
    if (m_limits.highWaterBytes == 0 || bytes > m_limits.highWaterBytes) {
      return {
          .status = SenderFrameSinkStatus::Failed,
          .diagnostic = QStringLiteral("sender frame exceeds the configured TLS write high-water limit"),
      };
    }
    if (m_transportQueuedBytes > m_limits.highWaterBytes - bytes) {
      return {
          .status = SenderFrameSinkStatus::Backpressured,
          .diagnostic = QStringLiteral("sender frame bridge is waiting for TLS write capacity"),
      };
    }
    m_frame = frame;
    m_frameBytes = bytes;
    return {.status = SenderFrameSinkStatus::Accepted};
  }

  void updateTransportQueuedBytes(quint64 bytes) noexcept
  {
    const QMutexLocker lock(&m_mutex);
    m_transportQueuedBytes = bytes;
  }

  [[nodiscard]] std::optional<Frame> pendingFrame() const
  {
    const QMutexLocker lock(&m_mutex);
    return m_frame;
  }

  void consumePendingFrame() noexcept
  {
    const QMutexLocker lock(&m_mutex);
    m_frame.reset();
    m_frameBytes = 0;
  }

private:
  SenderBackpressureLimits m_limits;
  mutable QMutex m_mutex;
  quint64 m_transportQueuedBytes = 0;
  quint64 m_frameBytes = 0;
  std::optional<Frame> m_frame;
};

struct SenderWorkResult
{
  SenderPumpResult result;
};

class SenderWorkerState final
{
public:
  SenderWorkerState(
      ::relaydesk::transfer::TransferSenderRequest request, SenderBackpressureLimits limits
  )
      : m_bridge(limits), m_pump(std::move(request), m_bridge, limits), m_limits(limits)
  {
  }

  [[nodiscard]] SenderWorkResult produce()
  {
    return {.result = m_pump.pump()};
  }

  void updateTransportQueuedBytes(quint64 bytes) noexcept
  {
    m_bridge.updateTransportQueuedBytes(bytes);
  }

  [[nodiscard]] bool readyToPump() const noexcept
  {
    return !m_pump.paused() || m_bridge.queuedBytes() <= m_limits.lowWaterBytes;
  }

  [[nodiscard]] std::optional<Frame> pendingFrame() const
  {
    return m_bridge.pendingFrame();
  }

  void consumePendingFrame() noexcept
  {
    m_bridge.consumePendingFrame();
  }

private:
  SenderFrameBridge m_bridge;
  TransferSenderPump m_pump;
  SenderBackpressureLimits m_limits;
};

void setDiagnostic(QString *output, const QString &diagnostic)
{
  if (output != nullptr) {
    *output = diagnostic;
  }
}

std::optional<QHostAddress> preferredAddress(const QList<QHostAddress> &addresses)
{
  for (const auto &address : addresses) {
    if (!address.isNull() && !address.isMulticast() && !address.isBroadcast() &&
        address.protocol() == QAbstractSocket::IPv4Protocol) {
      return address;
    }
  }
  return std::nullopt;
}

std::unique_ptr<IPlatformFileSafety> createPlatformFileSafety()
{
#if defined(Q_OS_WIN)
  return std::make_unique<WindowsFileSafety>();
#elif defined(Q_OS_MACOS)
  return std::make_unique<MacFileSafety>();
#else
  return {};
#endif
}

} // namespace

struct FileTransferRuntime::OutgoingSession
{
  OutgoingSession(
      ::relaydesk::transfer::TransferId transferId, DeviceId targetDevice, QList<QUrl> selectedItems,
      ::relaydesk::transfer::SendOptions sendOptions, ::relaydesk::transfer::TransferSnapshot initialSnapshot
  )
      : id(std::move(transferId)), peer(std::move(targetDevice)), localItems(std::move(selectedItems)),
        options(sendOptions), snapshot(std::move(initialSnapshot))
  {
  }

  ::relaydesk::transfer::TransferId id;
  DeviceId peer;
  QList<QUrl> localItems;
  ::relaydesk::transfer::SendOptions options;
  ::relaydesk::transfer::TransferSnapshot snapshot;
  std::optional<::relaydesk::transfer::TransferManifest> manifest;
  std::optional<::relaydesk::transfer::ManifestPagePlan> pagePlan;
  std::unique_ptr<::relaydesk::transfer::TransferOfferStateMachine> offerState;
  std::unique_ptr<::relaydesk::transfer::TransferControlStateMachine> control;
  std::shared_ptr<SenderWorkerState> sender;
  ::relaydesk::transfer::SenderPumpStatus pendingStatus =
      ::relaydesk::transfer::SenderPumpStatus::Progressed;
  quint64 nextManifestPage = 0;
  qsizetype currentEntry = 0;
  quint64 completedBytes = 0;
  quint64 completedFiles = 0;
  ::relaydesk::transfer::ConflictPolicy effectiveConflictPolicy =
      ::relaydesk::transfer::ConflictPolicy::AutoRename;
  QHash<QByteArray, quint64> resumeOffsets;
  std::optional<::relaydesk::transfer::ResumeQueryMessage> resumeQuery;
  quint64 senderGeneration = 0;
  bool senderTaskRunning = false;
  bool awaitingFileResult = false;
  bool paused = false;
  bool interrupted = false;
  bool resumeQueryPending = false;
  bool cancelled = false;
};

FileTransferRuntime::FileTransferRuntime(
    DeviceId localDeviceId, const TrustedDeviceStore &trustedDevices, DeviceDiscoveryRuntime &discoveryRuntime,
    QString combinedPemPath, FileTransferRuntimeOptions options, QObject *parent
)
    : IFileTransferService(parent), m_localDeviceId(std::move(localDeviceId)), m_trustedDevices(trustedDevices),
      m_discoveryRuntime(discoveryRuntime), m_combinedPemPath(std::move(combinedPemPath)),
      m_options(std::move(options)), m_workerPool(std::make_unique<QThreadPool>())
{
  m_workerPool->setMaxThreadCount(2);
  m_workerPool->setExpiryTimeout(30'000);
  m_fileSafety = createPlatformFileSafety();
  if (m_fileSafety != nullptr) {
    m_incoming = std::make_unique<IncomingTransferRuntime>(
        *m_fileSafety, *m_workerPool,
        [this](const DeviceId &deviceId) {
          const auto trusted = m_trustedDevices.find(deviceId);
          return trusted.has_value() && !trusted->revoked;
        },
        this
    );
  }
  auto *incoming = m_incoming.get();
  if (incoming != nullptr) {
    if (!m_options.localCapabilities.canReceiveFiles()) {
      m_options.localCapabilities.features.append(
          QString::fromLatin1(::relaydesk::transfer::kFileReceiveFeature)
      );
    }
    if (!m_options.localCapabilities.features.contains(QStringLiteral("folder.v1"))) {
      m_options.localCapabilities.features.append(QStringLiteral("folder.v1"));
    }
    if (!m_options.localCapabilities.features.contains(QStringLiteral("resume.v1"))) {
      m_options.localCapabilities.features.append(QStringLiteral("resume.v1"));
    }
    for (const auto policy : {
             ::relaydesk::transfer::ConflictPolicy::AutoRename,
             ::relaydesk::transfer::ConflictPolicy::Overwrite,
             ::relaydesk::transfer::ConflictPolicy::Skip,
             ::relaydesk::transfer::ConflictPolicy::Ask,
         }) {
      if (!m_options.localCapabilities.conflictPolicies.contains(policy)) {
        m_options.localCapabilities.conflictPolicies.append(policy);
      }
    }
    connect(incoming, &IncomingTransferRuntime::incomingOffer, this, &IFileTransferService::incomingOffer);
    connect(
        incoming, &IncomingTransferRuntime::incomingConflictDecisionRequired, this,
        &IFileTransferService::incomingConflictDecisionRequired
    );
    connect(
        incoming, &IncomingTransferRuntime::incomingConflictCancelRequested, this,
        [this](const ::relaydesk::transfer::TransferId &transferId) {
          cancel(transferId, {.reason = ::relaydesk::transfer::TransferCancelReason::UserRequested});
        }
    );
    connect(
        incoming, &IncomingTransferRuntime::transferOperationFinished, this,
        &IFileTransferService::transferOperationFinished
    );
    connect(incoming, &IncomingTransferRuntime::transferAdded, this, &IFileTransferService::transferAdded);
    connect(
        incoming, &IncomingTransferRuntime::transferChanged, this,
        &IFileTransferService::transferChanged
    );
    connect(
        incoming, &IncomingTransferRuntime::pipelineFailed, this,
        [this](const auto &, const auto &, QString diagnostic) {
          Q_EMIT errorOccurred(
              FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
              std::move(diagnostic)
          );
        }
    );
    connect(
        incoming, &IncomingTransferRuntime::responseReady, this,
        [this](const DeviceId &peer, const Frame &frame) {
          QString diagnostic;
          if (!sendPeerFrame(peer, frame, &diagnostic)) {
            Q_EMIT errorOccurred(
                FileTransferRuntimeError::TransportFailed, FileTlsError::ConnectionFailed,
                std::move(diagnostic)
            );
          }
        }
    );
    connect(
        incoming, &IncomingTransferRuntime::transferAccepted, this,
        [this](const DeviceId &peer, const ::relaydesk::transfer::TransferAccept &acceptance) {
          QString diagnostic;
          Frame frame{
              .type = MessageType::TransferAccept,
              .flags = ::relaydesk::transfer::Response,
              .metadata = ::relaydesk::transfer::ControlMessageCodec::encode(
                  ::relaydesk::transfer::kProtocolMajorVersion,
                  ::relaydesk::transfer::ControlMessage{acceptance}, &diagnostic
              ),
          };
          if (frame.metadata.isEmpty() || !sendPeerFrame(peer, frame, &diagnostic)) {
            Q_EMIT errorOccurred(
                FileTransferRuntimeError::TransportFailed, FileTlsError::ConnectionFailed,
                std::move(diagnostic)
            );
          }
        }
    );
    connect(
        incoming, &IncomingTransferRuntime::transferRejected, this,
        [this](const DeviceId &peer, const ::relaydesk::transfer::TransferReject &rejection) {
          QString diagnostic;
          Frame frame{
              .type = MessageType::TransferReject,
              .flags = ::relaydesk::transfer::Response,
              .metadata = ::relaydesk::transfer::ControlMessageCodec::encode(
                  ::relaydesk::transfer::kProtocolMajorVersion,
                  ::relaydesk::transfer::ControlMessage{rejection}, &diagnostic
              ),
          };
          if (frame.metadata.isEmpty() || !sendPeerFrame(peer, frame, &diagnostic)) {
            Q_EMIT errorOccurred(
                FileTransferRuntimeError::TransportFailed, FileTlsError::ConnectionFailed,
                std::move(diagnostic)
            );
          }
        }
    );
  }
}

FileTransferRuntime::~FileTransferRuntime()
{
  stop();
  m_workerPool->waitForDone();
  qDeleteAll(m_outgoing);
  m_outgoing.clear();
}

bool FileTransferRuntime::start(QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!onOwningThread(diagnostic)) {
    return false;
  }
  if (isRunning()) {
    return true;
  }

  if (m_options.localCapabilities.canReceiveFiles() && m_incoming == nullptr) {
    const auto message = QStringLiteral(
        "File transfer runtime cannot advertise file.receive.v1 without a platform receiver"
    );
    setDiagnostic(diagnostic, message);
    Q_EMIT errorOccurred(FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError, message);
    return false;
  }
  QString capabilityDiagnostic;
  if (CapabilityCodec::encode(m_options.localCapabilities, &capabilityDiagnostic).isEmpty()) {
    setDiagnostic(diagnostic, capabilityDiagnostic);
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError, capabilityDiagnostic
    );
    return false;
  }

  auto listener = std::make_unique<FileTlsListener>(
      m_localDeviceId, &m_trustedDevices, m_combinedPemPath, m_options.tlsSettings
  );
  connect(listener.get(), &FileTlsListener::connectionCreated, this, [this](FileTlsConnection *connection) {
    if (connection != nullptr) {
      attachConnection(*connection);
    }
  });
  connect(listener.get(), &FileTlsListener::failed, this, [this](FileTlsError error, const QString &message) {
    Q_EMIT errorOccurred(FileTransferRuntimeError::ListenerFailed, error, message);
  });

  QString listenDiagnostic;
  const auto result = listener->listen(m_options.listenAddress, m_options.listenPort, &listenDiagnostic);
  if (result != FileTlsError::None) {
    setDiagnostic(diagnostic, listenDiagnostic);
    Q_EMIT errorOccurred(FileTransferRuntimeError::ListenerFailed, result, listenDiagnostic);
    return false;
  }

  m_listener = std::move(listener);
  QString publishDiagnostic;
  if (!publishFileEndpoint(&publishDiagnostic)) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::DiscoveryPublishFailed, FileTlsError::None, publishDiagnostic
    );
  }
  Q_EMIT started(m_listener->serverPort());
  return true;
}

void FileTransferRuntime::stop()
{
  if (QThread::currentThread() != thread()) {
    return;
  }

  for (auto *session : std::as_const(m_outgoing)) {
    if (session != nullptr) {
      markOutgoingConnectionLost(*session);
    }
  }

  if (m_listener == nullptr) {
    return;
  }

  const auto connections = m_connections.keys();
  if (auto *incoming = m_incoming.get(); incoming != nullptr) {
    for (auto *connection : connections) {
      const auto context = m_connections.constFind(connection);
      if (context != m_connections.constEnd() && context->peer.has_value()) {
        incoming->peerDisconnected(*context->peer);
      }
    }
  }
  m_connections.clear();
  m_peerConnections.clear();
  for (auto *connection : connections) {
    if (connection != nullptr) {
      connection->close();
    }
  }
  const auto clients = m_clients.values();
  m_clients.clear();
  for (auto *client : clients) {
    delete client;
  }
  m_listener->close();
  m_listener.reset();

  QString publishDiagnostic;
  if (!publishFileEndpoint(&publishDiagnostic)) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::DiscoveryPublishFailed, FileTlsError::None, publishDiagnostic
    );
  }
  Q_EMIT stopped();
}

bool FileTransferRuntime::isRunning() const
{
  return m_listener != nullptr && m_listener->isListening();
}

quint16 FileTransferRuntime::listeningPort() const
{
  return m_listener == nullptr ? 0 : m_listener->serverPort();
}

bool FileTransferRuntime::connectPeer(const DeviceId &peerDeviceId, QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!onOwningThread(diagnostic)) {
    return false;
  }
  if (!isRunning()) {
    const auto message = QStringLiteral("File transfer runtime is not running");
    setDiagnostic(diagnostic, message);
    Q_EMIT errorOccurred(FileTransferRuntimeError::PeerUnavailable, FileTlsError::ConnectionFailed, message);
    return false;
  }
  if (m_peerConnections.contains(peerDeviceId) || m_clients.contains(peerDeviceId)) {
    return true;
  }

  const auto peerInfo = m_discoveryRuntime.registry().deviceInfo(peerDeviceId);
  const auto peerSnapshot = m_discoveryRuntime.registry().snapshot(peerDeviceId);
  if (!peerInfo.has_value() || !peerSnapshot.has_value() || !peerInfo->capabilities.fileV1 ||
      peerInfo->filePort == 0) {
    const auto message = QStringLiteral("Peer has no discovered file-transfer endpoint");
    setDiagnostic(diagnostic, message);
    Q_EMIT errorOccurred(FileTransferRuntimeError::PeerUnavailable, FileTlsError::ConnectionFailed, message);
    return false;
  }
  if (m_trustedDevices.trustStatus(peerDeviceId, peerInfo->certificateFingerprintSha256) !=
      TrustStatus::Trusted) {
    const auto message = QStringLiteral("Peer file-transfer identity is not trusted");
    setDiagnostic(diagnostic, message);
    Q_EMIT errorOccurred(FileTransferRuntimeError::PeerUnavailable, FileTlsError::UnknownPeer, message);
    return false;
  }
  const auto address = preferredAddress(peerSnapshot->addresses);
  if (!address.has_value()) {
    const auto message = QStringLiteral("Peer has no usable discovered IPv4 address");
    setDiagnostic(diagnostic, message);
    Q_EMIT errorOccurred(FileTransferRuntimeError::PeerUnavailable, FileTlsError::ConnectionFailed, message);
    return false;
  }

  return connectPeerAt(peerDeviceId, *address, peerInfo->filePort, diagnostic);
}

bool FileTransferRuntime::connectPeerAt(
    const DeviceId &peerDeviceId, const QHostAddress &address, quint16 filePort, QString *diagnostic
)
{
  if (diagnostic != nullptr) diagnostic->clear();
  if (!onOwningThread(diagnostic) || !isRunning() || address.isNull() || filePort == 0) {
    const auto message = QStringLiteral("Reconnect candidate is unavailable");
    setDiagnostic(diagnostic, message);
    return false;
  }
  const auto trusted = m_trustedDevices.find(peerDeviceId);
  if (!trusted.has_value() || trusted->revoked) {
    const auto message = QStringLiteral("Peer file-transfer identity is not trusted");
    setDiagnostic(diagnostic, message);
    return false;
  }
  if (m_peerConnections.contains(peerDeviceId) || m_clients.contains(peerDeviceId)) return true;

  auto *client = new FileTlsClient(
      m_localDeviceId, &m_trustedDevices, m_combinedPemPath, m_options.tlsSettings, this
  );
  m_clients.insert(peerDeviceId, client);
  connect(client, &FileTlsClient::connectionCreated, this, [this, peerDeviceId](FileTlsConnection *connection) {
    if (connection != nullptr) {
      attachConnection(*connection, peerDeviceId);
    }
  });

  QString connectDiagnostic;
  const auto result = client->connectToHost(address, filePort, &connectDiagnostic);
  if (result != FileTlsError::None) {
    m_clients.remove(peerDeviceId);
    delete client;
    setDiagnostic(diagnostic, connectDiagnostic);
    Q_EMIT errorOccurred(FileTransferRuntimeError::TransportFailed, result, connectDiagnostic);
    return false;
  }
  return true;
}

bool FileTransferRuntime::disconnectPeer(const DeviceId &peerDeviceId)
{
  if (QThread::currentThread() != thread())
    return false;

  if (auto *connection = m_peerConnections.value(peerDeviceId, nullptr); connection != nullptr) {
    connection->close();
    return true;
  }
  if (auto *client = m_clients.take(peerDeviceId); client != nullptr) {
    client->close();
    client->deleteLater();
    return true;
  }
  return false;
}

bool FileTransferRuntime::isPeerReady(const DeviceId &peerDeviceId) const
{
  const auto connection = m_peerConnections.value(peerDeviceId, nullptr);
  const auto context = m_connections.constFind(connection);
  return connection != nullptr && context != m_connections.constEnd() && context->negotiated.has_value();
}

std::optional<::relaydesk::transfer::NegotiatedCapabilities>
FileTransferRuntime::negotiatedCapabilities(const DeviceId &peerDeviceId) const
{
  const auto connection = m_peerConnections.value(peerDeviceId, nullptr);
  const auto context = m_connections.constFind(connection);
  return context == m_connections.constEnd() ? std::nullopt : context->negotiated;
}

::relaydesk::transfer::TransferStartResult FileTransferRuntime::send(
    const DeviceId &target, const QList<QUrl> &localItems,
    const ::relaydesk::transfer::SendOptions &options
)
{
  using ::relaydesk::transfer::TransferDirection;
  using ::relaydesk::transfer::TransferSnapshot;
  using ::relaydesk::transfer::TransferStartError;
  using ::relaydesk::transfer::TransferState;

  QString diagnostic;
  if (!onOwningThread(&diagnostic)) {
    return {.error = TransferStartError::WrongThread, .diagnostic = std::move(diagnostic)};
  }
  if (!isRunning()) {
    return {
        .error = TransferStartError::NotRunning,
        .diagnostic = QStringLiteral("File transfer runtime is not running"),
    };
  }
  if (localItems.isEmpty()) {
    return {
        .error = TransferStartError::InvalidRequest,
        .diagnostic = QStringLiteral("Transfer requires at least one local source"),
    };
  }
  for (const auto &item : localItems) {
    if (!item.isLocalFile() || item.toLocalFile().isEmpty()) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          QStringLiteral("Transfer sources must be non-empty local file URLs")
      );
      return {
          .error = TransferStartError::InvalidRequest,
          .diagnostic = QStringLiteral("Transfer sources must be non-empty local file URLs"),
      };
    }
  }
  const auto peer = m_discoveryRuntime.registry().snapshot(target);
  if (!peer.has_value()) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::PeerUnavailable, FileTlsError::ConnectionFailed,
        QStringLiteral("Transfer target is not present in the discovery registry")
    );
    return {
        .error = TransferStartError::PeerUnavailable,
        .diagnostic = QStringLiteral("Transfer target is not present in the discovery registry"),
    };
  }

  const auto id = ::relaydesk::transfer::TransferId::generate();
  QString displayName = QFileInfo(localItems.first().toLocalFile()).fileName();
  if (localItems.size() > 1) {
    displayName = tr("%n items", nullptr, localItems.size());
  }
  if (displayName.isEmpty()) {
    displayName = tr("File transfer");
  }
  TransferSnapshot snapshot{
      .id = id,
      .peerId = target,
      .peerDisplayName = peer->displayName,
      .displayName = std::move(displayName),
      .direction = TransferDirection::Sending,
      .state = TransferState::Preparing,
      .createdUtc = QDateTime::currentDateTimeUtc(),
  };
  snapshot.canCancel = true;
  auto *session = new OutgoingSession(id, target, localItems, options, snapshot);
  m_outgoing.insert(id, session);
  Q_EMIT transferAdded(session->snapshot);
  prepareOutgoing(id);
  return {.transferId = id};
}

void FileTransferRuntime::accept(
    const ::relaydesk::transfer::TransferId &transferId,
    const ::relaydesk::transfer::ReceiveOptions &options
)
{
  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    incoming->accept(transferId, options);
    return;
  }
  publishOperation(
      transferId, ::relaydesk::transfer::TransferOperation::Accept,
      ::relaydesk::transfer::TransferOperationOutcome::Rejected,
      ::relaydesk::transfer::TransferOperationError::UnknownTransfer,
      QStringLiteral("No incoming transfer exists for this ID")
  );
}

void FileTransferRuntime::resolveIncomingConflict(
    const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId,
    ::relaydesk::transfer::IncomingConflictDecision decision
)
{
  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    if (decision == ::relaydesk::transfer::IncomingConflictDecision::CancelTransfer) {
      if (!incoming->hasPendingIncomingConflict(transferId, conflictId)) {
        return;
      }
      // Reuse cancel's send-before-local-apply order. A transport failure
      // leaves the prompt and disk worker untouched so the caller can retry.
      cancel(transferId, {});
      return;
    }
    incoming->resolveIncomingConflict(transferId, conflictId, decision);
  }
}

void FileTransferRuntime::reject(
    const ::relaydesk::transfer::TransferId &transferId, ::relaydesk::transfer::RejectReason reason
)
{
  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    incoming->reject(transferId, reason);
    return;
  }
  publishOperation(
      transferId, ::relaydesk::transfer::TransferOperation::Reject,
      ::relaydesk::transfer::TransferOperationOutcome::Rejected,
      ::relaydesk::transfer::TransferOperationError::UnknownTransfer,
      QStringLiteral("No incoming transfer exists for this ID")
  );
}

void FileTransferRuntime::pause(const ::relaydesk::transfer::TransferId &transferId)
{
  using namespace ::relaydesk::transfer;

  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    DeviceId peer = DeviceId::generate();
    QString diagnostic;
    const TransferCommandMessage command = TransferPauseMessage{.transferId = transferId};
    if (!incoming->validateLocalCommand(command, &peer, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    if (!sendCommand(peer, command, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
          TransferOperationError::TransportFailed, diagnostic
      );
      return;
    }
    TransferOperationOutcome outcome = TransferOperationOutcome::Applied;
    if (!incoming->applyLocalCommand(command, nullptr, &diagnostic, &outcome)) {
      publishOperation(
          transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    publishOperation(transferId, TransferOperation::Pause, outcome);
    return;
  }
  auto *session = outgoing(transferId);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Transfer ID is unknown")
    );
    return;
  }
  if (session->paused && !session->cancelled) {
    publishOperation(transferId, TransferOperation::Pause, TransferOperationOutcome::Idempotent);
    return;
  }
  if (session->control == nullptr || session->cancelled) {
    publishOperation(
        transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Transfer cannot be paused in its current state")
    );
    return;
  }
  const auto result = session->control->pause();
  if (!result.ok()) {
    publishOperation(
        transferId, TransferOperation::Pause, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, result.diagnostic
    );
    return;
  }
  session->paused = true;
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);
  QString diagnostic;
  if (!sendCommand(session->peer, TransferPauseMessage{.transferId = transferId}, &diagnostic)) {
    failOutgoing(*session, TransferErrorCode::ConnectionLost, diagnostic);
    return;
  }
  publishOperation(transferId, TransferOperation::Pause, TransferOperationOutcome::Applied);
}

void FileTransferRuntime::resume(const ::relaydesk::transfer::TransferId &transferId)
{
  using namespace ::relaydesk::transfer;

  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    DeviceId peer = DeviceId::generate();
    QString diagnostic;
    const TransferCommandMessage command = TransferResumeMessage{.transferId = transferId};
    if (!incoming->validateLocalCommand(command, &peer, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    if (!sendCommand(peer, command, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
          TransferOperationError::TransportFailed, diagnostic
      );
      return;
    }
    TransferOperationOutcome outcome = TransferOperationOutcome::Applied;
    if (!incoming->applyLocalCommand(command, nullptr, &diagnostic, &outcome)) {
      publishOperation(
          transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    publishOperation(transferId, TransferOperation::Resume, outcome);
    return;
  }
  auto *session = outgoing(transferId);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Transfer ID is unknown")
    );
    return;
  }
  if (!session->paused && !session->cancelled && session->snapshot.state == TransferState::Transferring) {
    publishOperation(transferId, TransferOperation::Resume, TransferOperationOutcome::Idempotent);
    return;
  }
  if (session->control == nullptr || !session->paused || session->cancelled) {
    publishOperation(
        transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Transfer cannot be resumed in its current state")
    );
    return;
  }
  const auto resumed = session->control->resume();
  if (!resumed.ok() || !session->control->advance(TransferState::Transferring).ok()) {
    publishOperation(
        transferId, TransferOperation::Resume, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, resumed.diagnostic
    );
    return;
  }
  session->paused = false;
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);
  QString diagnostic;
  if (!sendCommand(session->peer, TransferResumeMessage{.transferId = transferId}, &diagnostic)) {
    failOutgoing(*session, TransferErrorCode::ConnectionLost, diagnostic);
    return;
  }
  scheduleSenderPump(transferId);
  publishOperation(transferId, TransferOperation::Resume, TransferOperationOutcome::Applied);
}

void FileTransferRuntime::cancel(
    const ::relaydesk::transfer::TransferId &transferId,
    const ::relaydesk::transfer::TransferCancelOptions &options
)
{
  using namespace ::relaydesk::transfer;

  if (auto *incoming = m_incoming.get(); incoming != nullptr && incoming->contains(transferId)) {
    DeviceId peer = DeviceId::generate();
    QString diagnostic;
    const TransferCommandMessage command = TransferCancelMessage{
        .transferId = transferId,
        .reason = options.reason,
        .keepPartial = options.partialDisposition == PartialDisposition::Keep,
    };
    if (!incoming->validateLocalCommand(command, &peer, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Cancel, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    if (!sendCommand(peer, command, &diagnostic)) {
      publishOperation(
          transferId, TransferOperation::Cancel, TransferOperationOutcome::Rejected,
          TransferOperationError::TransportFailed, diagnostic
      );
      return;
    }
    TransferOperationOutcome outcome = TransferOperationOutcome::Applied;
    if (!incoming->applyLocalCommand(command, nullptr, &diagnostic, &outcome)) {
      publishOperation(
          transferId, TransferOperation::Cancel, TransferOperationOutcome::Rejected,
          TransferOperationError::InvalidState, diagnostic
      );
      return;
    }
    publishOperation(transferId, TransferOperation::Cancel, outcome);
    return;
  }
  auto *session = outgoing(transferId);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Cancel, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Transfer ID is unknown")
    );
    return;
  }
  if (session->cancelled || TransferState::Cancelled == session->snapshot.state) {
    publishOperation(transferId, TransferOperation::Cancel, TransferOperationOutcome::Idempotent);
    return;
  }
  if (TransferControlStateMachine::isTerminal(session->snapshot.state)) {
    publishOperation(
        transferId, TransferOperation::Cancel, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Terminal transfer cannot be cancelled")
    );
    return;
  }
  session->cancelled = true;
  if (session->control != nullptr &&
      !::relaydesk::transfer::TransferControlStateMachine::isTerminal(session->control->snapshot().state)) {
    if (session->control->cancel().ok()) {
      (void)session->control->confirmCancelled();
      session->snapshot = session->control->snapshot();
    }
  } else {
    session->snapshot.state = TransferState::Cancelled;
    session->snapshot.canCancel = false;
    session->snapshot.finishedUtc = QDateTime::currentDateTimeUtc();
  }
  Q_EMIT transferChanged(session->snapshot);
  QString diagnostic;
  if (!sendCommand(
          session->peer,
          TransferCancelMessage{
              .transferId = transferId,
              .reason = options.reason,
              .keepPartial = options.partialDisposition == PartialDisposition::Keep,
          },
          &diagnostic
      )) {
    Q_EMIT errorOccurred(FileTransferRuntimeError::TransportFailed, FileTlsError::ConnectionFailed, diagnostic);
  }
  publishOperation(transferId, TransferOperation::Cancel, TransferOperationOutcome::Applied);
}

void FileTransferRuntime::retry(const ::relaydesk::transfer::TransferId &transferId)
{
  using namespace ::relaydesk::transfer;

  const auto *session = outgoing(transferId);
  if (session == nullptr) {
    publishOperation(
        transferId, TransferOperation::Retry, TransferOperationOutcome::Rejected,
        TransferOperationError::UnknownTransfer, QStringLiteral("Transfer ID is unknown")
    );
    return;
  }
  if (session->snapshot.state != TransferState::Failed) {
    publishOperation(
        transferId, TransferOperation::Retry, TransferOperationOutcome::Rejected,
        TransferOperationError::InvalidState, QStringLiteral("Only a failed transfer can be retried")
    );
    return;
  }
  const auto started = send(session->peer, session->localItems, session->options);
  if (!started.ok()) {
    publishOperation(
        transferId, TransferOperation::Retry, TransferOperationOutcome::Rejected,
        TransferOperationError::StartFailed, started.diagnostic
    );
    return;
  }
  publishOperation(transferId, TransferOperation::Retry, TransferOperationOutcome::Applied);
}

QList<::relaydesk::transfer::TransferSnapshot> FileTransferRuntime::activeTransfers() const
{
  QList<::relaydesk::transfer::TransferSnapshot> result;
  for (const auto *session : m_outgoing) {
    if (session != nullptr &&
        !::relaydesk::transfer::TransferControlStateMachine::isTerminal(session->snapshot.state)) {
      result.append(session->snapshot);
    }
  }
  if (const auto *incoming = m_incoming.get(); incoming != nullptr) {
    result.append(incoming->activeTransfers());
  }
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    return left.createdUtc < right.createdUtc;
  });
  return result;
}

bool FileTransferRuntime::onOwningThread(QString *diagnostic)
{
  if (QThread::currentThread() == thread() && m_discoveryRuntime.thread() == thread() &&
      m_discoveryRuntime.registry().thread() == thread()) {
    return true;
  }
  const auto message = QStringLiteral("File transfer runtime must start on the discovery runtime owning thread");
  setDiagnostic(diagnostic, message);
  Q_EMIT errorOccurred(FileTransferRuntimeError::WrongThread, FileTlsError::None, message);
  return false;
}

void FileTransferRuntime::attachConnection(
    FileTlsConnection &connection, std::optional<DeviceId> expectedPeer
)
{
  m_connections.insert(&connection, ConnectionContext{.expectedPeer = std::move(expectedPeer)});
  connect(&connection, &FileTlsConnection::authenticated, this, [this, &connection]() {
    handleAuthenticated(connection);
  });
  connect(&connection, &FileTlsConnection::frameReceived, this, [this, &connection](Frame frame) {
    handleFrame(connection, std::move(frame));
  });
  connect(
      &connection, &FileTlsConnection::writeCapacityAvailable, this,
      [this, &connection](quint64) {
        const auto context = m_connections.constFind(&connection);
        if (context != m_connections.cend() && context->peer.has_value()) {
          schedulePeerSenders(*context->peer);
        }
      }
  );
  connect(&connection, &FileTlsConnection::failed, this, [this, &connection](FileTlsError error, QString message) {
    failConnection(
        connection, FileTransferRuntimeError::TransportFailed, error, std::move(message)
    );
  });
  connect(&connection, &FileTlsConnection::disconnected, this, [this, &connection]() {
    removeConnection(connection);
  });
}

void FileTransferRuntime::handleAuthenticated(FileTlsConnection &connection)
{
  auto context = m_connections.find(&connection);
  const auto peer = connection.peerDeviceId();
  if (context == m_connections.end() || !peer.has_value()) {
    failConnection(
        connection, FileTransferRuntimeError::ProtocolFailed, FileTlsError::HelloInvalid,
        QStringLiteral("Authenticated file channel has no peer device ID")
    );
    return;
  }
  if (context->expectedPeer.has_value() && *context->expectedPeer != *peer) {
    failConnection(
        connection, FileTransferRuntimeError::ProtocolFailed, FileTlsError::HelloInvalid,
        QStringLiteral("Authenticated file channel returned a different peer device ID")
    );
    return;
  }

  context->peer = *peer;
  if (auto *existing = m_peerConnections.value(*peer, nullptr); existing != nullptr && existing != &connection) {
    existing->close();
  }
  m_peerConnections.insert(*peer, &connection);

  QString encodeDiagnostic;
  Frame capabilities{
      .type = MessageType::Capabilities,
      .metadata = CapabilityCodec::encode(m_options.localCapabilities, &encodeDiagnostic),
  };
  if (capabilities.metadata.isEmpty()) {
    failConnection(
        connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
        std::move(encodeDiagnostic)
    );
    return;
  }
  QString sendDiagnostic;
  const auto result = connection.sendFrame(capabilities, &sendDiagnostic);
  if (result != FileTlsError::None) {
    failConnection(
        connection, FileTransferRuntimeError::TransportFailed, result, std::move(sendDiagnostic)
    );
  }
}

void FileTransferRuntime::handleFrame(FileTlsConnection &connection, Frame frame)
{
  auto context = m_connections.find(&connection);
  if (context == m_connections.end() || !context->peer.has_value()) {
    failConnection(
        connection, FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
        QStringLiteral("File channel received a frame before peer authentication")
    );
    return;
  }

  if (frame.type == MessageType::Capabilities) {
    if (context->negotiated.has_value() || frame.streamId != 0 || !frame.payload.isEmpty()) {
      failConnection(
          connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
          QStringLiteral("File channel received invalid or duplicate capabilities")
      );
      return;
    }
    const auto decoded = CapabilityCodec::decode(frame.type, frame.metadata);
    if (!decoded.ok()) {
      failConnection(
          connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
          decoded.diagnostic
      );
      return;
    }
    const auto negotiated = CapabilityNegotiator::negotiate(
        {::relaydesk::transfer::kProtocolMajorVersion}, m_options.localCapabilities, {frame.version},
        *decoded.message
    );
    if (!negotiated.ok()) {
      failConnection(
          connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
          negotiated.diagnostic
      );
      return;
    }
    context->negotiated = *negotiated.capabilities;
    Q_EMIT peerReady(*context->peer, *context->negotiated);
    offerPreparedTransfers(*context->peer);
    return;
  }

  if (!context->negotiated.has_value()) {
    failConnection(
        connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
        QStringLiteral("File channel received a transfer frame before capability negotiation")
    );
    return;
  }
  if (frame.type == MessageType::TransferOffer && !context->negotiated->localCanReceiveFiles) {
    failConnection(
        connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
        QStringLiteral("Peer sent TRANSFER_OFFER without negotiated file.receive.v1 capability")
    );
    return;
  }
  routeTransferFrame(*context->peer, frame);
}

void FileTransferRuntime::routeTransferFrame(const DeviceId &peerDeviceId, const Frame &frame)
{
  switch (frame.type) {
  case MessageType::TransferOffer: {
    using ::relaydesk::transfer::ControlMessageCodec;
    using ::relaydesk::transfer::TransferOffer;
    const auto decoded = ControlMessageCodec::decode(frame.version, frame.type, frame.metadata);
    const auto *offer = decoded.ok() ? std::get_if<TransferOffer>(&*decoded.message) : nullptr;
    const auto capabilities = negotiatedCapabilities(peerDeviceId);
    const auto peer = m_discoveryRuntime.registry().snapshot(peerDeviceId);
    const auto trusted = m_trustedDevices.find(peerDeviceId);
    const bool peerTrusted = trusted.has_value() && !trusted->revoked;
    const bool peerAllowsAutoAccept = peerTrusted && trusted->autoAcceptFiles;
    QString diagnostic;
    if (offer == nullptr) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          decoded.diagnostic
      );
      break;
    }
    auto *incoming = m_incoming.get();
    if (incoming == nullptr || !capabilities.has_value() ||
        !incoming->receiveOffer(
            peerDeviceId,
            peer.has_value()
                ? peer->displayName
                : (trusted.has_value() ? trusted->alias : QStringLiteral("Trusted peer")),
            peerTrusted, peerAllowsAutoAccept, *capabilities, *offer, &diagnostic
        )) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          std::move(diagnostic)
      );
    }
    break;
  }
  case MessageType::TransferAccept:
  case MessageType::TransferReject:
    handleOfferResponse(peerDeviceId, frame);
    break;
  case MessageType::FileResult:
    handleFileResult(peerDeviceId, frame);
    break;
  case MessageType::ResumeQuery: {
    QString diagnostic;
    auto *incoming = m_incoming.get();
    if (incoming == nullptr || !incoming->receiveResumeQuery(peerDeviceId, frame, &diagnostic)) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          std::move(diagnostic)
      );
    }
    break;
  }
  case MessageType::ResumeResponse:
    handleResumeResponse(peerDeviceId, frame);
    break;
  case MessageType::FileCheckpoint: {
    const auto decoded = ::relaydesk::transfer::FileMessageCodec::decode(frame.type, frame.metadata);
    if (!decoded.ok()) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          decoded.diagnostic
      );
    }
    break;
  }
  case MessageType::TransferPause:
  case MessageType::TransferResume:
  case MessageType::TransferCancel: {
    QString diagnostic;
    auto *incoming = m_incoming.get();
    if (!applyRemoteCommand(peerDeviceId, frame, &diagnostic) &&
        (incoming == nullptr || !incoming->receiveCommand(peerDeviceId, frame, &diagnostic))) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          std::move(diagnostic)
      );
    }
    break;
  }
  case MessageType::ManifestPage:
  case MessageType::ManifestComplete:
  case MessageType::FileBegin:
  case MessageType::FileChunk:
  case MessageType::FileEnd: {
    QString diagnostic;
    auto *incoming = m_incoming.get();
    if (incoming == nullptr || !incoming->enqueueFrame(peerDeviceId, frame, &diagnostic)) {
      Q_EMIT errorOccurred(
          FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError,
          std::move(diagnostic)
      );
    }
    break;
  }
  default:
    break;
  }
}

bool FileTransferRuntime::applyRemoteCommand(
    const DeviceId &peerDeviceId, const Frame &frame, QString *diagnostic
)
{
  using namespace ::relaydesk::transfer;

  const auto decoded = TransferCommandCodec::decode(frame.version, frame.type, frame.metadata);
  if (!decoded.ok()) {
    setDiagnostic(diagnostic, decoded.diagnostic);
    return false;
  }
  const auto transferId = std::visit([](const auto &typed) { return typed.transferId; }, *decoded.message);
  auto *session = outgoing(transferId);
  if (session == nullptr || session->peer != peerDeviceId || session->control == nullptr) {
    return false;
  }
  if (std::holds_alternative<TransferCancelMessage>(*decoded.message) &&
      (session->cancelled || session->snapshot.state == TransferState::Cancelled)) {
    return true;
  }
  if (std::holds_alternative<TransferPauseMessage>(*decoded.message)) {
    if (session->paused) {
      return true;
    }
    const auto result = session->control->pause();
    if (!result.ok()) {
      setDiagnostic(diagnostic, result.diagnostic);
      return false;
    }
    session->paused = true;
  } else if (std::holds_alternative<TransferResumeMessage>(*decoded.message)) {
    if (!session->paused) {
      return true;
    }
    const auto resumed = session->control->resume();
    if (!resumed.ok() || !session->control->advance(TransferState::Transferring).ok()) {
      setDiagnostic(diagnostic, resumed.diagnostic);
      return false;
    }
    session->paused = false;
    scheduleSenderPump(transferId);
  } else {
    const auto cancelled = session->control->cancel();
    if (!cancelled.ok()) {
      setDiagnostic(diagnostic, cancelled.diagnostic);
      return false;
    }
    const auto confirmed = session->control->confirmCancelled();
    if (!confirmed.ok()) {
      setDiagnostic(diagnostic, confirmed.diagnostic);
      return false;
    }
    session->cancelled = true;
  }
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);
  return true;
}

bool FileTransferRuntime::sendPeerFrame(const DeviceId &peerDeviceId, const Frame &frame, QString *diagnostic)
{
  auto *connection = m_peerConnections.value(peerDeviceId, nullptr);
  if (connection == nullptr || !isPeerReady(peerDeviceId)) {
    setDiagnostic(diagnostic, QStringLiteral("Peer file channel is not ready"));
    return false;
  }
  const auto result = connection->sendFrame(frame, diagnostic);
  return result == FileTlsError::None;
}

bool FileTransferRuntime::sendCommand(
    const DeviceId &peerDeviceId, const ::relaydesk::transfer::TransferCommandMessage &command,
    QString *diagnostic
)
{
  QString encodeDiagnostic;
  Frame frame{
      .type = ::relaydesk::transfer::messageType(command),
      .flags = ::relaydesk::transfer::AckRequired,
      .metadata = ::relaydesk::transfer::TransferCommandCodec::encode(command, &encodeDiagnostic),
  };
  if (frame.metadata.isEmpty()) {
    setDiagnostic(diagnostic, std::move(encodeDiagnostic));
    return false;
  }
  return sendPeerFrame(peerDeviceId, frame, diagnostic);
}

void FileTransferRuntime::prepareOutgoing(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::ManifestSourceRequest;
  using ::relaydesk::transfer::TransferManifestRequest;

  auto *session = outgoing(transferId);
  if (session == nullptr) {
    return;
  }
  QList<ManifestSourceRequest> sources;
  sources.reserve(session->localItems.size());
  for (const auto &url : session->localItems) {
    sources.append({.sourcePath = url.toLocalFile()});
  }
  TransferManifestRequest request{
      .sources = std::move(sources),
      .transferId = transferId,
      .displayName = session->localItems.size() > 1 ? session->snapshot.displayName : QString{},
  };
  auto *watcher = new QFutureWatcher<::relaydesk::transfer::TransferManifestBuildResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, watcher]() {
    auto result = watcher->result();
    watcher->deleteLater();
    finishManifestPreparation(transferId, std::move(result));
  });
  watcher->setFuture(QtConcurrent::run(m_workerPool.get(), [request = std::move(request)]() {
    return ::relaydesk::transfer::ManifestBuilder::buildTransfer(request);
  }));
}

void FileTransferRuntime::finishManifestPreparation(
    const ::relaydesk::transfer::TransferId &transferId,
    ::relaydesk::transfer::TransferManifestBuildResult result
)
{
  using ::relaydesk::transfer::ManifestPageCodec;
  using ::relaydesk::transfer::TransferControlStateMachine;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->cancelled) {
    return;
  }
  if (!result.ok()) {
    failOutgoing(*session, TransferErrorCode::ManifestBuildFailed, result.diagnostic);
    return;
  }

  session->manifest = std::move(*result.manifest);
  const auto plan = ManifestPageCodec::plan(*session->manifest);
  if (!plan.ok()) {
    failOutgoing(*session, TransferErrorCode::ManifestBuildFailed, plan.diagnostic);
    return;
  }
  session->pagePlan = *plan.plan;
  session->snapshot.displayName = session->manifest->summary.displayName;
  session->snapshot.progress.totalBytes = session->manifest->summary.totalBytes;
  session->snapshot.progress.totalFiles = session->manifest->summary.fileCount;
  session->control = std::make_unique<TransferControlStateMachine>(session->snapshot);
  const auto initialized = session->control->initialize();
  if (!initialized.ok()) {
    failOutgoing(*session, TransferErrorCode::ManifestBuildFailed, initialized.diagnostic);
    return;
  }
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);

  QString connectDiagnostic;
  if (!connectPeer(session->peer, &connectDiagnostic)) {
    failOutgoing(*session, TransferErrorCode::ConnectionLost, connectDiagnostic);
    return;
  }
  if (isPeerReady(session->peer)) {
    sendOffer(*session);
  }
}

void FileTransferRuntime::offerPreparedTransfers(const DeviceId &peerDeviceId)
{
  const auto sessions = m_outgoing.values();
  for (auto *session : sessions) {
    if (session != nullptr && session->peer == peerDeviceId && session->interrupted &&
        !session->cancelled) {
      if (session->senderTaskRunning) {
        QTimer::singleShot(10, this, [this, peerDeviceId]() { offerPreparedTransfers(peerDeviceId); });
      } else {
        sendResumeQuery(*session);
      }
    } else if (session != nullptr && session->peer == peerDeviceId && session->manifest.has_value() &&
        session->pagePlan.has_value() && session->offerState == nullptr && !session->cancelled) {
      sendOffer(*session);
    }
  }
}

void FileTransferRuntime::sendResumeQuery(OutgoingSession &session)
{
  using namespace ::relaydesk::transfer;

  if (!session.interrupted || session.resumeQueryPending || !session.manifest.has_value() ||
      session.control == nullptr) {
    return;
  }
  const auto negotiated = negotiatedCapabilities(session.peer);
  if (!negotiated.has_value() || !negotiated->features.contains(QStringLiteral("resume.v1"))) {
    failOutgoing(
        session, TransferErrorCode::ConnectionLost,
        QStringLiteral("Peer did not negotiate resume.v1 after transfer interruption")
    );
    return;
  }
  ResumeQueryMessage query{
      .transferId = session.id,
      .manifestSha256 = session.manifest->summary.canonicalSha256,
  };
  QString diagnostic;
  Frame frame{
      .type = MessageType::ResumeQuery,
      .flags = AckRequired,
      .metadata = ResumeMessageCodec::encode(ResumeControlMessage{query}, &diagnostic),
  };
  if (frame.metadata.isEmpty() || !sendPeerFrame(session.peer, frame, &diagnostic)) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::TransportFailed, FileTlsError::ConnectionFailed,
        std::move(diagnostic)
    );
    return;
  }
  const auto resumed = session.control->resume();
  if (!resumed.ok()) {
    failOutgoing(session, TransferErrorCode::ConnectionLost, resumed.diagnostic);
    return;
  }
  session.resumeQuery = std::move(query);
  session.resumeQueryPending = true;
  session.snapshot = session.control->snapshot();
  Q_EMIT transferChanged(session.snapshot);
}

void FileTransferRuntime::handleResumeResponse(const DeviceId &peerDeviceId, const Frame &frame)
{
  using namespace ::relaydesk::transfer;

  const auto decoded = ResumeMessageCodec::decode(frame.version, frame.type, frame.metadata);
  const auto *response = decoded.ok() ? std::get_if<ResumeResponseMessage>(&*decoded.message) : nullptr;
  if (response == nullptr) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError, decoded.diagnostic
    );
    return;
  }
  auto *session = outgoing(response->transferId);
  if (session == nullptr || session->peer != peerDeviceId || !session->resumeQueryPending ||
      !session->resumeQuery.has_value() || !session->manifest.has_value() || session->control == nullptr) {
    return;
  }
  QList<ManifestEntry> entries;
  entries.reserve(session->manifest->entries.size());
  for (const auto &source : session->manifest->entries) {
    entries.append(source.entry);
  }
  const auto validated = ResumeNegotiator::validateResponse(*session->resumeQuery, *response, entries);
  if (!validated.ok()) {
    failOutgoing(*session, TransferErrorCode::ConnectionLost, validated.diagnostic);
    return;
  }

  session->resumeOffsets.clear();
  for (const auto &file : validated.plan->files) {
    session->resumeOffsets.insert(file.fileId.toBytes(), file.durableOffset);
  }
  session->completedBytes = 0;
  session->completedFiles = 0;
  session->currentEntry = 0;
  for (qsizetype index = 0; index < session->manifest->entries.size(); ++index) {
    const auto &entry = session->manifest->entries.at(index).entry;
    if (entry.type == ManifestEntryType::Directory) {
      session->currentEntry = index + 1;
      continue;
    }
    const auto offsetEntry = session->resumeOffsets.constFind(entry.id.toBytes());
    const bool offsetReported = offsetEntry != session->resumeOffsets.cend();
    const quint64 offset = offsetReported ? *offsetEntry : 0;
    session->completedBytes += offset;
    const bool committed = (entry.size == 0 && !offsetReported) ||
                           (entry.size != 0 && offsetReported && offset == entry.size);
    if (committed) {
      ++session->completedFiles;
      session->currentEntry = index + 1;
      continue;
    }
    session->currentEntry = index;
    break;
  }
  session->nextManifestPage = 0;
  session->sender.reset();
  session->awaitingFileResult = false;
  session->pendingStatus = SenderPumpStatus::Progressed;
  session->resumeQueryPending = false;
  session->interrupted = false;
  updateOutgoingProgress(*session);
  Q_EMIT transferChanged(session->snapshot);
  QTimer::singleShot(0, this, [this, id = session->id]() { sendNextManifestPage(id); });
}

void FileTransferRuntime::sendOffer(OutgoingSession &session)
{
  using ::relaydesk::transfer::ControlMessage;
  using ::relaydesk::transfer::ControlMessageCodec;
  using ::relaydesk::transfer::TransferOffer;
  using ::relaydesk::transfer::TransferOfferStateMachine;
  using ::relaydesk::transfer::TransferState;

  const auto negotiated = negotiatedCapabilities(session.peer);
  if (!negotiated.has_value() || !session.manifest.has_value() || !session.pagePlan.has_value() ||
      session.control == nullptr) {
    return;
  }
  if (!negotiated->peerCanReceiveFiles) {
    failOutgoing(
        session, TransferErrorCode::OfferFailed,
        QStringLiteral("Peer did not advertise file.receive.v1 capability")
    );
    return;
  }
  TransferOffer offer{
      .transferId = session.id,
      .displayName = session.manifest->summary.displayName,
      .totalBytes = session.manifest->summary.totalBytes,
      .fileCount = session.manifest->summary.fileCount,
      .directoryCount = session.manifest->summary.directoryCount,
      .manifestSha256 = session.manifest->summary.canonicalSha256,
      .manifestPageCount = session.pagePlan->pageCount(),
      .requestedConflictPolicy = session.options.conflictPolicy,
      .createdAtMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
  };
  auto offerState = std::make_unique<TransferOfferStateMachine>(*negotiated);
  const auto begun = offerState->beginOutgoing(offer);
  if (!begun.ok()) {
    failOutgoing(session, TransferErrorCode::OfferFailed, begun.diagnostic);
    return;
  }
  QString diagnostic;
  Frame frame{
      .type = MessageType::TransferOffer,
      .flags = ::relaydesk::transfer::AckRequired,
      .metadata = ControlMessageCodec::encode(
          ::relaydesk::transfer::kProtocolMajorVersion, ControlMessage{offer}, &diagnostic
      ),
  };
  if (frame.metadata.isEmpty() || !sendPeerFrame(session.peer, frame, &diagnostic)) {
    failOutgoing(session, TransferErrorCode::OfferFailed, diagnostic);
    return;
  }
  session.offerState = std::move(offerState);
  if (!session.control->advance(TransferState::Offered).ok() ||
      !session.control->advance(TransferState::WaitingForAcceptance).ok()) {
    failOutgoing(
        session, TransferErrorCode::OfferFailed,
        QStringLiteral("Outgoing transfer state could not enter acceptance wait")
    );
    return;
  }
  session.snapshot = session.control->snapshot();
  Q_EMIT transferChanged(session.snapshot);
}

void FileTransferRuntime::handleOfferResponse(const DeviceId &peerDeviceId, const Frame &frame)
{
  using ::relaydesk::transfer::ControlMessageCodec;
  using ::relaydesk::transfer::TransferAccept;
  using ::relaydesk::transfer::TransferReject;
  using ::relaydesk::transfer::TransferState;

  const auto decoded = ControlMessageCodec::decode(frame.version, frame.type, frame.metadata);
  if (!decoded.ok()) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError, decoded.diagnostic
    );
    return;
  }
  if (const auto *acceptance = std::get_if<TransferAccept>(&*decoded.message)) {
    auto *session = outgoing(acceptance->transferId);
    if (session == nullptr || session->peer != peerDeviceId || session->offerState == nullptr ||
        session->control == nullptr) {
      return;
    }
    const auto accepted = session->offerState->receiveAccept(*acceptance);
    if (!accepted.ok() || !session->control->advance(TransferState::Queued).ok()) {
      failOutgoing(*session, TransferErrorCode::OfferFailed, accepted.diagnostic);
      return;
    }
    session->effectiveConflictPolicy = acceptance->effectiveConflictPolicy;
    session->snapshot = session->control->snapshot();
    Q_EMIT transferChanged(session->snapshot);
    QTimer::singleShot(0, this, [this, id = session->id]() { sendNextManifestPage(id); });
    return;
  }
  if (const auto *rejection = std::get_if<TransferReject>(&*decoded.message)) {
    auto *session = outgoing(rejection->transferId);
    if (session == nullptr || session->peer != peerDeviceId || session->offerState == nullptr ||
        session->control == nullptr) {
      return;
    }
    (void)session->offerState->receiveReject(*rejection);
    (void)session->control->advance(TransferState::Rejected);
    session->snapshot = session->control->snapshot();
    Q_EMIT transferChanged(session->snapshot);
  }
}

void FileTransferRuntime::sendNextManifestPage(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::ManifestComplete;
  using ::relaydesk::transfer::ManifestPageCodec;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->cancelled || !session->manifest.has_value() ||
      !session->pagePlan.has_value()) {
    return;
  }
  QString diagnostic;
  if (session->nextManifestPage < session->pagePlan->pageCount()) {
    Frame frame{
        .type = MessageType::ManifestPage,
        .metadata = ManifestPageCodec::encodePage(
            *session->manifest, *session->pagePlan, session->nextManifestPage, {}, &diagnostic
        ),
    };
    if (frame.metadata.isEmpty() || !sendPeerFrame(session->peer, frame, &diagnostic)) {
      failOutgoing(*session, TransferErrorCode::OfferFailed, diagnostic);
      return;
    }
    ++session->nextManifestPage;
    QTimer::singleShot(0, this, [this, transferId]() { sendNextManifestPage(transferId); });
    return;
  }

  Frame complete{
      .type = MessageType::ManifestComplete,
      .flags = ::relaydesk::transfer::Final,
      .metadata = ManifestPageCodec::encodeComplete(
          ManifestComplete{.transferId = session->id,
                           .canonicalSha256 = session->manifest->summary.canonicalSha256},
          &diagnostic
      ),
  };
  if (complete.metadata.isEmpty() || !sendPeerFrame(session->peer, complete, &diagnostic)) {
    failOutgoing(*session, TransferErrorCode::OfferFailed, diagnostic);
    return;
  }
  startNextOutgoingFile(*session);
}

void FileTransferRuntime::startNextOutgoingFile(OutgoingSession &session)
{
  using ::relaydesk::transfer::ManifestEntryType;
  using ::relaydesk::transfer::TransferSenderRequest;
  using ::relaydesk::transfer::TransferState;

  if (!session.manifest.has_value() || session.control == nullptr || session.cancelled) {
    return;
  }
  while (session.currentEntry < session.manifest->entries.size() &&
         session.manifest->entries.at(session.currentEntry).entry.type == ManifestEntryType::Directory) {
    ++session.currentEntry;
  }
  if (session.currentEntry >= session.manifest->entries.size()) {
    if (session.control->snapshot().state == TransferState::Queued) {
      (void)session.control->advance(TransferState::Transferring);
    }
    completeOutgoing(session);
    return;
  }

  const auto capabilities = negotiatedCapabilities(session.peer);
  if (!capabilities.has_value()) {
    failOutgoing(
        session, TransferErrorCode::ConnectionLost, QStringLiteral("Peer file channel is no longer ready")
    );
    return;
  }
  const auto &source = session.manifest->entries.at(session.currentEntry);
  auto *connection = m_peerConnections.value(session.peer, nullptr);
  if (connection == nullptr) {
    failOutgoing(
        session, TransferErrorCode::ConnectionLost, QStringLiteral("Peer file channel is no longer ready")
    );
    return;
  }
  session.sender = std::make_shared<SenderWorkerState>(
      TransferSenderRequest{
          .transferId = session.id,
          .source = source,
          .streamId = static_cast<quint64>(session.currentEntry + 1),
          .chunkBytes = capabilities->chunkBytes,
          .startOffset = session.resumeOffsets.value(source.entry.id.toBytes(), 0),
      },
      senderBackpressureLimits(*connection)
  );
  ++session.senderGeneration;
  session.awaitingFileResult = false;
  session.pendingStatus = ::relaydesk::transfer::SenderPumpStatus::Progressed;
  const auto state = session.control->snapshot().state;
  if ((state == TransferState::Queued || state == TransferState::Resuming) &&
      !session.control->advance(TransferState::Transferring).ok()) {
    failOutgoing(
        session, TransferErrorCode::SenderFailed,
        QStringLiteral("Outgoing transfer could not enter the transferring state")
    );
    return;
  }
  updateOutgoingProgress(session);
  Q_EMIT transferChanged(session.snapshot);
  scheduleSenderPump(session.id);
}

void FileTransferRuntime::schedulePeerSenders(const DeviceId &peerDeviceId)
{
  QList<::relaydesk::transfer::TransferId> transfers;
  for (auto *session : std::as_const(m_outgoing)) {
    if (session != nullptr && session->peer == peerDeviceId && session->sender != nullptr && !session->cancelled) {
      transfers.append(session->id);
    }
  }
  for (const auto &transferId : std::as_const(transfers)) {
    scheduleSenderPump(transferId);
  }
}

void FileTransferRuntime::scheduleSenderPump(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::SenderPumpStatus;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->cancelled || session->paused || session->awaitingFileResult ||
      session->interrupted || session->senderTaskRunning || session->sender == nullptr) {
    return;
  }
  auto *connection = m_peerConnections.value(session->peer, nullptr);
  if (connection == nullptr || !isPeerReady(session->peer)) {
    failOutgoing(
        *session, TransferErrorCode::ConnectionLost,
        QStringLiteral("Peer file channel disconnected while sending")
    );
    return;
  }

  session->sender->updateTransportQueuedBytes(connection->queuedWriteBytes());
  if (const auto pendingFrame = session->sender->pendingFrame(); pendingFrame.has_value()) {
    const auto frameType = pendingFrame->type;
    const auto payloadBytes = static_cast<quint64>(pendingFrame->payload.size());
    FileTlsFrameSink sink(*connection);
    const quint64 queuedBeforeSubmit = sink.queuedBytes();
    const SenderFrameSinkResult submitted = sink.submit(*pendingFrame);
    if (submitted.status == SenderFrameSinkStatus::Backpressured) {
      if (queuedBeforeSubmit == 0) {
        failOutgoing(*session, TransferErrorCode::SenderFailed, submitted.diagnostic);
      }
      return;
    }
    if (submitted.status != SenderFrameSinkStatus::Accepted) {
      failOutgoing(*session, TransferErrorCode::SenderFailed, submitted.diagnostic);
      return;
    }
    session->sender->consumePendingFrame();
    if (frameType == MessageType::FileChunk) {
      session->completedBytes += payloadBytes;
      updateOutgoingProgress(*session);
      Q_EMIT transferChanged(session->snapshot);
    }
    if (session->pendingStatus == SenderPumpStatus::Finished) {
      session->awaitingFileResult = true;
      return;
    }
  }

  session->sender->updateTransportQueuedBytes(connection->queuedWriteBytes());
  if (!session->sender->readyToPump()) {
    return;
  }

  session->senderTaskRunning = true;
  const quint64 generation = session->senderGeneration;
  const auto worker = session->sender;
  auto *watcher = new QFutureWatcher<SenderWorkResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, generation, watcher]() {
    auto work = watcher->result();
    watcher->deleteLater();
    dispatchSenderPumpResult(transferId, generation, work.result);
  });
  watcher->setFuture(QtConcurrent::run(m_workerPool.get(), [worker]() { return worker->produce(); }));
}

void FileTransferRuntime::dispatchSenderPumpResult(
    const ::relaydesk::transfer::TransferId &transferId, quint64 generation,
    const SenderPumpResult &result
)
{
  using ::relaydesk::transfer::SenderPumpStatus;

  auto *session = outgoing(transferId);
  if (session == nullptr) {
    return;
  }
  if (generation != session->senderGeneration) {
    session->senderTaskRunning = false;
    if (session->interrupted && isPeerReady(session->peer)) {
      offerPreparedTransfers(session->peer);
    }
    return;
  }
  session->senderTaskRunning = false;
  if (session->cancelled || session->interrupted) {
    return;
  }
  if (result.status == SenderPumpStatus::Failed) {
    failOutgoing(*session, TransferErrorCode::SenderFailed, result.diagnostic);
    return;
  }
  session->pendingStatus = result.status;
  if (result.status == SenderPumpStatus::Finished && !session->sender->pendingFrame().has_value()) {
    session->awaitingFileResult = true;
    return;
  }
  scheduleSenderPump(transferId);
}

void FileTransferRuntime::handleFileResult(const DeviceId &peerDeviceId, const Frame &frame)
{
  using ::relaydesk::transfer::FileMessageCodec;
  using ::relaydesk::transfer::FileResultCode;
  using ::relaydesk::transfer::FileResultMessage;

  const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
  if (!decoded.ok()) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError, decoded.diagnostic
    );
    return;
  }
  const auto *result = std::get_if<FileResultMessage>(&*decoded.message);
  if (result == nullptr) {
    return;
  }
  auto *session = outgoing(result->transferId);
  if (session == nullptr || session->peer != peerDeviceId || !session->manifest.has_value() ||
      !session->awaitingFileResult || session->currentEntry >= session->manifest->entries.size() ||
      session->manifest->entries.at(session->currentEntry).entry.id != result->fileId) {
    return;
  }
  // A receiver-side Ask decision may choose Skip even though the negotiated
  // sender policy remains Ask. TARGET_SKIPPED is therefore a completed file,
  // independent of the sender's original policy.
  const bool skipped = result->code == FileResultCode::TargetExists &&
                       result->diagnostic == QStringLiteral("TARGET_SKIPPED");
  if (result->code != FileResultCode::Ok && !skipped) {
    failOutgoing(*session, TransferErrorCode::PeerFileFailed, result->diagnostic);
    return;
  }

  ++session->completedFiles;
  ++session->currentEntry;
  session->sender.reset();
  session->awaitingFileResult = false;
  updateOutgoingProgress(*session);
  Q_EMIT transferChanged(session->snapshot);
  startNextOutgoingFile(*session);
}

void FileTransferRuntime::updateOutgoingProgress(OutgoingSession &session)
{
  if (session.control == nullptr || !session.manifest.has_value()) {
    return;
  }
  QString currentPath;
  if (session.currentEntry < session.manifest->entries.size()) {
    currentPath = session.manifest->entries.at(session.currentEntry).entry.relativeProtocolPath;
  }
  (void)session.control->updateProgress(
      session.completedBytes, session.completedFiles, 0.0, std::nullopt, std::move(currentPath)
  );
  session.snapshot = session.control->snapshot();
}

void FileTransferRuntime::completeOutgoing(OutgoingSession &session)
{
  using ::relaydesk::transfer::TransferState;

  if (session.control == nullptr) {
    return;
  }
  updateOutgoingProgress(session);
  if (!session.control->advance(TransferState::Verifying).ok() ||
      !session.control->advance(TransferState::Committing).ok() ||
      !session.control->advance(TransferState::Completed).ok()) {
    failOutgoing(
        session, TransferErrorCode::SenderFailed,
        QStringLiteral("Outgoing transfer could not complete its state sequence")
    );
    return;
  }
  session.snapshot = session.control->snapshot();
  Q_EMIT transferChanged(session.snapshot);
}

void FileTransferRuntime::markOutgoingConnectionLost(OutgoingSession &session)
{
  using ::relaydesk::transfer::TransferControlStateMachine;
  using ::relaydesk::transfer::TransferErrorCode;

  if (session.cancelled || TransferControlStateMachine::isTerminal(session.snapshot.state)) {
    return;
  }

  session.paused = false;
  session.resumeQueryPending = false;
  session.resumeQuery.reset();
  session.awaitingFileResult = false;
  ++session.senderGeneration;
  session.sender.reset();

  if (session.control != nullptr && session.control->interrupt().ok()) {
    session.interrupted = true;
    session.snapshot = session.control->snapshot();
    Q_EMIT transferChanged(session.snapshot);
    return;
  }

  session.interrupted = false;
  failOutgoing(session, TransferErrorCode::ConnectionLost);
}

void FileTransferRuntime::failOutgoing(
    OutgoingSession &session, TransferErrorCode errorCode, QString diagnostic
)
{
  using ::relaydesk::transfer::TransferControlStateMachine;
  using ::relaydesk::transfer::TransferState;

  if (session.control != nullptr &&
      !TransferControlStateMachine::isTerminal(session.control->snapshot().state)) {
    (void)session.control->fail(errorCode);
    session.snapshot = session.control->snapshot();
  } else if (!TransferControlStateMachine::isTerminal(session.snapshot.state)) {
    session.snapshot.state = TransferState::Failed;
    session.snapshot.errorCode = errorCode;
    session.snapshot.canCancel = false;
    session.snapshot.canRetry = true;
    session.snapshot.finishedUtc = QDateTime::currentDateTimeUtc();
  }
  session.cancelled = true;
  Q_EMIT transferChanged(session.snapshot);
  if (!diagnostic.isEmpty()) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::ProtocolFailed, FileTlsError::ProtocolError, std::move(diagnostic)
    );
  }
}

void FileTransferRuntime::publishOperation(
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

FileTransferRuntime::OutgoingSession *
FileTransferRuntime::outgoing(const ::relaydesk::transfer::TransferId &transferId) const
{
  return m_outgoing.value(transferId, nullptr);
}

void FileTransferRuntime::removeConnection(FileTlsConnection &connection)
{
  auto context = m_connections.find(&connection);
  if (context == m_connections.end()) {
    return;
  }
  const auto peer = context->peer;
  const auto expectedPeer = context->expectedPeer;
  const bool wasReady = context->negotiated.has_value();
  m_connections.erase(context);
  if (peer.has_value() && m_peerConnections.value(*peer, nullptr) == &connection) {
    m_peerConnections.remove(*peer);
    if (wasReady) {
      for (auto *session : std::as_const(m_outgoing)) {
        if (session == nullptr || session->peer != *peer || session->cancelled ||
            ::relaydesk::transfer::TransferControlStateMachine::isTerminal(session->snapshot.state)) {
          continue;
        }
        markOutgoingConnectionLost(*session);
      }
      if (auto *incoming = m_incoming.get(); incoming != nullptr) {
        incoming->peerDisconnected(*peer);
      }
      Q_EMIT peerDisconnected(*peer);
    }
  }
  if (expectedPeer.has_value()) {
    if (auto *client = m_clients.take(*expectedPeer); client != nullptr) {
      client->deleteLater();
    }
  }
}

void FileTransferRuntime::failConnection(
    FileTlsConnection &connection, FileTransferRuntimeError error, FileTlsError transportError,
    QString diagnostic
)
{
  if (!m_connections.contains(&connection)) {
    return;
  }
  Q_EMIT errorOccurred(error, transportError, diagnostic);
  connection.close();
}

bool FileTransferRuntime::publishFileEndpoint(QString *diagnostic)
{
  if (!isRunning() || m_incoming == nullptr ||
      !m_options.localCapabilities.canReceiveFiles()) {
    return m_discoveryRuntime.setFileEndpoint(FileEndpointAnnouncement::disabled(), diagnostic);
  }
  return m_discoveryRuntime.setFileEndpoint(
      FileEndpointAnnouncement{
          .port = listeningPort(), .fileV1 = true, .folderV1 = true, .resumeV1 = true
      },
      diagnostic
  );
}

} // namespace deskflow::relaydesk
