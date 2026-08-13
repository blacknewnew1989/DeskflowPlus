/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/transfer/ControlMessageCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/transfer/TransferOfferStateMachine.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QAbstractSocket>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <memory>
#include <utility>

namespace deskflow::relaydesk {
namespace {
using ::relaydesk::transfer::CapabilityCodec;
using ::relaydesk::transfer::CapabilityNegotiator;
using ::relaydesk::transfer::Frame;
using ::relaydesk::transfer::MessageType;
using ::relaydesk::transfer::SenderFrameSinkResult;
using ::relaydesk::transfer::SenderFrameSinkStatus;
using ::relaydesk::transfer::SenderPumpResult;
using ::relaydesk::transfer::TransferFrameSink;
using ::relaydesk::transfer::TransferSenderPump;

constexpr int kManifestBuildFailed = 1001;
constexpr int kOfferFailed = 1002;
constexpr int kSenderFailed = 1003;
constexpr int kPeerRejected = 1004;
constexpr int kPeerFileFailed = 1005;

class CapturingFrameSink final : public TransferFrameSink
{
public:
  [[nodiscard]] quint64 queuedBytes() const noexcept override
  {
    return 0;
  }

  [[nodiscard]] SenderFrameSinkResult submit(const Frame &frame) override
  {
    if (m_frame.has_value()) {
      return {
          .status = SenderFrameSinkStatus::Failed,
          .diagnostic = QStringLiteral("sender worker produced more than one frame per pump"),
      };
    }
    m_frame = frame;
    return {.status = SenderFrameSinkStatus::Accepted};
  }

  [[nodiscard]] std::optional<Frame> take()
  {
    return std::exchange(m_frame, std::nullopt);
  }

private:
  std::optional<Frame> m_frame;
};

struct SenderWorkResult
{
  SenderPumpResult result;
  std::optional<Frame> frame;
  quint64 bytesProduced = 0;
};

class SenderWorkerState final
{
public:
  explicit SenderWorkerState(::relaydesk::transfer::TransferSenderRequest request)
      : m_pump(std::move(request), m_sink)
  {
  }

  [[nodiscard]] SenderWorkResult produce()
  {
    auto result = m_pump.pump();
    return {
        .result = std::move(result),
        .frame = m_sink.take(),
        .bytesProduced = m_pump.bytesProduced(),
    };
  }

private:
  CapturingFrameSink m_sink;
  TransferSenderPump m_pump;
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
  std::optional<Frame> pendingFrame;
  ::relaydesk::transfer::SenderPumpStatus pendingStatus =
      ::relaydesk::transfer::SenderPumpStatus::Progressed;
  quint64 nextManifestPage = 0;
  qsizetype currentEntry = 0;
  quint64 completedBytes = 0;
  quint64 completedFiles = 0;
  bool senderTaskRunning = false;
  bool awaitingFileResult = false;
  bool paused = false;
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
  if (!publishFileEndpoint(m_listener->serverPort(), &publishDiagnostic)) {
    Q_EMIT errorOccurred(
        FileTransferRuntimeError::DiscoveryPublishFailed, FileTlsError::None, publishDiagnostic
    );
  }
  Q_EMIT started(m_listener->serverPort());
  return true;
}

void FileTransferRuntime::stop()
{
  if (QThread::currentThread() != thread() || m_listener == nullptr) {
    return;
  }

  const auto connections = m_connections.keys();
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
  if (!publishFileEndpoint(0, &publishDiagnostic)) {
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
  const auto result = client->connectToHost(*address, peerInfo->filePort, &connectDiagnostic);
  if (result != FileTlsError::None) {
    m_clients.remove(peerDeviceId);
    delete client;
    setDiagnostic(diagnostic, connectDiagnostic);
    Q_EMIT errorOccurred(FileTransferRuntimeError::TransportFailed, result, connectDiagnostic);
    return false;
  }
  return true;
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
    const ::relaydesk::transfer::TransferId &, const ::relaydesk::transfer::ReceiveOptions &
)
{
  // Incoming sessions are composed in the receiver slice. Unknown IDs are
  // intentionally idempotent at the public service boundary.
}

void FileTransferRuntime::reject(
    const ::relaydesk::transfer::TransferId &, ::relaydesk::transfer::RejectReason
)
{
}

void FileTransferRuntime::pause(const ::relaydesk::transfer::TransferId &transferId)
{
  auto *session = outgoing(transferId);
  if (session == nullptr || session->control == nullptr || session->paused || session->cancelled) {
    return;
  }
  const auto result = session->control->pause();
  if (!result.ok()) {
    return;
  }
  session->paused = true;
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);
}

void FileTransferRuntime::resume(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::TransferState;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->control == nullptr || !session->paused || session->cancelled) {
    return;
  }
  const auto resumed = session->control->resume();
  if (!resumed.ok() || !session->control->advance(TransferState::Transferring).ok()) {
    return;
  }
  session->paused = false;
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);
  scheduleSenderPump(transferId);
}

