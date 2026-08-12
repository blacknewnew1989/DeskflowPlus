/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/filetransport/FileTlsTransport.h"

#include "relaydesk/transfer/FrameCodec.h"
#include "relaydesk/transfer/SessionMessageCodec.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TlsPeerPinningPolicy.h"

#include <QDateTime>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslServer>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace deskflow::relaydesk {
namespace {

using ::relaydesk::transfer::AuthResultMessage;
using ::relaydesk::transfer::Frame;
using ::relaydesk::transfer::FrameCodec;
using ::relaydesk::transfer::FrameDecodeStatus;
using ::relaydesk::transfer::HelloMessage;
using ::relaydesk::transfer::MessageType;
using ::relaydesk::transfer::SessionMessageCodec;

bool settingsAreValid(const FileTlsSettings &settings)
{
  const auto &limits = settings.protocolLimits;
  return settings.handshakeTimeoutMs > 0 && settings.maxQueuedWriteBytes > 0 && limits.maxControlMetadataBytes > 0 &&
         limits.maxDataPayloadBytes > 0 &&
         limits.maxFrameBytes >= static_cast<quint64>(::relaydesk::transfer::kFixedHeaderBytes) &&
         limits.maxFrameBytes <= static_cast<quint64>(std::numeric_limits<qsizetype>::max());
}

FileTlsError mapPinningError(PeerPinningError error)
{
  switch (error) {
  case PeerPinningError::None:
    return FileTlsError::None;
  case PeerPinningError::UnknownPeer:
    return FileTlsError::UnknownPeer;
  case PeerPinningError::RevokedPeer:
    return FileTlsError::RevokedPeer;
  case PeerPinningError::FingerprintChanged:
    return FileTlsError::FingerprintChanged;
  }
  return FileTlsError::FingerprintChanged;
}

void setDiagnostic(QString *output, const QString &diagnostic)
{
  if (output != nullptr) {
    *output = diagnostic;
  }
}

} // namespace

FileTlsConnection::FileTlsConnection(
    QSslSocket *socket, DeviceId localDeviceId, QByteArray localFingerprint, const TrustedDeviceStore *trustedDevices,
    FileTlsSettings settings, QObject *parent
)
    : QObject(parent),
      m_socket(socket),
      m_localDeviceId(std::move(localDeviceId)),
      m_localFingerprint(std::move(localFingerprint)),
      m_trustedDevices(trustedDevices),
      m_settings(std::move(settings)),
      m_handshakeTimer(new QTimer(this))
{
  m_socket->setParent(this);
  m_handshakeTimer->setSingleShot(true);
}

FileTlsConnection::~FileTlsConnection() = default;

void FileTlsConnection::begin()
{
  connect(m_socket, &QSslSocket::encrypted, this, &FileTlsConnection::handleEncrypted);
  connect(m_socket, &QSslSocket::readyRead, this, &FileTlsConnection::handleReadyRead);
  connect(m_socket, &QSslSocket::disconnected, this, &FileTlsConnection::disconnected);
  connect(m_handshakeTimer, &QTimer::timeout, this, [this]() {
    fail(FileTlsError::HandshakeTimeout, QStringLiteral("file TLS handshake timed out"));
  });
  connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
    if (!m_failed && error != QAbstractSocket::RemoteHostClosedError) {
      fail(
          m_socket->isEncrypted() ? FileTlsError::ConnectionFailed : FileTlsError::HandshakeFailed,
          QStringLiteral("file TLS socket failed: %1").arg(m_socket->errorString())
      );
    }
  });
  m_handshakeTimer->start(m_settings.handshakeTimeoutMs);
  if (m_socket->isEncrypted()) {
    handleEncrypted();
    if (m_socket->bytesAvailable() > 0) {
      handleReadyRead();
    }
  }
}

bool FileTlsConnection::isAuthenticated() const noexcept
{
  return m_authenticated;
}

std::optional<DeviceId> FileTlsConnection::peerDeviceId() const
{
  return m_peerDeviceId;
}

quint64 FileTlsConnection::queuedWriteBytes() const noexcept
{
  return static_cast<quint64>(std::max<qint64>(0, m_socket->bytesToWrite()));
}

quint64 FileTlsConnection::writeHighWaterBytes() const noexcept
{
  return m_settings.maxQueuedWriteBytes;
}

