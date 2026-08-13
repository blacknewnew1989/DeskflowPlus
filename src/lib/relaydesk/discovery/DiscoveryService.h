/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/discovery/DiscoveryCodec.h"
#include "relaydesk/discovery/FileEndpointAnnouncement.h"

#include <QHostAddress>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QtTypes>

#include <functional>

namespace deskflow::relaydesk {

inline constexpr quint16 kDefaultDiscoveryPort = 24802;
inline constexpr int kDefaultDiscoveryAnnouncementIntervalMs = 4000;

struct DiscoveryInterface
{
  QString name;
  int index = 0;
  QHostAddress address;
  QHostAddress broadcastAddress;
  bool isUp = false;
  bool isRunning = false;
  bool isLoopback = false;

  bool operator==(const DiscoveryInterface &) const = default;
};

struct DiscoveryServiceSettings
{
  quint16 port = kDefaultDiscoveryPort;
  int announcementIntervalMs = kDefaultDiscoveryAnnouncementIntervalMs;
};

enum class DiscoveryServiceError
{
  InvalidSettings,
  BindFailed,
  EncodeFailed,
  NoUsableInterfaces,
  SendFailed,
  ReceiveFailed,
  InvalidDatagram,
};

class DiscoveryService final : public QObject
{
  Q_OBJECT

public:
  using InterfaceProvider = std::function<QList<DiscoveryInterface>()>;
  using DatagramSender = std::function<qint64(
      const QByteArray &, const QHostAddress &, quint16, const QHostAddress &, QString *
  )>;

  explicit DiscoveryService(
      DeviceInfo localDevice, DiscoveryServiceSettings settings = {}, InterfaceProvider interfaceProvider = {},
      DatagramSender datagramSender = {}, QObject *parent = nullptr
  );

  [[nodiscard]] bool start(QString *errorMessage = nullptr);
  void stop();
  [[nodiscard]] bool announceNow(QString *errorMessage = nullptr);
  [[nodiscard]] qint64 sendPeerDatagram(
      const QByteArray &datagram, const QHostAddress &destination, quint16 port,
      QString *errorMessage = nullptr
  );
  [[nodiscard]] bool setFileEndpoint(
      FileEndpointAnnouncement announcement, QString *errorMessage = nullptr
  );

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] quint16 boundPort() const;
  [[nodiscard]] quint16 destinationPort() const;
  [[nodiscard]] const DeviceInfo &localDevice() const;

  [[nodiscard]] static QList<DiscoveryInterface> systemInterfaces();

Q_SIGNALS:
  void started(quint16 port);
  void stopped();
  void advertisementReceived(DeviceInfo device, QHostAddress senderAddress);
  void unrecognizedDatagramReceived(QByteArray datagram, QHostAddress senderAddress, quint16 senderPort);
  void errorOccurred(DiscoveryServiceError error, QString diagnostic);

private Q_SLOTS:
  void processPendingDatagrams();

private:
  [[nodiscard]] static qint64 sendDatagram(
      const QByteArray &datagram, const QHostAddress &destination, quint16 port,
      const QHostAddress &interfaceAddress, QString *errorMessage
  );
  void reportError(DiscoveryServiceError error, const QString &diagnostic, QString *errorMessage = nullptr);

  DeviceInfo m_localDevice;
  DiscoveryServiceSettings m_settings;
  InterfaceProvider m_interfaceProvider;
  DatagramSender m_datagramSender;
  QUdpSocket m_receiveSocket;
  QTimer m_announcementTimer;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::DiscoveryServiceError)
