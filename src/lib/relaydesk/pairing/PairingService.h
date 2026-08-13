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

class IPairingService : public QObject
{
  Q_OBJECT

public:
  explicit IPairingService(QObject *parent = nullptr) : QObject(parent)
  {
  }
  ~IPairingService() override = default;

  virtual PairingOperationResult startPairing(
      DeviceSnapshot peer, QByteArray peerFingerprintSha256, PairingEndpoint endpoint
  ) = 0;
  virtual PairingOperationResult confirmMatchingSas(const QUuid &sessionId) = 0;
  virtual PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits) = 0;
  virtual PairingOperationResult cancel(const QUuid &sessionId) = 0;
  virtual PairingOperationResult revoke(const DeviceId &deviceId) = 0;
  [[nodiscard]] virtual std::optional<PairingSnapshot> snapshot() const = 0;

Q_SIGNALS:
  void pairingChanged(PairingSnapshot snapshot);
  void operationFailed(PairingOperationResult result);
};

class PairingService final : public IPairingService
{
  Q_OBJECT

public:
  PairingService(
      DeviceInfo localDevice, TrustedDeviceStore &trustedDevices, PairingOptions options = {},
      PairingManager::Clock clock = {}, PairingManager::SasGenerator sasGenerator = {},
      PairingManager::DatagramSender datagramSender = {}, QObject *parent = nullptr
  );

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
  ) override;
  PairingOperationResult confirmMatchingSas(const QUuid &sessionId) override;
  PairingOperationResult submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits) override;
  PairingOperationResult cancel(const QUuid &sessionId) override;
  PairingOperationResult revoke(const DeviceId &deviceId) override;
  [[nodiscard]] std::optional<PairingSnapshot> snapshot() const override;

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