FileTlsError FileTlsConnection::sendFrame(const Frame &frame, QString *diagnostic)
{
  if (!m_authenticated) {
    setDiagnostic(diagnostic, QStringLiteral("file TLS peer is not authenticated"));
    return FileTlsError::NotAuthenticated;
  }
  QString encodeError;
  QByteArray encoded = FrameCodec::encode(frame, m_settings.protocolLimits, &encodeError);
  if (encoded.isEmpty()) {
    setDiagnostic(diagnostic, encodeError);
    return FileTlsError::ProtocolError;
  }
  if (!writeEncoded(std::move(encoded), diagnostic)) {
    return FileTlsError::WriteLimitExceeded;
  }
  return FileTlsError::None;
}

void FileTlsConnection::close()
{
  m_handshakeTimer->stop();
  m_socket->disconnectFromHost();
}

void FileTlsConnection::handleEncrypted()
{
  if (m_failed || !m_peerFingerprint.isEmpty()) {
    return;
  }
  const QSslCertificate certificate = m_socket->peerCertificate();
  if (certificate.isNull()) {
    fail(FileTlsError::PeerCertificateMissing, QStringLiteral("file TLS peer did not present a certificate"));
    return;
  }
  m_peerFingerprint = certificate.digest(QCryptographicHash::Sha256);
  if (m_peerFingerprint.size() != 32) {
    fail(FileTlsError::PeerCertificateMissing, QStringLiteral("file TLS peer certificate has no SHA-256 digest"));
    return;
  }

  Frame hello;
  hello.type = MessageType::Hello;
  QString encodeError;
  hello.metadata = SessionMessageCodec::encodeHello(
      HelloMessage{
          .deviceId = m_localDeviceId,
          .sessionId = QUuid::createUuid(),
          .appVersion = QStringLiteral("0.1.0"),
          .supportedMajorVersions = {::relaydesk::transfer::kProtocolMajorVersion},
          .certificateFingerprintSha256 = m_localFingerprint,
          .timestampMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
      },
      &encodeError
  );
  if (hello.metadata.isEmpty()) {
    fail(FileTlsError::HelloInvalid, QStringLiteral("could not encode RDFT HELLO: %1").arg(encodeError));
    return;
  }
  QByteArray encoded = FrameCodec::encode(hello, m_settings.protocolLimits, &encodeError);
  if (encoded.isEmpty() || !writeEncoded(std::move(encoded), &encodeError)) {
    fail(FileTlsError::ProtocolError, QStringLiteral("could not send RDFT HELLO: %1").arg(encodeError));
  }
}

void FileTlsConnection::handleReadyRead()
{
  if (m_failed) {
    return;
  }
  const qint64 available = m_socket->bytesAvailable();
  const quint64 buffered = static_cast<quint64>(m_receiveBuffer.size());
  if (available < 0 || buffered > m_settings.protocolLimits.maxFrameBytes ||
      static_cast<quint64>(available) > m_settings.protocolLimits.maxFrameBytes - buffered) {
    fail(FileTlsError::ReceiveLimitExceeded, QStringLiteral("file TLS receive buffer exceeded one bounded frame"));
    return;
  }
  m_receiveBuffer.append(m_socket->read(available));
  while (!m_receiveBuffer.isEmpty() && !m_failed) {
    Frame frame;
    const auto result = FrameCodec::tryDecode(m_receiveBuffer, frame, m_settings.protocolLimits);
    if (result.status == FrameDecodeStatus::NeedMoreData) {
      break;
    }
    if (result.status == FrameDecodeStatus::ProtocolError) {
      fail(FileTlsError::ProtocolError, result.diagnostic);
      break;
    }
    handleFrame(std::move(frame));
  }
}