void FileTransferRuntime::cancel(
    const ::relaydesk::transfer::TransferId &transferId,
    const ::relaydesk::transfer::TransferCancelOptions &
)
{
  using ::relaydesk::transfer::TransferState;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->cancelled ||
      TransferState::Cancelled == session->snapshot.state) {
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
}

void FileTransferRuntime::retry(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::TransferState;

  const auto *session = outgoing(transferId);
  if (session == nullptr || session->snapshot.state != TransferState::Failed) {
    return;
  }
  (void)send(session->peer, session->localItems, session->options);
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
  routeTransferFrame(*context->peer, frame);
}

void FileTransferRuntime::routeTransferFrame(const DeviceId &peerDeviceId, const Frame &frame)
{
  switch (frame.type) {
  case MessageType::TransferAccept:
  case MessageType::TransferReject:
    handleOfferResponse(peerDeviceId, frame);
    break;
  case MessageType::FileResult:
    handleFileResult(peerDeviceId, frame);
    break;
  default:
    break;
  }
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
    failOutgoing(
        *session, kManifestBuildFailed, QStringLiteral("relaydesk.transfer.manifest_failed"), result.diagnostic
    );
    return;
  }

  session->manifest = std::move(*result.manifest);
  const auto plan = ManifestPageCodec::plan(*session->manifest);
  if (!plan.ok()) {
    failOutgoing(
        *session, kManifestBuildFailed, QStringLiteral("relaydesk.transfer.manifest_failed"), plan.diagnostic
    );
    return;
  }
  session->pagePlan = *plan.plan;
  session->snapshot.displayName = session->manifest->summary.displayName;
  session->snapshot.progress.totalBytes = session->manifest->summary.totalBytes;
  session->snapshot.progress.totalFiles = session->manifest->summary.fileCount;
  session->control = std::make_unique<TransferControlStateMachine>(session->snapshot);
  const auto initialized = session->control->initialize();
  if (!initialized.ok()) {
    failOutgoing(
        *session, kManifestBuildFailed, QStringLiteral("relaydesk.transfer.manifest_failed"),
        initialized.diagnostic
    );
    return;
  }
  session->snapshot = session->control->snapshot();
  Q_EMIT transferChanged(session->snapshot);

  QString connectDiagnostic;
  if (!connectPeer(session->peer, &connectDiagnostic)) {
    failOutgoing(
        *session, kOfferFailed, QStringLiteral("relaydesk.transfer.peer_unavailable"), connectDiagnostic
    );
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
    if (session != nullptr && session->peer == peerDeviceId && session->manifest.has_value() &&
        session->pagePlan.has_value() && session->offerState == nullptr && !session->cancelled) {
      sendOffer(*session);
    }
  }
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
    failOutgoing(session, kOfferFailed, QStringLiteral("relaydesk.transfer.offer_failed"), begun.diagnostic);
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
    failOutgoing(session, kOfferFailed, QStringLiteral("relaydesk.transfer.offer_failed"), diagnostic);
    return;
  }
  session.offerState = std::move(offerState);
  if (!session.control->advance(TransferState::Offered).ok() ||
      !session.control->advance(TransferState::WaitingForAcceptance).ok()) {
    failOutgoing(
        session, kOfferFailed, QStringLiteral("relaydesk.transfer.offer_failed"),
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
      failOutgoing(
          *session, kOfferFailed, QStringLiteral("relaydesk.transfer.offer_failed"), accepted.diagnostic
      );
      return;
    }
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
    session->snapshot.errorCode = kPeerRejected;
    session->snapshot.errorMessageKey = QStringLiteral("relaydesk.transfer.rejected");
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
      failOutgoing(
          *session, kOfferFailed, QStringLiteral("relaydesk.transfer.manifest_send_failed"), diagnostic
      );
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
    failOutgoing(
        *session, kOfferFailed, QStringLiteral("relaydesk.transfer.manifest_send_failed"), diagnostic
    );
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
        session, kSenderFailed, QStringLiteral("relaydesk.transfer.peer_disconnected"),
        QStringLiteral("Peer file channel is no longer ready")
    );
    return;
  }
  const auto &source = session.manifest->entries.at(session.currentEntry);
  session.sender = std::make_shared<SenderWorkerState>(TransferSenderRequest{
      .transferId = session.id,
      .source = source,
      .streamId = static_cast<quint64>(session.currentEntry + 1),
      .chunkBytes = capabilities->chunkBytes,
  });
  session.awaitingFileResult = false;
  session.pendingFrame.reset();
  if (session.control->snapshot().state == TransferState::Queued &&
      !session.control->advance(TransferState::Transferring).ok()) {
    failOutgoing(
        session, kSenderFailed, QStringLiteral("relaydesk.transfer.sender_failed"),
        QStringLiteral("Outgoing transfer could not enter the transferring state")
    );
    return;
  }
  updateOutgoingProgress(session);
  Q_EMIT transferChanged(session.snapshot);
  scheduleSenderPump(session.id);
}

