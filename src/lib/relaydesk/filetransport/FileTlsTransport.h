/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/Protocol.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QHostAddress>
#include <QObject>
#include <QString>

#include <optional>

class QSslServer;
class QSslSocket;
class QTimer;

namespace deskflow::relaydesk {

inline constexpr quint64 kDefaultFileTlsWriteHighWaterBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr int kDefaultFileTlsHandshakeTimeoutMs = 5'000;

enum class FileTlsError
{
  None,
  InvalidIdentity,
  TlsUnavailable,
  ListenFailed,
  ConnectionFailed,
  HandshakeTimeout,
  HandshakeFailed,
  PeerCertificateMissing,
  UnknownPeer,
  RevokedPeer,
  FingerprintChanged,
  HelloInvalid,
  ProtocolError,
  ReceiveLimitExceeded,
  WriteLimitExceeded,
  NotAuthenticated,
};

struct FileTlsSettings
{
  ::relaydesk::transfer::ProtocolLimits protocolLimits;
  quint64 maxQueuedWriteBytes = kDefaultFileTlsWriteHighWaterBytes;
  int handshakeTimeoutMs = kDefaultFileTlsHandshakeTimeoutMs;
};

class FileTlsConnection final : public QObject
{
  Q_OBJECT

public:
  ~FileTlsConnection() override;

  [[nodiscard]] bool isAuthenticated() const noexcept;
  [[nodiscard]] std::optional<DeviceId> peerDeviceId() const;
  [[nodiscard]] FileTlsError sendFrame(const ::relaydesk::transfer::Frame &frame, QString *diagnostic = nullptr);
  void close();

Q_SIGNALS:
  void authenticated();
  void frameReceived(const ::relaydesk::transfer::Frame &frame);
  void failed(deskflow::relaydesk::FileTlsError error, const QString &diagnostic);
  void disconnected();

private:
  friend class FileTlsClient;
  friend class FileTlsListener;

  FileTlsConnection(
      QSslSocket *socket, DeviceId localDeviceId, QByteArray localFingerprint, const TrustedDeviceStore *trustedDevices,
      FileTlsSettings settings, QObject *parent
  );

  void begin();
  void handleEncrypted();
  void handleReadyRead();
  void handleFrame(::relaydesk::transfer::Frame frame);
  void maybeAuthenticate();
  void fail(FileTlsError error, QString diagnostic);
  [[nodiscard]] bool writeEncoded(QByteArray encoded, QString *diagnostic = nullptr);

  QSslSocket *m_socket = nullptr;
  DeviceId m_localDeviceId;
  QByteArray m_localFingerprint;
  const TrustedDeviceStore *m_trustedDevices = nullptr;
  FileTlsSettings m_settings;
  QTimer *m_handshakeTimer = nullptr;
  QByteArray m_receiveBuffer;
  QByteArray m_peerFingerprint;
  std::optional<DeviceId> m_peerDeviceId;
  bool m_peerHelloVerified = false;
  bool m_peerAccepted = false;
  bool m_authenticated = false;
  bool m_failed = false;
};

class FileTlsListener final : public QObject
{
  Q_OBJECT

public:
  FileTlsListener(
      DeviceId localDeviceId, const TrustedDeviceStore *trustedDevices, QString combinedPemPath,
      FileTlsSettings settings = {}, QObject *parent = nullptr
  );
  ~FileTlsListener() override;

  [[nodiscard]] FileTlsError
  listen(const QHostAddress &address = QHostAddress::Any, quint16 port = 0, QString *diagnostic = nullptr);
  void close();
  [[nodiscard]] bool isListening() const;
  [[nodiscard]] quint16 serverPort() const;

Q_SIGNALS:
  void connectionCreated(deskflow::relaydesk::FileTlsConnection *connection);
  void failed(deskflow::relaydesk::FileTlsError error, const QString &diagnostic);

private:
  DeviceId m_localDeviceId;
  const TrustedDeviceStore *m_trustedDevices = nullptr;
  QString m_combinedPemPath;
  FileTlsSettings m_settings;
  QSslServer *m_server = nullptr;
};

class FileTlsClient final : public QObject
{
  Q_OBJECT

public:
  FileTlsClient(
      DeviceId localDeviceId, const TrustedDeviceStore *trustedDevices, QString combinedPemPath,
      FileTlsSettings settings = {}, QObject *parent = nullptr
  );
  ~FileTlsClient() override;

  [[nodiscard]] FileTlsError connectToHost(const QHostAddress &address, quint16 port, QString *diagnostic = nullptr);
  [[nodiscard]] FileTlsConnection *connection() const noexcept;
  void close();

Q_SIGNALS:
  void connectionCreated(deskflow::relaydesk::FileTlsConnection *connection);
  void failed(deskflow::relaydesk::FileTlsError error, const QString &diagnostic);

private:
  DeviceId m_localDeviceId;
  const TrustedDeviceStore *m_trustedDevices = nullptr;
  QString m_combinedPemPath;
  FileTlsSettings m_settings;
  FileTlsConnection *m_connection = nullptr;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::FileTlsError)
