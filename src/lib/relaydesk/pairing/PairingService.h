/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingManager.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <optional>

namespace deskflow::relaydesk {

inline constexpr quint16 kDefaultPairingPort = 24802;

/** Internal UDP transport; GUI code consumes IPairingService via PairingTrustRuntime. */
class PairingService final : public QObject
{
  Q_OBJECT

public:
  PairingService(
      DeviceInfo localDevice, TrustedDeviceStore &trustedDevices, PairingOptions options = {},
      PairingManager::Clock clock = {}, PairingManager::SasGenerator sasGenerator = {},
      PairingManager::DatagramSender datagramSender = {}, QObject *parent = nullptr
  );

  Q_DISABLE_COPY_MOVE(PairingService)

  [[nodiscard]] PairingOperationResult listen(
      const QHostAddress &address = QHostAddress::AnyIPv4, quint16 port = kDefaultPairingPort
  );
  void close();
  [[nodiscard]] bool isListening() const;
  [[nodiscard]] quint16 localPort() const;
  PairingOperationResult receiveDatagram(QByteArrayView bytes, PairingEndpoint source);
  [[nodiscard]] bool expireIfNeeded();

  PairingOperationResult startPairing(
      DeviceSnapshot peer, QByteArray peerFingerprintSha256, PairingEndpoint endpoint
  );
  PairingOperationResult confirmMatchingSas(const QUuid &sessionId);
  PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits);
  PairingOperationResult cancel(const QUuid &sessionId);
  PairingOperationResult revoke(const DeviceId &deviceId);
  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const;
  [[nodiscard]] std::optional<QByteArray> pendingFingerprint(const QUuid &sessionId) const;

Q_SIGNALS:
  void pairingChanged(PairingSnapshot snapshot);
  void operationFailed(PairingOperationResult result);

private Q_SLOTS:
  void readPendingDatagrams();

private:
  [[nodiscard]] PairingTransportResult sendDatagram(QByteArray bytes, PairingEndpoint endpoint);
  PairingOperationResult report(PairingOperationResult result);

  PairingManager::DatagramSender m_datagramSender;
  QUdpSocket m_socket;
  PairingManager m_manager;
  QTimer m_expiryTimer;
};

} // namespace deskflow::relaydesk