void FileTlsConnection::handleFrame(Frame frame)
{
  if (!m_authenticated) {
    if (!frame.payload.isEmpty() || frame.streamId != 0) {
      fail(FileTlsError::HelloInvalid, QStringLiteral("RDFT handshake frames must use stream zero without payload"));
      return;
    }
    if (frame.type == MessageType::AuthResult) {
      if (m_peerAccepted) {
        fail(FileTlsError::HelloInvalid, QStringLiteral("duplicate RDFT AUTH_RESULT is not allowed"));
        return;
      }
      const auto result = SessionMessageCodec::decodeAuthResult(frame.type, frame.metadata);
      if (!result.ok() || !result.message->accepted) {
        fail(
            FileTlsError::HandshakeFailed,
            result.ok() ? QStringLiteral("file TLS peer rejected authentication") : result.diagnostic
        );
        return;
      }
      m_peerAccepted = true;
      maybeAuthenticate();
      return;
    }
    if (frame.type != MessageType::Hello || m_peerHelloVerified) {
      fail(FileTlsError::HelloInvalid, QStringLiteral("the file TLS handshake expected one RDFT HELLO"));
      return;
    }
    const auto hello = SessionMessageCodec::decodeHello(frame.type, frame.metadata);
    if (!hello.ok() || hello.message->certificateFingerprintSha256 != m_peerFingerprint) {
      fail(
          FileTlsError::HelloInvalid,
          hello.ok() ? QStringLiteral("RDFT HELLO fingerprint does not match the TLS certificate") : hello.diagnostic
      );
      return;
    }
    const PeerPinningResult pinning =
        TlsPeerPinningPolicy::verify(*m_trustedDevices, hello.message->deviceId, m_peerFingerprint);
    if (!pinning.ok()) {
      fail(mapPinningError(pinning.error), pinning.diagnostic);
      return;
    }
    m_peerDeviceId = hello.message->deviceId;
    m_peerHelloVerified = true;
    QString encodeError;
    Frame resultFrame;
    resultFrame.type = MessageType::AuthResult;
    resultFrame.metadata = SessionMessageCodec::encodeAuthResult(AuthResultMessage{.accepted = true}, &encodeError);
    QByteArray encoded = FrameCodec::encode(resultFrame, m_settings.protocolLimits, &encodeError);
    if (resultFrame.metadata.isEmpty() || encoded.isEmpty() || !writeEncoded(std::move(encoded), &encodeError)) {
      fail(FileTlsError::ProtocolError, QStringLiteral("could not send RDFT AUTH_RESULT: %1").arg(encodeError));
      return;
    }
    maybeAuthenticate();
    return;
  }
  if (frame.type == MessageType::Hello || frame.type == MessageType::AuthResult) {
    fail(FileTlsError::HelloInvalid, QStringLiteral("duplicate RDFT session handshake message is not allowed"));
    return;
  }
  Q_EMIT frameReceived(frame);
}

void FileTlsConnection::maybeAuthenticate()
{
  if (m_authenticated || !m_peerHelloVerified || !m_peerAccepted) {
    return;
  }
  m_authenticated = true;
  m_handshakeTimer->stop();
  Q_EMIT authenticated();
}

void FileTlsConnection::fail(FileTlsError error, QString diagnostic)
{
  if (m_failed) {
    return;
  }
  m_failed = true;
  m_handshakeTimer->stop();
  Q_EMIT failed(error, diagnostic);
  m_socket->abort();
}

bool FileTlsConnection::writeEncoded(QByteArray encoded, QString *diagnostic)
{
  const quint64 pending = queuedWriteBytes();
  if (static_cast<quint64>(encoded.size()) > m_settings.maxQueuedWriteBytes ||
      pending > m_settings.maxQueuedWriteBytes - static_cast<quint64>(encoded.size())) {
    setDiagnostic(diagnostic, QStringLiteral("file TLS write high-water limit exceeded"));
    return false;
  }
  if (m_socket->write(encoded) != encoded.size()) {
    setDiagnostic(diagnostic, QStringLiteral("file TLS socket rejected the encoded frame"));
    return false;
  }
  return true;
}

FileTlsListener::FileTlsListener(
    DeviceId localDeviceId, const TrustedDeviceStore *trustedDevices, QString combinedPemPath, FileTlsSettings settings,
    QObject *parent
)
    : QObject(parent),
      m_localDeviceId(std::move(localDeviceId)),
      m_trustedDevices(trustedDevices),
      m_combinedPemPath(std::move(combinedPemPath)),
      m_settings(std::move(settings))
{
}

FileTlsListener::~FileTlsListener() = default;

