/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/DiscoveryService.h"

#include <QAbstractSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QSet>
#include <QStringList>

#include <utility>

namespace deskflow::relaydesk {

namespace {
void setError(QString *errorMessage, const QString &message)
{
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
}

QString interfaceDescription(const DiscoveryInterface &interface)
{
  return QStringLiteral("%1 (%2)").arg(interface.name, interface.address.toString());
}
} // namespace

DiscoveryService::DiscoveryService(
    DeviceInfo localDevice, DiscoveryServiceSettings settings, InterfaceProvider interfaceProvider,
    DatagramSender datagramSender, QObject *parent
)
    : QObject(parent), m_localDevice(std::move(localDevice)), m_settings(settings),
      m_interfaceProvider(std::move(interfaceProvider)), m_datagramSender(std::move(datagramSender)),
      m_receiveSocket(this), m_announcementTimer(this)
{
  if (!m_interfaceProvider) {
    m_interfaceProvider = &DiscoveryService::systemInterfaces;
  }
  if (!m_datagramSender) {
    m_datagramSender = &DiscoveryService::sendDatagram;
  }

  if (m_settings.announcementIntervalMs > 0) {
    m_announcementTimer.setInterval(m_settings.announcementIntervalMs);
  }
  m_announcementTimer.setTimerType(Qt::CoarseTimer);
  connect(&m_announcementTimer, &QTimer::timeout, this, [this]() { static_cast<void>(announceNow()); });
  connect(&m_receiveSocket, &QUdpSocket::readyRead, this, &DiscoveryService::processPendingDatagrams);
}

bool DiscoveryService::start(QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  if (isRunning()) {
    return true;
  }
  if (m_settings.announcementIntervalMs <= 0) {
    reportError(
        DiscoveryServiceError::InvalidSettings, QStringLiteral("Discovery announcement interval must be positive"),
        errorMessage
    );
    return false;
  }

  const auto bindFlags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
  if (!m_receiveSocket.bind(QHostAddress::AnyIPv4, m_settings.port, bindFlags)) {
    reportError(
        DiscoveryServiceError::BindFailed,
        QStringLiteral("Unable to bind UDP discovery port %1: %2")
            .arg(m_settings.port)
            .arg(m_receiveSocket.errorString()),
        errorMessage
    );
    return false;
  }

  m_announcementTimer.start();
  Q_EMIT started(boundPort());
  static_cast<void>(announceNow());
  return true;
}

void DiscoveryService::stop()
{
  if (!isRunning()) {
    return;
  }
  m_announcementTimer.stop();
  m_receiveSocket.close();
  Q_EMIT stopped();
}

bool DiscoveryService::announceNow(QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }

  QString encodeError;
  const auto datagram = DiscoveryCodec::encodeAdvertisement(m_localDevice, &encodeError);
  if (datagram.isEmpty()) {
    reportError(
        DiscoveryServiceError::EncodeFailed,
        QStringLiteral("Unable to encode discovery advertisement: %1").arg(encodeError),
        errorMessage
    );
    return false;
  }

  QList<DiscoveryInterface> usableInterfaces;
  QSet<QString> destinations;
  for (const auto &interface : m_interfaceProvider()) {
    if (!interface.isUp || !interface.isRunning || interface.isLoopback ||
        interface.address.protocol() != QAbstractSocket::IPv4Protocol || interface.address.isNull() ||
        interface.broadcastAddress.protocol() != QAbstractSocket::IPv4Protocol || interface.broadcastAddress.isNull()) {
      continue;
    }

    const auto destinationKey =
        QStringLiteral("%1|%2").arg(interface.address.toString(), interface.broadcastAddress.toString());
    if (!destinations.contains(destinationKey)) {
      destinations.insert(destinationKey);
      usableInterfaces.append(interface);
    }
  }

  if (usableInterfaces.isEmpty()) {
    reportError(
        DiscoveryServiceError::NoUsableInterfaces,
        QStringLiteral("No active non-loopback IPv4 interface has a broadcast address"), errorMessage
    );
    return false;
  }