void FileTransferRuntime::scheduleSenderPump(const ::relaydesk::transfer::TransferId &transferId)
{
  using ::relaydesk::transfer::SenderPumpStatus;

  auto *session = outgoing(transferId);
  if (session == nullptr || session->cancelled || session->paused || session->awaitingFileResult ||
      session->senderTaskRunning || session->sender == nullptr) {
    return;
  }
  auto *connection = m_peerConnections.value(session->peer, nullptr);
  if (connection == nullptr || !isPeerReady(session->peer)) {
    failOutgoing(
        *session, kSenderFailed, QStringLiteral("relaydesk.transfer.peer_disconnected"),
        QStringLiteral("Peer file channel disconnected while sending")
    );
    return;
  }

  if (session->pendingFrame.has_value()) {
    if (connection->queuedWriteBytes() >= connection->writeHighWaterBytes() / 2) {
      QTimer::singleShot(5, this, [this, transferId]() { scheduleSenderPump(transferId); });
      return;
    }
    const auto frameType = session->pendingFrame->type;
    const auto payloadBytes = static_cast<quint64>(session->pendingFrame->payload.size());
    QString diagnostic;
    if (!sendPeerFrame(session->peer, *session->pendingFrame, &diagnostic)) {
      if (connection->queuedWriteBytes() >= connection->writeHighWaterBytes() / 2) {
        QTimer::singleShot(5, this, [this, transferId]() { scheduleSenderPump(transferId); });
        return;
      }
      failOutgoing(
          *session, kSenderFailed, QStringLiteral("relaydesk.transfer.sender_failed"), diagnostic
      );
      return;
    }
    session->pendingFrame.reset();
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

  if (connection->queuedWriteBytes() >= connection->writeHighWaterBytes() / 2) {
    QTimer::singleShot(5, this, [this, transferId]() { scheduleSenderPump(transferId); });
    return;
  }

  session->senderTaskRunning = true;
  const auto worker = session->sender;
  auto *watcher = new QFutureWatcher<SenderWorkResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, transferId, watcher]() {
    auto work = watcher->result();
    watcher->deleteLater();
    dispatchSenderPumpResult(
        transferId, work.result, std::move(work.frame), work.bytesProduced
    );
  });
  watcher->setFuture(QtConcurrent::run(m_workerPool.get(), [worker]() { return worker->produce(); }));
}

void FileTransferRuntime::dispatchSenderPumpResult(
    const ::relaydesk::transfer::TransferId &transferId, const SenderPumpResult &result,
    std::optional<Frame> frame, quint64 bytesProduced
)
{
  using ::relaydesk::transfer::SenderPumpStatus;

  auto *session = outgoing(transferId);
  if (session == nullptr) {
    return;
  }
  session->senderTaskRunning = false;
  if (session->cancelled) {
    return;
  }
  if (result.status == SenderPumpStatus::Failed) {
    failOutgoing(
        *session, kSenderFailed, QStringLiteral("relaydesk.transfer.sender_failed"), result.diagnostic
    );
    return;
  }
  if (result.status == SenderPumpStatus::Backpressured) {
    QTimer::singleShot(5, this, [this, transferId]() { scheduleSenderPump(transferId); });
    return;
  }
  if (frame.has_value()) {
    session->pendingFrame = std::move(frame);
    session->pendingStatus = result.status;
    scheduleSenderPump(transferId);
    return;
  }
  if (result.status == SenderPumpStatus::Finished) {
    session->awaitingFileResult = true;
    return;
  }
  Q_UNUSED(bytesProduced);
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
  if (result->code != FileResultCode::Ok) {
    failOutgoing(
        *session, kPeerFileFailed, QStringLiteral("relaydesk.transfer.peer_file_failed"), result->diagnostic
    );
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
        session, kSenderFailed, QStringLiteral("relaydesk.transfer.sender_failed"),
        QStringLiteral("Outgoing transfer could not complete its state sequence")
    );
    return;
  }
  session.snapshot = session.control->snapshot();
  Q_EMIT transferChanged(session.snapshot);
}

void FileTransferRuntime::failOutgoing(
    OutgoingSession &session, int errorCode, QString errorMessageKey, QString diagnostic
)
{
  using ::relaydesk::transfer::TransferControlStateMachine;
  using ::relaydesk::transfer::TransferState;

  if (session.control != nullptr &&
      !TransferControlStateMachine::isTerminal(session.control->snapshot().state)) {
    (void)session.control->fail(errorCode, errorMessageKey);
    session.snapshot = session.control->snapshot();
  } else if (!TransferControlStateMachine::isTerminal(session.snapshot.state)) {
    session.snapshot.state = TransferState::Failed;
    session.snapshot.errorCode = errorCode;
    session.snapshot.errorMessageKey = std::move(errorMessageKey);
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

bool FileTransferRuntime::publishFileEndpoint(quint16 port, QString *diagnostic)
{
  const auto &features = m_options.localCapabilities.features;
  return m_discoveryRuntime.setFileEndpoint(
      port == 0
          ? FileEndpointAnnouncement::disabled()
          : FileEndpointAnnouncement{
                .port = port,
                .fileV1 = true,
                .folderV1 = features.contains(QStringLiteral("folder.v1")),
                .resumeV1 = features.contains(QStringLiteral("resume.v1")),
            },
      diagnostic
  );
}

} // namespace deskflow::relaydesk
