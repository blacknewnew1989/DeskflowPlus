/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingService.h"

#include <QNetworkDatagram>

#include <utility>

namespace deskflow::relaydesk {

PairingService::PairingService(
    DeviceInfo localDevice, TrustedDeviceStore &trustedDevices, PairingOptions options,
    PairingManager::Clock clock, PairingManager::SasGenerator sasGenerator,
    PairingManager::DatagramSender datagramSender, QObject *parent
)
    : IPairingService(parent), m_datagramSender(std::move(datagramSender)), m_socket(this),
      m_manager(
          std::move(localDevice), trustedDevices,
          [this](QByteArray bytes, PairingEndpoint endpoint) {
            return sendDatagram(std::move(bytes), std::move(endpoint));
          },
          options, std::move(clock), std::move(sasGenerator), this
      ),
      m_expiryTimer(this)
{
  connect(&m_socket, &QUdpSocket::readyRead, this, &PairingService::readPendingDatagrams);
  connect(&m_manager, &PairingManager::pairingChanged, this, &IPairingService::pairingChanged);
  connect(&m_manager, &PairingManager::operationFailed, this, &IPairingService::operationFailed);
  m_expiryTimer.setInterval(500);
  connect(&m_expiryTimer, &QTimer::timeout, &m_manager, &PairingManager::expireIfNeeded);
  m_expiryTimer.start();
}

PairingOperationResult PairingService::listen(const QHostAddress &address, quint16 port)
{
  if (m_socket.state() != QAbstractSocket::UnconnectedState) {
    m_socket.close();
  }
  if (!m_socket.bind(address, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    return report({
        .error = PairingOperationError::InvalidEndpoint,
        .diagnostic = QStringLiteral("could not listen for pairing datagrams: %1").arg(m_socket.errorString()),
    });
  }
  m_expiryTimer.start();
  return {};
}

void PairingService::close()
{
  m_expiryTimer.stop();
  m_socket.close();
}

bool PairingService::isListening() const
{
  return m_socket.state() == QAbstractSocket::BoundState;
}

quint16 PairingService::localPort() const
{
  return m_socket.localPort();
}

PairingOperationResult PairingService::receiveDatagram(QByteArrayView bytes, PairingEndpoint source)
{
  return report(m_manager.receiveDatagram(bytes, std::move(source)));
}

bool PairingService::expireIfNeeded()
{
  return m_manager.expireIfNeeded();
}

PairingOperationResult PairingService::startPairing(
    DeviceSnapshot peer, QByteArray peerFingerprintSha256, PairingEndpoint endpoint
)
{
  return report(m_manager.startPairing(
      std::move(peer), std::move(peerFingerprintSha256), std::move(endpoint)
  ));
}

PairingOperationResult PairingService::confirmMatchingSas(const QUuid &sessionId)
{
  return report(m_manager.confirmMatchingSas(sessionId));
}

PairingOperationResult PairingService::submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits)
{
  return report(m_manager.submitDisplayedSas(sessionId, sixDigits));
}

PairingOperationResult PairingService::cancel(const QUuid &sessionId)
{
  return report(m_manager.cancel(sessionId));
}

PairingOperationResult PairingService::revoke(const DeviceId &deviceId)
{
  return report(m_manager.revoke(deviceId));
}

std::optional<PairingSnapshot> PairingService::snapshot() const
{
  return m_manager.snapshot();
}

void PairingService::readPendingDatagrams()
{
  while (m_socket.hasPendingDatagrams()) {
    const auto datagram = m_socket.receiveDatagram();
    const auto senderPort = datagram.senderPort();
    receiveDatagram(
        datagram.data(),
        {
            .address = datagram.senderAddress(),
            .port = senderPort > 0 && senderPort <= 65535 ? quint16(senderPort) : quint16(0),
        }
    );
  }
}

PairingTransportResult PairingService::sendDatagram(QByteArray bytes, PairingEndpoint endpoint)
{
  if (m_datagramSender) {
    return m_datagramSender(std::move(bytes), std::move(endpoint));
  }
  const auto written = m_socket.writeDatagram(bytes, endpoint.address, endpoint.port);
  if (written != bytes.size()) {
    return {
        .ok = false,
        .diagnostic = QStringLiteral("could not send pairing datagram: %1").arg(m_socket.errorString()),
    };
  }
  return {.ok = true};
}

PairingOperationResult PairingService::report(PairingOperationResult result)
{
  if (!result.ok() && result.error != PairingOperationError::DuplicateMessage) {
    Q_EMIT operationFailed(result);
  }
  return result;
}

} // namespace deskflow::relaydesk
