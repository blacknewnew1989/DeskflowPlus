/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/discovery/AddressCandidateProvider.h"
#include "relaydesk/trust/TlsPeerPinningPolicy.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <functional>
#include <chrono>
#include <optional>

namespace deskflow::relaydesk {

inline constexpr qsizetype kDefaultMaxReconnectCandidates = 16;
inline constexpr qsizetype kDefaultMaxRememberedAddresses = 8;

enum class AutoReconnectConnectError
{
  None,
  NetworkError,
  AuthenticationFailed,
};

struct AuthenticatedReconnectPeer
{
  DeviceId deviceId;
  QByteArray certificateFingerprintSha256;

  [[nodiscard]] bool operator==(const AuthenticatedReconnectPeer &) const = default;
};

struct AutoReconnectConnectResult
{
  AutoReconnectConnectError error = AutoReconnectConnectError::None;
  // Populated only after the connector has completed TLS and derived the
  // identity from the peer certificate/HELLO exchange. Discovery data and
  // callers are never an authentication source.
  std::optional<AuthenticatedReconnectPeer> authenticatedPeer;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == AutoReconnectConnectError::None && authenticatedPeer.has_value();
  }
};

struct AutoReconnectRequest
{
  DeviceId deviceId;
  QList<QHostAddress> discoveredAddresses;
  DiscoverySettings settings;
  quint16 inputPort = kDefaultManualInputPort;
  quint16 filePort = kDefaultManualFilePort;
};

using ReconnectDelay = std::chrono::milliseconds;

struct AutoReconnectOptions
{
  qsizetype maxCandidatesPerRound = kDefaultMaxReconnectCandidates;
  qsizetype maxRememberedAddresses = kDefaultMaxRememberedAddresses;
  ReconnectDelay initialRetryDelay{1000};
  ReconnectDelay maxRetryDelay{10000};
};

class AutoReconnectCoordinator final : public QObject
{
  Q_OBJECT

public:
  using ConnectCallback = std::function<void(AutoReconnectConnectResult)>;
  using Connector = std::function<void(const DeviceId &, const AddressCandidate &, ConnectCallback)>;
  using Scheduler = std::function<void(ReconnectDelay, std::function<void()>)>;

  AutoReconnectCoordinator(
      TrustedDeviceStore &trustedDevices, AddressCandidateProvider &candidateProvider, Connector connector,
      Scheduler scheduler = {}, AutoReconnectOptions options = {}, QObject *parent = nullptr
  );

  void start(AutoReconnectRequest request);
  void networkAvailable();
  void stop();

Q_SIGNALS:
  void connecting(DeviceId deviceId, AddressCandidate candidate);
  void connected(DeviceId deviceId, AddressCandidate candidate);
  void retryScheduled(DeviceId deviceId, ReconnectDelay delay);
  void trustBlocked(DeviceId deviceId, PeerPinningError error, QString diagnostic);
  void roundFailed(DeviceId deviceId, QString diagnostic);
  void persistenceFailed(DeviceId deviceId, QString diagnostic);

private:
  void beginRound();
  [[nodiscard]] PeerPinningResult verifyTrustedDevice() const;
  [[nodiscard]] PeerPinningResult verifyAuthenticatedPeer(const AuthenticatedReconnectPeer &peer) const;
  void candidatesReady(AddressCandidateResult result);
  void tryNextCandidate(quint64 generation);
  void candidateFinished(quint64 generation, AddressCandidate candidate, AutoReconnectConnectResult result);
  void scheduleRetry(quint64 generation, QString diagnostic);
  void rememberSuccessfulAddress(const AddressCandidate &candidate);

  TrustedDeviceStore &m_trustedDevices;
  AddressCandidateProvider &m_candidateProvider;
  Connector m_connector;
  Scheduler m_scheduler;
  AutoReconnectOptions m_options;
  std::optional<AutoReconnectRequest> m_request;
  QList<AddressCandidate> m_candidates;
  qsizetype m_nextCandidate = 0;
  quint64 m_generation = 0;
  int m_retryAttempt = 0;
  bool m_hasRequest = false;
  bool m_connected = false;
  bool m_waitingForCandidates = false;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::AutoReconnectConnectError)
Q_DECLARE_METATYPE(deskflow::relaydesk::AuthenticatedReconnectPeer)
Q_DECLARE_METATYPE(deskflow::relaydesk::AutoReconnectConnectResult)
Q_DECLARE_METATYPE(deskflow::relaydesk::PeerPinningError)
Q_DECLARE_METATYPE(deskflow::relaydesk::ReconnectDelay)
