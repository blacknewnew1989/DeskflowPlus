/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QAbstractSocket>
#include <QThread>

#include <utility>

namespace deskflow::relaydesk {
namespace {
using ::relaydesk::transfer::CapabilityCodec;
using ::relaydesk::transfer::CapabilityNegotiator;
using ::relaydesk::transfer::Frame;
using ::relaydesk::transfer::MessageType;

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

FileTransferRuntime::FileTransferRuntime(
    DeviceId localDeviceId, const TrustedDeviceStore &trustedDevices, DeviceDiscoveryRuntime &discoveryRuntime,
    QString combinedPemPath, FileTransferRuntimeOptions options, QObject *parent
)
    : QObject(parent), m_localDeviceId(std::move(localDeviceId)), m_trustedDevices(trustedDevices),
      m_discoveryRuntime(discoveryRuntime), m_combinedPemPath(std::move(combinedPemPath)),
      m_options(std::move(options))
{
}

FileTransferRuntime::~FileTransferRuntime()
{
  stop();
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
    return;
  }

  if (!context->negotiated.has_value()) {
    failConnection(
        connection, FileTransferRuntimeError::CapabilityFailed, FileTlsError::ProtocolError,
        QStringLiteral("File channel received a transfer frame before capability negotiation")
    );
    return;
  }
  Q_EMIT protocolFrameReceived(*context->peer, std::move(frame));
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
      port, features.contains(QStringLiteral("folder.v1")), features.contains(QStringLiteral("resume.v1")),
      diagnostic
  );
}

} // namespace deskflow::relaydesk