  bool allSent = true;
  QStringList failures;
  const auto port = destinationPort();
  if (port == 0) {
    reportError(
        DiscoveryServiceError::InvalidSettings,
        QStringLiteral("Discovery destination port is unavailable before an ephemeral listener is started"),
        errorMessage
    );
    return false;
  }
  for (const auto &interface : usableInterfaces) {
    QString sendError;
    const auto written =
        m_datagramSender(datagram, interface.broadcastAddress, port, interface.address, &sendError);
    if (written != datagram.size()) {
      allSent = false;
      failures.append(
          QStringLiteral("%1: %2")
              .arg(interfaceDescription(interface), sendError.isEmpty() ? QStringLiteral("short UDP write") : sendError)
      );
    }
  }

  if (!allSent) {
    reportError(
        DiscoveryServiceError::SendFailed,
        QStringLiteral("Discovery advertisement failed on %1").arg(failures.join(QStringLiteral("; "))), errorMessage
    );
  }
  return allSent;
}

bool DiscoveryService::setFileEndpoint(quint16 port, bool folderV1, bool resumeV1, QString *errorMessage)
{
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }
  m_localDevice.filePort = port;
  m_localDevice.capabilities.fileV1 = port != 0;
  m_localDevice.capabilities.folderV1 = port != 0 && folderV1;
  m_localDevice.capabilities.resumeV1 = port != 0 && resumeV1;
  return !isRunning() || announceNow(errorMessage);
}

bool DiscoveryService::isRunning() const
{
  return m_receiveSocket.state() == QAbstractSocket::BoundState;
}

quint16 DiscoveryService::boundPort() const
{
  return isRunning() ? m_receiveSocket.localPort() : 0;
}

quint16 DiscoveryService::destinationPort() const
{
  return m_settings.port != 0 ? m_settings.port : boundPort();
}

const DeviceInfo &DiscoveryService::localDevice() const
{
  return m_localDevice;
}

QList<DiscoveryInterface> DiscoveryService::systemInterfaces()
{
  QList<DiscoveryInterface> result;
  for (const auto &networkInterface : QNetworkInterface::allInterfaces()) {
    const auto flags = networkInterface.flags();
    for (const auto &entry : networkInterface.addressEntries()) {
      if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
        continue;
      }
      result.append({
          .name = networkInterface.humanReadableName(),
          .index = networkInterface.index(),
          .address = entry.ip(),
          .broadcastAddress = entry.broadcast(),
          .isUp = flags.testFlag(QNetworkInterface::IsUp),
          .isRunning = flags.testFlag(QNetworkInterface::IsRunning),
          .isLoopback = flags.testFlag(QNetworkInterface::IsLoopBack),
      });
    }
  }
  return result;
}

qint64 DiscoveryService::sendDatagram(
    const QByteArray &datagram, const QHostAddress &destination, quint16 port, const QHostAddress &interfaceAddress,
    QString *errorMessage
)
{
  QUdpSocket socket;
  if (!socket.bind(interfaceAddress, 0)) {
    setError(errorMessage, QStringLiteral("unable to bind interface: %1").arg(socket.errorString()));
    return -1;
  }
  const auto written = socket.writeDatagram(datagram, destination, port);
  if (written < 0) {
    setError(errorMessage, socket.errorString());
  }
  return written;
}

void DiscoveryService::processPendingDatagrams()
{
  constexpr int maximumDatagramsPerTurn = 64;
  int processed = 0;
  while (m_receiveSocket.hasPendingDatagrams() && processed < maximumDatagramsPerTurn) {
    ++processed;
    const auto networkDatagram = m_receiveSocket.receiveDatagram(kMaximumDiscoveryDatagramBytes + 1);
    if (!networkDatagram.isValid()) {
      reportError(
          DiscoveryServiceError::ReceiveFailed,
          QStringLiteral("Unable to read UDP discovery datagram: %1").arg(m_receiveSocket.errorString())
      );
      continue;
    }

    const auto decoded = DiscoveryCodec::decode(networkDatagram.data());
    if (!decoded.isSuccess()) {
      reportError(DiscoveryServiceError::InvalidDatagram, decoded.diagnostic);
      continue;
    }
    Q_EMIT advertisementReceived(decoded.datagram->device, networkDatagram.senderAddress());
  }

  if (m_receiveSocket.hasPendingDatagrams()) {
    QTimer::singleShot(0, this, &DiscoveryService::processPendingDatagrams);
  }
}

void DiscoveryService::reportError(
    DiscoveryServiceError error, const QString &diagnostic, QString *errorMessage
)
{
  setError(errorMessage, diagnostic);
  Q_EMIT errorOccurred(error, diagnostic);
}

} // namespace deskflow::relaydesk
