/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/filetransport/FileTlsTransport.h"
#include "relaydesk/transfer/CapabilityCodec.h"
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/transfer/ManifestBuilder.h"
#include "relaydesk/transfer/TransferSender.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>

#include <memory>
#include <optional>

class QThreadPool;

namespace deskflow::relaydesk {

class DeviceDiscoveryRuntime;
class AutoReconnectRuntime;
class IncomingTransferRuntime;
class IPlatformFileSafety;
class TrustedDeviceStore;

struct FileTransferRuntimeOptions
{
  QHostAddress listenAddress = QHostAddress::Any;
  quint16 listenPort = 0;
  FileTlsSettings tlsSettings;
  ::relaydesk::transfer::CapabilitiesMessage localCapabilities{
      .features = {QStringLiteral("file.v1"), QStringLiteral("sha256")},
      .conflictPolicies = {::relaydesk::transfer::ConflictPolicy::AutoRename},
  };
};

enum class FileTransferRuntimeError
{
  WrongThread,
  ListenerFailed,
  DiscoveryPublishFailed,
  PeerUnavailable,
  TransportFailed,
  ProtocolFailed,
  CapabilityFailed,
};

// Owns the independent RDFT listener and the application-level dependencies
// that later transfer slices compose. No Deskflow input socket is shared.
class FileTransferRuntime final : public IFileTransferService
{
  Q_OBJECT

public:
  FileTransferRuntime(
      DeviceId localDeviceId, const TrustedDeviceStore &trustedDevices, DeviceDiscoveryRuntime &discoveryRuntime,
      QString combinedPemPath, FileTransferRuntimeOptions options = {}, QObject *parent = nullptr
  );
  ~FileTransferRuntime() override;

  Q_DISABLE_COPY_MOVE(FileTransferRuntime)

  [[nodiscard]] bool start(QString *diagnostic = nullptr);
  void stop();
  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] quint16 listeningPort() const;
  [[nodiscard]] bool connectPeer(const DeviceId &peerDeviceId, QString *diagnostic = nullptr);
  [[nodiscard]] bool isPeerReady(const DeviceId &peerDeviceId) const;
  [[nodiscard]] std::optional<::relaydesk::transfer::NegotiatedCapabilities>
  negotiatedCapabilities(const DeviceId &peerDeviceId) const;

  [[nodiscard]] ::relaydesk::transfer::TransferStartResult send(
      const DeviceId &target, const QList<QUrl> &localItems,
      const ::relaydesk::transfer::SendOptions &options
  ) override;
  void accept(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::ReceiveOptions &options
  ) override;
  void reject(
      const ::relaydesk::transfer::TransferId &transferId, ::relaydesk::transfer::RejectReason reason
  ) override;
  void pause(const ::relaydesk::transfer::TransferId &transferId) override;
  void resume(const ::relaydesk::transfer::TransferId &transferId) override;
  void cancel(
      const ::relaydesk::transfer::TransferId &transferId,
      const ::relaydesk::transfer::TransferCancelOptions &options
  ) override;
  void retry(const ::relaydesk::transfer::TransferId &transferId) override;
  [[nodiscard]] QList<::relaydesk::transfer::TransferSnapshot> activeTransfers() const override;

Q_SIGNALS:
  void started(quint16 port);
  void stopped();
  void peerReady(
      deskflow::relaydesk::DeviceId peerDeviceId,
      ::relaydesk::transfer::NegotiatedCapabilities capabilities
  );
  void peerDisconnected(deskflow::relaydesk::DeviceId peerDeviceId);
  void errorOccurred(
      deskflow::relaydesk::FileTransferRuntimeError error, deskflow::relaydesk::FileTlsError transportError,
      QString diagnostic
  );

private:
  friend class AutoReconnectRuntime;
  struct ConnectionContext
  {
    std::optional<DeviceId> expectedPeer;
    std::optional<DeviceId> peer;
    std::optional<::relaydesk::transfer::NegotiatedCapabilities> negotiated;
  };
  struct OutgoingSession;

  [[nodiscard]] bool onOwningThread(QString *diagnostic);
  [[nodiscard]] bool connectPeerAt(
      const DeviceId &peerDeviceId, const QHostAddress &address, quint16 filePort,
      QString *diagnostic = nullptr
  );
  void attachConnection(FileTlsConnection &connection, std::optional<DeviceId> expectedPeer = std::nullopt);
  void handleAuthenticated(FileTlsConnection &connection);
  void handleFrame(FileTlsConnection &connection, ::relaydesk::transfer::Frame frame);
  void routeTransferFrame(const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame);
  void removeConnection(FileTlsConnection &connection);
  void failConnection(
      FileTlsConnection &connection, FileTransferRuntimeError error, FileTlsError transportError,
      QString diagnostic
  );
  [[nodiscard]] bool publishFileEndpoint(QString *diagnostic = nullptr);
  [[nodiscard]] bool sendPeerFrame(
      const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame,
      QString *diagnostic = nullptr
  );
  void prepareOutgoing(const ::relaydesk::transfer::TransferId &transferId);
  void finishManifestPreparation(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::TransferManifestBuildResult result
  );
  void offerPreparedTransfers(const DeviceId &peerDeviceId);
  void sendOffer(OutgoingSession &session);
  void sendResumeQuery(OutgoingSession &session);
  void handleOfferResponse(const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame);
  void handleResumeResponse(const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame);
  void sendNextManifestPage(const ::relaydesk::transfer::TransferId &transferId);
  void startNextOutgoingFile(OutgoingSession &session);
  void schedulePeerSenders(const DeviceId &peerDeviceId);
  void scheduleSenderPump(const ::relaydesk::transfer::TransferId &transferId);
  void dispatchSenderPumpResult(
      const ::relaydesk::transfer::TransferId &transferId,
      quint64 generation, const ::relaydesk::transfer::SenderPumpResult &result
  );
  void handleFileResult(const DeviceId &peerDeviceId, const ::relaydesk::transfer::Frame &frame);
  void updateOutgoingProgress(OutgoingSession &session);
  void completeOutgoing(OutgoingSession &session);
  void markOutgoingConnectionLost(OutgoingSession &session);
  void failOutgoing(
      OutgoingSession &session, ::relaydesk::transfer::TransferErrorCode errorCode, QString diagnostic = {}
  );
  void publishOperation(
      const ::relaydesk::transfer::TransferId &transferId,
      ::relaydesk::transfer::TransferOperation operation,
      ::relaydesk::transfer::TransferOperationOutcome outcome,
      ::relaydesk::transfer::TransferOperationError error = ::relaydesk::transfer::TransferOperationError::None,
      QString diagnostic = {}
  );
  [[nodiscard]] OutgoingSession *outgoing(const ::relaydesk::transfer::TransferId &transferId) const;

  DeviceId m_localDeviceId;
  const TrustedDeviceStore &m_trustedDevices;
  DeviceDiscoveryRuntime &m_discoveryRuntime;
  QString m_combinedPemPath;
  FileTransferRuntimeOptions m_options;
  std::unique_ptr<FileTlsListener> m_listener;
  QHash<FileTlsConnection *, ConnectionContext> m_connections;
  QHash<DeviceId, FileTlsConnection *> m_peerConnections;
  QHash<DeviceId, FileTlsClient *> m_clients;
  QHash<::relaydesk::transfer::TransferId, OutgoingSession *> m_outgoing;
  std::unique_ptr<QThreadPool> m_workerPool;
  std::unique_ptr<IPlatformFileSafety> m_fileSafety;
  std::unique_ptr<IncomingTransferRuntime> m_incoming;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::FileTransferRuntimeError)