FileTlsError FileTlsListener::listen(const QHostAddress &address, quint16 port, QString *diagnostic)
{
  if (m_server != nullptr || m_trustedDevices == nullptr || !settingsAreValid(m_settings)) {
    setDiagnostic(diagnostic, QStringLiteral("file TLS listener configuration is invalid"));
    return FileTlsError::InvalidIdentity;
  }
  if (!QSslSocket::supportsSsl()) {
    setDiagnostic(diagnostic, QStringLiteral("Qt TLS backend is unavailable"));
    return FileTlsError::TlsUnavailable;
  }
  QString identityError;
  const auto configuration = TlsIdentityAdapter::loadConfiguration(m_combinedPemPath, &identityError);
  const auto identity = TlsIdentityAdapter::inspect(m_combinedPemPath);
  if (!configuration.has_value() || !identity.ok()) {
    setDiagnostic(diagnostic, identityError.isEmpty() ? identity.diagnostic : identityError);
    return FileTlsError::InvalidIdentity;
  }

  m_server = new QSslServer(this);
  m_server->setSslConfiguration(*configuration);
  m_server->setHandshakeTimeout(m_settings.handshakeTimeoutMs);
  m_server->setMaxPendingConnections(4);
  connect(
      m_server, &QSslServer::sslErrors, this,
      [](QSslSocket *socket, const QList<QSslError> &errors) { socket->ignoreSslErrors(errors); }, Qt::DirectConnection
  );
  connect(
      m_server, &QSslServer::errorOccurred, this,
      [this](QSslSocket *, QAbstractSocket::SocketError socketError) {
        Q_EMIT failed(
            socketError == QAbstractSocket::SocketTimeoutError ? FileTlsError::HandshakeTimeout
                                                               : FileTlsError::HandshakeFailed,
            QStringLiteral("incoming file TLS handshake failed")
        );
      },
      Qt::DirectConnection
  );
  connect(m_server, &QSslServer::pendingConnectionAvailable, this, [this, identity]() {
    while (m_server->hasPendingConnections()) {
      auto *socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection());
      if (socket == nullptr) {
        continue;
      }
      auto *connection = new FileTlsConnection(
          socket, m_localDeviceId, identity.fingerprintSha256, m_trustedDevices, m_settings, this
      );
      Q_EMIT connectionCreated(connection);
      connection->begin();
    }
  });
  if (!m_server->listen(address, port)) {
    const QString error = m_server->errorString();
    m_server->deleteLater();
    m_server = nullptr;
    setDiagnostic(diagnostic, error);
    return FileTlsError::ListenFailed;
  }
  return FileTlsError::None;
}

void FileTlsListener::close()
{
  if (m_server != nullptr) {
    m_server->close();
  }
}

bool FileTlsListener::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

quint16 FileTlsListener::serverPort() const
{
  return m_server == nullptr ? 0 : m_server->serverPort();
}

FileTlsClient::FileTlsClient(
    DeviceId localDeviceId, const TrustedDeviceStore *trustedDevices, QString combinedPemPath, FileTlsSettings settings,
    QObject *parent
)
    : QObject(parent),
      m_localDeviceId(std::move(localDeviceId)),
      m_trustedDevices(trustedDevices),
      m_combinedPemPath(std::move(combinedPemPath)),
      m_settings(std::move(settings))
{
}

FileTlsClient::~FileTlsClient() = default;

FileTlsError FileTlsClient::connectToHost(const QHostAddress &address, quint16 port, QString *diagnostic)
{
  if (m_connection != nullptr || m_trustedDevices == nullptr || !settingsAreValid(m_settings) || port == 0) {
    setDiagnostic(diagnostic, QStringLiteral("file TLS client configuration is invalid"));
    return FileTlsError::InvalidIdentity;
  }
  if (!QSslSocket::supportsSsl()) {
    setDiagnostic(diagnostic, QStringLiteral("Qt TLS backend is unavailable"));
    return FileTlsError::TlsUnavailable;
  }
  QString identityError;
  const auto configuration = TlsIdentityAdapter::loadConfiguration(m_combinedPemPath, &identityError);
  const auto identity = TlsIdentityAdapter::inspect(m_combinedPemPath);
  if (!configuration.has_value() || !identity.ok()) {
    setDiagnostic(diagnostic, identityError.isEmpty() ? identity.diagnostic : identityError);
    return FileTlsError::InvalidIdentity;
  }

  auto *socket = new QSslSocket;
  socket->setSslConfiguration(*configuration);
  connect(
      socket, &QSslSocket::sslErrors, socket,
      [socket](const QList<QSslError> &errors) { socket->ignoreSslErrors(errors); }, Qt::DirectConnection
  );
  m_connection =
      new FileTlsConnection(socket, m_localDeviceId, identity.fingerprintSha256, m_trustedDevices, m_settings, this);
  connect(m_connection, &FileTlsConnection::failed, this, &FileTlsClient::failed);
  Q_EMIT connectionCreated(m_connection);
  m_connection->begin();
  socket->connectToHostEncrypted(address.toString(), port);
  return FileTlsError::None;
}

FileTlsConnection *FileTlsClient::connection() const noexcept
{
  return m_connection;
}

void FileTlsClient::close()
{
  if (m_connection != nullptr) {
    m_connection->close();
  }
}

} // namespace deskflow::relaydesk
