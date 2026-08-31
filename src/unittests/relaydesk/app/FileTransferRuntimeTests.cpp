/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/IncomingTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/transfer/ControlMessageCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/transfer/ResumeStore.h"
#include "relaydesk/transfer/TransferRecoveryStore.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "../TestTlsIdentity.h"

#include <QSignalSpy>
#include <QCryptographicHash>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>

#include <limits>

using namespace deskflow::relaydesk;

namespace deskflow::relaydesk {
class FileTransferRuntimeTestAccess final
{
public:
  static void removePeerChannel(FileTransferRuntime &runtime, const DeviceId &peerDeviceId)
  {
    runtime.m_peerConnections.remove(peerDeviceId);
  }

  static FileTlsConnection *takePeerChannel(
      FileTransferRuntime &runtime, const DeviceId &peerDeviceId
  )
  {
    return runtime.m_peerConnections.take(peerDeviceId);
  }

  static void restorePeerChannel(
      FileTransferRuntime &runtime, const DeviceId &peerDeviceId, FileTlsConnection *connection
  )
  {
    runtime.m_peerConnections.insert(peerDeviceId, connection);
  }

  static bool hasPendingIncomingConflict(
      const FileTransferRuntime &runtime, const ::relaydesk::transfer::TransferId &transferId,
      const QUuid &conflictId
  )
  {
    return runtime.m_incoming != nullptr &&
           runtime.m_incoming->hasPendingIncomingConflict(transferId, conflictId);
  }

  static void occupyWorkerPool(FileTransferRuntime &runtime, QSemaphore &started, QSemaphore &release)
  {
    for (int index = 0; index < 2; ++index) {
      runtime.m_workerPool->start([&started, &release] {
        started.release();
        release.acquire();
      });
    }
  }

  static qsizetype peerConnectionCount(const FileTransferRuntime &runtime)
  {
    return runtime.m_peerConnections.size();
  }

  static qsizetype clientCount(const FileTransferRuntime &runtime)
  {
    return runtime.m_clients.size();
  }
};
} // namespace deskflow::relaydesk

namespace {
DeviceInfo localDevice(DeviceId id, QByteArray fingerprint, QString name)
{
  return {
      .deviceId = std::move(id),
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .capabilities = {.input = true, .clipboardText = true},
      .certificateFingerprintSha256 = std::move(fingerprint),
  };
}

TrustedDevice trustedDevice(DeviceId id, QByteArray fingerprint)
{
  return {
      .deviceId = std::move(id),
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = std::move(fingerprint),
  };
}
} // namespace

class FileTransferRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void listenerLifecycleIsOwnedAndRestartable();
  void activeTransferDestructionIsSafe_data();
  void activeTransferDestructionIsSafe();
  void stoppingWhilePreparingPublishesRetryableFailure();
  void stoppingBeforeStreamingPublishesRetryableFailure_data();
  void stoppingBeforeStreamingPublishesRetryableFailure();
  void trustedPeersNegotiateIndependentFileChannel();
  void outgoingSingleFileStreamsThroughWorkerPump();
  void incomingSingleFileCommitsThroughPlatformReceiver();
  void incomingConflictPolicies_data();
  void incomingConflictPolicies();
  void interruptedIncomingFileResumesFromDurableCheckpoint_data();
  void interruptedIncomingFileResumesFromDurableCheckpoint();
  void incomingHydrationValidatesResolvedTargets_data();
  void incomingHydrationValidatesResolvedTargets();
  void stoppingSenderLeavesOutgoingAtResumableCheckpoint_data();
  void stoppingSenderLeavesOutgoingAtResumableCheckpoint();
  void outgoingRecoverySaveFailureStopsBeforeStreaming();
  void outgoingRecoveryRemovalFailureDoesNotPublishTerminalSuccess_data();
  void outgoingRecoveryRemovalFailureDoesNotPublishTerminalSuccess();
  void incomingFolderCommitsEveryFileAndPreservesEmptyDirectories();
  void runtimeSourceUsesCanonicalSenderBoundary();
  void runtimeSourceComposesPlatformReceiver();
  void runtimeEnablesComposedReceiverCapability();
  void remoteCommandsSynchronizeIncomingTransfer();
  void incomingControlsSynchronizeOutgoingTransfer();
  void incomingCancelKeepsPartialDataAndIsIdempotent();
  void incomingControlsRejectTransportFailureWithoutLocalMutation_data();
  void incomingControlsRejectTransportFailureWithoutLocalMutation();
  void unknownControlOperationsPublishTypedResults();
  void invalidIdentityFailsWithoutPublishingAListener();

private:
  void commandsSynchronizeTransfer(
      bool receiverControls, ::relaydesk::transfer::PartialDisposition partialDisposition,
      bool repeatCancel = false
  );
  void recoveryStoreFailureDoesNotPublishSuccess(bool failSave, bool cancelAfterStart);
};

void FileTransferRuntimeTests::listenerLifecycleIsOwnedAndRestartable()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  QVERIFY(!identityPath.isEmpty());

  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x11'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(localId, trust, discovery, identityPath, options);
  QSignalSpy started(&runtime, &FileTransferRuntime::started);
  QSignalSpy stopped(&runtime, &FileTransferRuntime::stopped);
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  const quint16 firstPort = runtime.listeningPort();
  QVERIFY(firstPort != 0);
  QCOMPARE(discovery.service().localDevice().filePort, firstPort);
  QVERIFY(discovery.service().localDevice().capabilities.fileV1);
  QVERIFY(discovery.service().localDevice().capabilities.folderV1);
  QVERIFY(discovery.service().localDevice().capabilities.resumeV1);
  QCOMPARE(started.count(), 1);
  QVERIFY(runtime.start(&diagnostic));
  QCOMPARE(runtime.listeningPort(), firstPort);
  QCOMPARE(started.count(), 1);

  runtime.stop();
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(discovery.service().localDevice().filePort, quint16{0});
  QVERIFY(!discovery.service().localDevice().capabilities.fileV1);
  QCOMPARE(stopped.count(), 1);
  QTcpServer releasedPortProbe;
  QVERIFY(releasedPortProbe.listen(QHostAddress::LocalHost, firstPort));
  releasedPortProbe.close();

  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  QVERIFY(runtime.listeningPort() != 0);
  QCOMPARE(discovery.service().localDevice().filePort, runtime.listeningPort());
  QVERIFY(discovery.service().localDevice().capabilities.fileV1);
  QVERIFY(discovery.service().localDevice().capabilities.folderV1);
  QVERIFY(discovery.service().localDevice().capabilities.resumeV1);
  QCOMPARE(started.count(), 2);
  QCOMPARE(errors.count(), 0);
}

void FileTransferRuntimeTests::activeTransferDestructionIsSafe_data()
{
  QTest::addColumn<bool>("explicitStop");
  QTest::addColumn<bool>("exerciseSymlinkSkip");
  QTest::newRow("explicit-stop") << true << false;
  QTest::newRow("direct-destruction") << false << false;
  QTest::newRow("explicit-stop-symlink-skip") << true << true;
  QTest::newRow("direct-symlink-skip") << false << true;
}

void FileTransferRuntimeTests::activeTransferDestructionIsSafe()
{
  using namespace ::relaydesk::transfer;
  QFETCH(bool, explicitStop);
  QFETCH(bool, exerciseSymlinkSkip);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY(identity.ok());
  const auto sourcePath = directory.filePath(QStringLiteral("destruction-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(QByteArray(4 * 1024 * 1024 + 113, '\x41')), qint64{4 * 1024 * 1024 + 113});
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("received"));
  QVERIFY(QDir().mkpath(receiveRoot));
  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Destruction sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Destruction receiver")), receiverModel
  );
  FileTransferRuntimeOptions senderOptions;
  senderOptions.listenAddress = QHostAddress::LocalHost;
  senderOptions.recoveryStateRoot = directory.filePath(QStringLiteral("sender-recovery"));
  senderOptions.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  auto receiverOptions = senderOptions;
  receiverOptions.recoveryStateRoot = directory.filePath(QStringLiteral("receiver-recovery"));
  auto sender = std::make_unique<FileTransferRuntime>(
      senderId, senderTrust, senderDiscovery, identityPath, senderOptions
  );
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, receiverOptions
  );
  connect(receiver.get(), &IFileTransferService::incomingOffer, this, [&, runtime = receiver.get()](const IncomingOffer &offer) {
    runtime->accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });
  bool active = false;
  connect(receiver.get(), &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    active = active || (snapshot.direction == TransferDirection::Receiving &&
                        snapshot.state == TransferState::Transferring && snapshot.progress.completedBytes > 0);
  });
  QString diagnostic;
  QVERIFY2(sender->start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver->start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender->send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY(started.ok());
  QTRY_VERIFY_WITH_TIMEOUT(active, 15'000);
  if (explicitStop) {
    receiver->stop();
    sender->stop();
  }
  if (exerciseSymlinkSkip) {
    ResumeStore resumeStore(QDir(receiveRoot).filePath(QStringLiteral(".incoming/resume-active")));
    const QString linkPath = resumeStore.statePath(*started.transferId);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(linkPath), 5'000);
    const QString targetPath = linkPath + QStringLiteral(".target");
    QVERIFY(QFile::rename(linkPath, targetPath));
    if (!QFile::link(targetPath, linkPath)) {
      QSKIP("the current Windows token cannot create a symbolic link");
    }
    if (!QFileInfo(linkPath).isSymLink()) {
      QFile::remove(linkPath);
      QSKIP("platform did not create a symbolic link");
    }
  }
  receiver.reset();
  sender.reset();
}

void FileTransferRuntimeTests::stoppingWhilePreparingPublishesRetryableFailure()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto sourcePath = directory.filePath(QStringLiteral("preparing.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write("RelayDesk preparing state\n"), qint64{26});
  source.close();

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  QVERIFY(trust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(senderId, trust, discovery, identityPath, options);
  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));

  auto receiver = localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver"));
  receiver.filePort = 24801;
  receiver.capabilities.fileV1 = true;
  QVERIFY(discovery.registry().observeAdvertisement(receiver, QHostAddress::LocalHost));

  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IFileTransferService::transferAdded, this, [&](const TransferSnapshot &snapshot) {
    latest = snapshot;
  });
  connect(&runtime, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    latest = snapshot;
  });

  const auto started = runtime.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QVERIFY(latest.has_value());
  QCOMPARE(latest->state, TransferState::Preparing);

  runtime.stop();
  QVERIFY(latest.has_value());
  QCOMPARE(latest->state, TransferState::Failed);
  QCOMPARE(latest->errorCode, TransferErrorCode::ConnectionLost);
  QVERIFY(latest->canRetry);
  QVERIFY(!latest->canCancel);

  QCoreApplication::processEvents();
  QCOMPARE(latest->state, TransferState::Failed);
}

void FileTransferRuntimeTests::stoppingBeforeStreamingPublishesRetryableFailure_data()
{
  QTest::addColumn<bool>("acceptOffer");
  QTest::addColumn<int>("targetState");

  QTest::newRow("waiting-for-acceptance") << false
                                          << static_cast<int>(::relaydesk::transfer::TransferState::WaitingForAcceptance);
  QTest::newRow("queued-after-acceptance") << true
                                            << static_cast<int>(::relaydesk::transfer::TransferState::Queued);
}

void FileTransferRuntimeTests::stoppingBeforeStreamingPublishesRetryableFailure()
{
  using namespace ::relaydesk::transfer;

  QFETCH(bool, acceptOffer);
  QFETCH(int, targetState);
  const auto expectedState = static_cast<TransferState>(targetState);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto sourcePath = directory.filePath(QStringLiteral("pre-stream.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(QByteArray(64, '\x42')), qint64{64});
  source.close();

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));

  FileTlsListener receiver(receiverId, &receiverTrust, identityPath);
  QString diagnostic;
  QCOMPARE(receiver.listen(QHostAddress::LocalHost, 0, &diagnostic), FileTlsError::None);

  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(senderId, senderTrust, discovery, identityPath, options);
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  auto receiverInfo = localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver"));
  receiverInfo.filePort = receiver.serverPort();
  receiverInfo.capabilities.fileV1 = true;
  QVERIFY(discovery.registry().observeAdvertisement(receiverInfo, QHostAddress::LocalHost));

  QStringList errors;
  bool offerSeen = false;
  connect(&receiver, &FileTlsListener::connectionCreated, this, [&](FileTlsConnection *connection) {
    connect(connection, &FileTlsConnection::failed, this, [&](FileTlsError, const QString &message) {
      errors.append(message);
    });
    connect(connection, &FileTlsConnection::frameReceived, this, [&, connection](const Frame &frame) {
      QString responseDiagnostic;
      if (frame.type == MessageType::Capabilities) {
        auto capabilities = options.localCapabilities;
        capabilities.features.append(QStringLiteral("file.receive.v1"));
        const Frame response{
            .type = MessageType::Capabilities,
            .metadata = CapabilityCodec::encode(capabilities, &responseDiagnostic),
        };
        if (connection->sendFrame(response, &responseDiagnostic) != FileTlsError::None)
          errors.append(responseDiagnostic);
        return;
      }
      if (frame.type != MessageType::TransferOffer)
        return;
      const auto decoded = ControlMessageCodec::decode(frame.version, frame.type, frame.metadata);
      if (!decoded.ok()) {
        errors.append(decoded.diagnostic);
        return;
      }
      const auto *offer = std::get_if<TransferOffer>(&*decoded.message);
      if (offer == nullptr) {
        errors.append(QStringLiteral("offer frame decoded to the wrong variant"));
        return;
      }
      offerSeen = true;
      if (!acceptOffer)
        return;
      const TransferAccept acceptance{
          .transferId = offer->transferId,
          .effectiveConflictPolicy = ConflictPolicy::AutoRename,
          .logicalDestination = QStringLiteral("RelayDesk"),
          .freeBytes = static_cast<quint64>(std::numeric_limits<qint64>::max()),
      };
      const Frame response{
          .type = MessageType::TransferAccept,
          .flags = Response,
          .metadata = ControlMessageCodec::encode(
              kProtocolMajorVersion, ControlMessage{acceptance}, &responseDiagnostic
          ),
      };
      if (response.metadata.isEmpty() ||
          connection->sendFrame(response, &responseDiagnostic) != FileTlsError::None) {
        errors.append(responseDiagnostic);
      }
    });
  });

  std::optional<TransferSnapshot> latest;
  connect(&runtime, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    latest = snapshot;
    if (acceptOffer && snapshot.state == expectedState)
      QTimer::singleShot(0, &runtime, [&runtime] { runtime.stop(); });
  });
  const auto started = runtime.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));

  if (!acceptOffer) {
    QTRY_VERIFY_WITH_TIMEOUT(offerSeen && latest.has_value() && latest->state == expectedState, 5'000);
    runtime.stop();
  }
  QTRY_VERIFY_WITH_TIMEOUT(latest.has_value() && latest->state == TransferState::Failed, 5'000);
  QCOMPARE(latest->errorCode, TransferErrorCode::ConnectionLost);
  QVERIFY(latest->canRetry);
  QVERIFY(!latest->canCancel);
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
}

void FileTransferRuntimeTests::trustedPeersNegotiateIndependentFileChannel()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  model::DeviceHomeModel firstModel;
  model::DeviceHomeModel secondModel;
  DeviceDiscoveryRuntime firstDiscovery(
      localDevice(firstId, identity.fingerprintSha256, QStringLiteral("First")), firstModel
  );
  DeviceDiscoveryRuntime secondDiscovery(
      localDevice(secondId, identity.fingerprintSha256, QStringLiteral("Second")), secondModel
  );
  TrustedDeviceStore firstTrust(directory.filePath(QStringLiteral("first-trust.json")));
  TrustedDeviceStore secondTrust(directory.filePath(QStringLiteral("second-trust.json")));
  QVERIFY(firstTrust.upsert(trustedDevice(secondId, identity.fingerprintSha256)));
  QVERIFY(secondTrust.upsert(trustedDevice(firstId, identity.fingerprintSha256)));

  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime first(firstId, firstTrust, firstDiscovery, identityPath, options);
  FileTransferRuntime second(secondId, secondTrust, secondDiscovery, identityPath, options);
  QString diagnostic;
  QVERIFY2(first.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(second.start(&diagnostic), qPrintable(diagnostic));
  auto directPeer = secondDiscovery.service().localDevice();
  directPeer.filePort = second.listeningPort();
  directPeer.capabilities.fileV1 = true;
  QVERIFY(firstDiscovery.registry().observeAdvertisement(directPeer, QHostAddress::LocalHost));

  bool firstReady = false;
  bool secondReady = false;
  QStringList errors;
  connect(&first, &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
    if (peer == secondId) {
      firstReady = true;
    }
  });
  connect(&second, &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
    if (peer == firstId) {
      secondReady = true;
    }
  });
  connect(&first, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  connect(&second, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });

  QVERIFY2(first.connectPeer(secondId, &diagnostic), qPrintable(diagnostic));
  QTRY_VERIFY2_WITH_TIMEOUT(firstReady && secondReady, qPrintable(errors.join(QStringLiteral("; "))), 5'000);
  QVERIFY(first.isPeerReady(secondId));
  QVERIFY(second.isPeerReady(firstId));
  const auto negotiated = first.negotiatedCapabilities(secondId);
  QVERIFY(negotiated.has_value());
  QVERIFY(negotiated->features.contains(QStringLiteral("file.v1")));
  QVERIFY(negotiated->features.contains(QStringLiteral("sha256")));
  QVERIFY(negotiated->localCanReceiveFiles);
  QVERIFY(negotiated->peerCanReceiveFiles);

  QSignalSpy disconnected(&first, &FileTransferRuntime::peerDisconnected);
  QVERIFY(first.disconnectPeer(secondId));
  QTRY_VERIFY_WITH_TIMEOUT(disconnected.count() == 1 && !first.isPeerReady(secondId), 5'000);
  QCOMPARE(*static_cast<const DeviceId *>(disconnected.first().at(0).constData()), secondId);
}

void FileTransferRuntimeTests::outgoingSingleFileStreamsThroughWorkerPump()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(2 * 1024 * 1024 + 137, '\x5a');
  const auto sourcePath = directory.filePath(QStringLiteral("payload.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));

  FileTlsListener receiver(receiverId, &receiverTrust, identityPath);
  QString diagnostic;
  QCOMPARE(receiver.listen(QHostAddress::LocalHost, 0, &diagnostic), FileTlsError::None);

  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(senderId, senderTrust, discovery, identityPath, options);
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  auto receiverInfo = localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver"));
  receiverInfo.filePort = receiver.serverPort();
  receiverInfo.capabilities.fileV1 = true;
  receiverInfo.capabilities.folderV1 = true;
  receiverInfo.capabilities.resumeV1 = true;
  QVERIFY(discovery.registry().observeAdvertisement(receiverInfo, QHostAddress::LocalHost));

  QByteArray receivedBytes;
  std::optional<TransferOffer> receivedOffer;
  std::optional<ManifestEntry> receivedEntry;
  bool manifestComplete = false;
  bool fileBeganAfterManifest = false;
  int chunkCount = 0;
  QStringList errors;
  connect(&receiver, &FileTlsListener::connectionCreated, this, [&](FileTlsConnection *connection) {
    connect(connection, &FileTlsConnection::failed, this, [&](FileTlsError, const QString &message) {
      errors.append(message);
    });
    connect(connection, &FileTlsConnection::frameReceived, this, [&, connection](const Frame &frame) {
      QString encodeDiagnostic;
      if (frame.type == MessageType::Capabilities) {
        auto receiverCapabilities = options.localCapabilities;
        receiverCapabilities.features.append(QStringLiteral("file.receive.v1"));
        Frame response{
            .type = MessageType::Capabilities,
            .metadata = CapabilityCodec::encode(receiverCapabilities, &encodeDiagnostic),
        };
        if (connection->sendFrame(response, &encodeDiagnostic) != FileTlsError::None) {
          errors.append(encodeDiagnostic);
        }
        return;
      }
      if (frame.type == MessageType::TransferOffer) {
        const auto decoded = ControlMessageCodec::decode(frame.version, frame.type, frame.metadata);
        if (!decoded.ok()) {
          errors.append(decoded.diagnostic);
          return;
        }
        const auto *offer = std::get_if<TransferOffer>(&*decoded.message);
        if (offer == nullptr) {
          errors.append(QStringLiteral("offer frame decoded to the wrong variant"));
          return;
        }
        receivedOffer = *offer;
        const TransferAccept acceptance{
            .transferId = offer->transferId,
            .effectiveConflictPolicy = ConflictPolicy::AutoRename,
            .logicalDestination = QStringLiteral("RelayDesk"),
            .freeBytes = static_cast<quint64>(std::numeric_limits<qint64>::max()),
        };
        Frame response{
            .type = MessageType::TransferAccept,
            .flags = Response,
            .metadata = ControlMessageCodec::encode(
                kProtocolMajorVersion, ControlMessage{acceptance}, &encodeDiagnostic
            ),
        };
        if (response.metadata.isEmpty() ||
            connection->sendFrame(response, &encodeDiagnostic) != FileTlsError::None) {
          errors.append(encodeDiagnostic);
        }
        return;
      }
      if (frame.type == MessageType::ManifestPage) {
        const auto decoded = ManifestPageCodec::decode(frame.version, frame.metadata);
        if (!decoded.ok() || decoded.page->entries.size() != 1) {
          errors.append(decoded.ok() ? QStringLiteral("unexpected manifest entry count") : decoded.diagnostic);
          return;
        }
        receivedEntry = decoded.page->entries.first();
        return;
      }
      if (frame.type == MessageType::ManifestComplete) {
        const auto decoded = ManifestPageCodec::decodeComplete(frame.version, frame.metadata);
        if (!decoded.ok()) {
          errors.append(decoded.diagnostic);
          return;
        }
        manifestComplete = receivedOffer.has_value() &&
                           decoded.message->transferId == receivedOffer->transferId &&
                           decoded.message->canonicalSha256 == receivedOffer->manifestSha256;
        return;
      }
      const auto decoded = FileMessageCodec::decode(frame.type, frame.metadata);
      if (!decoded.ok()) {
        errors.append(decoded.diagnostic);
        return;
      }
      if (const auto *begin = std::get_if<FileBeginMessage>(&*decoded.message)) {
        fileBeganAfterManifest = manifestComplete && receivedEntry.has_value() &&
                                 begin->fileId == receivedEntry->id && begin->size == sourceBytes.size();
        return;
      }
      if (const auto *chunk = std::get_if<FileChunkMessage>(&*decoded.message)) {
        if (chunk->offset != static_cast<quint64>(receivedBytes.size())) {
          errors.append(QStringLiteral("file chunk offset was not contiguous"));
          return;
        }
        ++chunkCount;
        receivedBytes.append(frame.payload);
        return;
      }
      if (const auto *end = std::get_if<FileEndMessage>(&*decoded.message)) {
        if (receivedBytes.size() != sourceBytes.size() ||
            end->sha256 != QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256)) {
          errors.append(QStringLiteral("file end did not match streamed payload"));
          return;
        }
        const FileResultMessage result{
            .transferId = end->transferId,
            .fileId = end->fileId,
            .code = FileResultCode::Ok,
        };
        Frame response{
            .type = MessageType::FileResult,
            .flags = Response | Final,
            .streamId = frame.streamId,
            .metadata = FileMessageCodec::encode(FileControlMessage{result}, &encodeDiagnostic),
        };
        if (response.metadata.isEmpty() ||
            connection->sendFrame(response, &encodeDiagnostic) != FileTlsError::None) {
          errors.append(encodeDiagnostic);
        }
      }
    });
  });

  std::optional<TransferSnapshot> latest;
  QStringList stateTrace;
  connect(&runtime, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    latest = snapshot;
    stateTrace.append(
        QStringLiteral("state=%1 bytes=%2 files=%3 frames=%4")
            .arg(static_cast<int>(snapshot.state))
            .arg(snapshot.progress.completedBytes)
            .arg(snapshot.progress.completedFiles)
            .arg(chunkCount)
    );
  });
  connect(&runtime, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  const auto start = runtime.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(start.ok(), qPrintable(start.diagnostic));
  QVERIFY(start.transferId.has_value());
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 10'000 &&
         (!latest.has_value() || latest->state != TransferState::Completed)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence =
      QStringLiteral("errors=[%1] trace=[%2] offer=%3 manifest=%4 begin=%5 bytes=%6 chunks=%7")
          .arg(errors.join(QStringLiteral("; ")), stateTrace.join(QStringLiteral(" | ")))
          .arg(receivedOffer.has_value())
          .arg(manifestComplete)
          .arg(fileBeganAfterManifest)
          .arg(receivedBytes.size())
          .arg(chunkCount);
  QVERIFY2(latest.has_value() && latest->state == TransferState::Completed, qPrintable(evidence));
  QCOMPARE(receivedBytes, sourceBytes);
  QVERIFY(fileBeganAfterManifest);
  QVERIFY(chunkCount >= 3);
  QCOMPARE(latest->progress.completedBytes, static_cast<quint64>(sourceBytes.size()));
  QCOMPARE(latest->progress.completedFiles, quint64{1});
  QVERIFY(runtime.activeTransfers().isEmpty());
}

void FileTransferRuntimeTests::incomingSingleFileCommitsThroughPlatformReceiver()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(1024 * 1024 + 73, '\x4c');
  const auto sourcePath = directory.filePath(QStringLiteral("vertical-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("vertical-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("vertical-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  auto receiverTrustedSender = trustedDevice(senderId, identity.fingerprintSha256);
  receiverTrustedSender.autoAcceptFiles = true;
  QVERIFY(receiverTrust.upsert(receiverTrustedSender));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  QStringList errors;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  std::optional<IncomingOffer> incoming;
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    incoming = offer;
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });
  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending) {
      senderLatest = snapshot;
    }
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  auto receiverInfo = receiverDiscovery.service().localDevice();
  QVERIFY(receiverInfo.filePort != 0);
  QVERIFY(receiverInfo.capabilities.fileV1);
  QVERIFY(senderDiscovery.registry().observeAdvertisement(receiverInfo, QHostAddress::LocalHost));

  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QVERIFY(started.transferId.has_value());
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 15'000 &&
         (!senderLatest.has_value() || senderLatest->state != TransferState::Completed ||
          !receiverLatest.has_value() || receiverLatest->state != TransferState::Completed)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence =
      QStringLiteral("errors=[%1] offer=%2 senderState=%3 receiverState=%4 senderBytes=%5 receiverBytes=%6")
          .arg(errors.join(QStringLiteral("; ")))
          .arg(incoming.has_value())
          .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
          .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1)
          .arg(senderLatest.has_value() ? senderLatest->progress.completedBytes : 0)
          .arg(receiverLatest.has_value() ? receiverLatest->progress.completedBytes : 0);
  QVERIFY2(senderLatest.has_value() && senderLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY2(
      receiverLatest.has_value() && receiverLatest->state == TransferState::Completed,
      qPrintable(evidence)
  );
  QVERIFY(incoming.has_value());
  QCOMPARE(incoming->offer.transferId, *started.transferId);
  QVERIFY(incoming->mayAutoAccept);
  QFile committed(QDir(receiveRoot).filePath(QStringLiteral("vertical-source.bin")));
  QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
  QCOMPARE(committed.readAll(), sourceBytes);
  QVERIFY(!QFileInfo::exists(committed.fileName() + QStringLiteral(".part")));
  QVERIFY(sender.activeTransfers().isEmpty());
  QVERIFY(receiver.activeTransfers().isEmpty());
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
}

void FileTransferRuntimeTests::incomingConflictPolicies_data()
{
  using ::relaydesk::transfer::ConflictPolicy;
  using ::relaydesk::transfer::IncomingConflictDecision;

  QTest::addColumn<ConflictPolicy>("policy");
  QTest::addColumn<IncomingConflictDecision>("askDecision");
  QTest::addColumn<QString>("committedName");
  QTest::addColumn<bool>("originalReplaced");
  QTest::addColumn<bool>("expectCancellation");
  QTest::addColumn<bool>("breakCancelTransport");
  QTest::addColumn<bool>("pauseBeforeDecision");
  QTest::newRow("auto-rename")
      << ConflictPolicy::AutoRename << IncomingConflictDecision::AutoRename
      << QStringLiteral("conflict (1).bin") << false << false << false << false;
  QTest::newRow("overwrite") << ConflictPolicy::Overwrite << IncomingConflictDecision::Overwrite
                              << QStringLiteral("conflict.bin") << true << false << false << false;
  QTest::newRow("skip") << ConflictPolicy::Skip << IncomingConflictDecision::Skip << QString{}
                         << false << false << false << false;
  QTest::newRow("ask-overwrite") << ConflictPolicy::Ask << IncomingConflictDecision::Overwrite
                                  << QStringLiteral("conflict.bin") << true << false << false << false;
  QTest::newRow("ask-auto-rename")
      << ConflictPolicy::Ask << IncomingConflictDecision::AutoRename
      << QStringLiteral("conflict (1).bin") << false << false << false << false;
  QTest::newRow("ask-skip") << ConflictPolicy::Ask << IncomingConflictDecision::Skip << QString{}
                             << false << false << false << false;
  QTest::newRow("ask-auto-rename-paused")
      << ConflictPolicy::Ask << IncomingConflictDecision::AutoRename
      << QStringLiteral("conflict (1).bin") << false << false << false << true;
  QTest::newRow("ask-cancel") << ConflictPolicy::Ask << IncomingConflictDecision::CancelTransfer
                               << QString{} << false << true << false << false;
  QTest::newRow("ask-cancel-transport-failure")
      << ConflictPolicy::Ask << IncomingConflictDecision::CancelTransfer << QString{} << false
      << true << true << false;
}

void FileTransferRuntimeTests::incomingConflictPolicies()
{
  using namespace ::relaydesk::transfer;

  QFETCH(ConflictPolicy, policy);
  QFETCH(IncomingConflictDecision, askDecision);
  QFETCH(QString, committedName);
  QFETCH(bool, originalReplaced);
  QFETCH(bool, expectCancellation);
  QFETCH(bool, breakCancelTransport);
  QFETCH(bool, pauseBeforeDecision);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(256 * 1024 + 31, '\x73');
  const QByteArray originalBytes = QByteArrayLiteral("preserve-original");
  const auto sourcePath = directory.filePath(QStringLiteral("conflict.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("conflict-received"));
  QVERIFY(QDir().mkpath(receiveRoot));
  QFile original(QDir(receiveRoot).filePath(QStringLiteral("conflict.bin")));
  QVERIFY(original.open(QIODevice::WriteOnly));
  QCOMPARE(original.write(originalBytes), qint64(originalBytes.size()));
  original.close();

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("conflict-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("conflict-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Conflict sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Conflict receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  QSignalSpy receiverOperations(&receiver, &IFileTransferService::transferOperationFinished);
  QVERIFY(receiverOperations.isValid());
  QStringList errors;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    receiver.accept(
        offer.offer.transferId, {.destinationRoot = receiveRoot, .conflictPolicy = policy}
    );
  });
  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  QSignalSpy prompts(&receiver, &IFileTransferService::incomingConflictDecisionRequired);
  std::optional<IncomingConflictPrompt> conflictPrompt;
  bool promptPathIsPrivateAndRelative = true;
  bool wrongConflictIdsIgnored = true;
  bool pausedDecisionDeferredDiskWork = !pauseBeforeDecision;
  FileTlsConnection *detachedPeerChannel = nullptr;
  connect(&receiver, &IFileTransferService::incomingConflictDecisionRequired, this,
          [&](const IncomingConflictPrompt &prompt) {
            conflictPrompt = prompt;
            promptPathIsPrivateAndRelative =
                prompt.relativeProtocolPath == QStringLiteral("conflict.bin") &&
                !prompt.relativeProtocolPath.contains(receiveRoot, Qt::CaseInsensitive) &&
                !QDir::isAbsolutePath(prompt.relativeProtocolPath);
            receiver.resolveIncomingConflict(
                TransferId::generate(), prompt.conflictId, askDecision
            );
            receiver.resolveIncomingConflict(
                prompt.transferId, QUuid::createUuid(), askDecision
            );
            wrongConflictIdsIgnored = FileTransferRuntimeTestAccess::hasPendingIncomingConflict(
                receiver, prompt.transferId, prompt.conflictId
            );
            if (pauseBeforeDecision) {
              receiver.pause(prompt.transferId);
            }
            if (breakCancelTransport) {
              detachedPeerChannel =
                  FileTransferRuntimeTestAccess::takePeerChannel(receiver, senderId);
            }
            receiver.resolveIncomingConflict(prompt.transferId, prompt.conflictId, askDecision);
            if (!breakCancelTransport) {
              receiver.resolveIncomingConflict(prompt.transferId, prompt.conflictId, askDecision);
            }
            if (pauseBeforeDecision) {
              QTimer::singleShot(200, this, [&, transferId = prompt.transferId] {
                QDirIterator partFiles(
                    receiveRoot, QStringList{QStringLiteral("*.part")}, QDir::Files,
                    QDirIterator::Subdirectories
                );
                QFile preserved(QDir(receiveRoot).filePath(QStringLiteral("conflict.bin")));
                pausedDecisionDeferredDiskWork =
                    receiverLatest.has_value() && receiverLatest->state == TransferState::Paused &&
                    !partFiles.hasNext() && preserved.open(QIODevice::ReadOnly) &&
                    preserved.readAll() == originalBytes;
                receiver.resume(transferId);
              });
            }
          });
  connect(&sender, &IFileTransferService::transferAdded, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending) {
      senderLatest = snapshot;
    }
  });
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending) {
      senderLatest = snapshot;
    }
  });
  connect(&receiver, &IFileTransferService::transferAdded, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
    }
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender.send(
      receiverId, {QUrl::fromLocalFile(sourcePath)}, {.conflictPolicy = policy}
  );
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));

  if (breakCancelTransport) {
    QTRY_VERIFY2_WITH_TIMEOUT(
        conflictPrompt.has_value(), qPrintable(errors.join(QStringLiteral("; "))), 15'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(receiverOperations.count() >= 2, 5'000);
    const auto *cancelResult = static_cast<const TransferOperationResult *>(
        receiverOperations.last().constFirst().constData()
    );
    QVERIFY(cancelResult != nullptr);
    QCOMPARE(cancelResult->operation, TransferOperation::Cancel);
    QCOMPARE(cancelResult->outcome, TransferOperationOutcome::Rejected);
    QCOMPARE(cancelResult->error, TransferOperationError::TransportFailed);
    QVERIFY(wrongConflictIdsIgnored);
    QVERIFY(promptPathIsPrivateAndRelative);
    QVERIFY(FileTransferRuntimeTestAccess::hasPendingIncomingConflict(
        receiver, conflictPrompt->transferId, conflictPrompt->conflictId
    ));
    QVERIFY(receiverLatest.has_value());
    QCOMPARE(receiverLatest->state, TransferState::Queued);
    QDirIterator partFiles(
        receiveRoot, QStringList{QStringLiteral("*.part")}, QDir::Files,
        QDirIterator::Subdirectories
    );
    QVERIFY(!partFiles.hasNext());
    QFile preservedBeforeRecovery(QDir(receiveRoot).filePath(QStringLiteral("conflict.bin")));
    QVERIFY(preservedBeforeRecovery.open(QIODevice::ReadOnly));
    QCOMPARE(preservedBeforeRecovery.readAll(), originalBytes);
    QVERIFY(detachedPeerChannel != nullptr);
    FileTransferRuntimeTestAccess::restorePeerChannel(
        receiver, senderId, detachedPeerChannel
    );
    receiver.resolveIncomingConflict(
        conflictPrompt->transferId, conflictPrompt->conflictId,
        IncomingConflictDecision::AutoRename
    );
    QTRY_VERIFY_WITH_TIMEOUT(
        senderLatest.has_value() && senderLatest->state == TransferState::Completed, 15'000
    );
    QTRY_VERIFY_WITH_TIMEOUT(
        receiverLatest.has_value() && receiverLatest->state == TransferState::Completed, 15'000
    );
    QVERIFY(!FileTransferRuntimeTestAccess::hasPendingIncomingConflict(
        receiver, conflictPrompt->transferId, conflictPrompt->conflictId
    ));
    QFile preserved(QDir(receiveRoot).filePath(QStringLiteral("conflict.bin")));
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), originalBytes);
    QFile committed(QDir(receiveRoot).filePath(QStringLiteral("conflict (1).bin")));
    QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
    QCOMPARE(committed.readAll(), sourceBytes);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
    QCOMPARE(prompts.count(), 1);
    return;
  }

  QElapsedTimer wait;
  wait.start();
  const auto expectedState = expectCancellation ? TransferState::Cancelled : TransferState::Completed;
  while (wait.elapsed() < 15'000 &&
         (!senderLatest.has_value() || senderLatest->state != expectedState ||
          !receiverLatest.has_value() || receiverLatest->state != expectedState)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence = QStringLiteral("policy=%1 errors=[%2] sender=%3 receiver=%4")
                            .arg(static_cast<int>(policy))
                            .arg(errors.join(QStringLiteral("; ")))
                            .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
                            .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1);
  QVERIFY2(senderLatest.has_value() && senderLatest->state == expectedState, qPrintable(evidence));
  QVERIFY2(receiverLatest.has_value() && receiverLatest->state == expectedState, qPrintable(evidence));
  QVERIFY(promptPathIsPrivateAndRelative);
  QVERIFY(wrongConflictIdsIgnored);
  QVERIFY(pausedDecisionDeferredDiskWork);
  QFile preserved(QDir(receiveRoot).filePath(QStringLiteral("conflict.bin")));
  QVERIFY(preserved.open(QIODevice::ReadOnly));
  QCOMPARE(preserved.readAll(), originalReplaced ? sourceBytes : originalBytes);
  if (!committedName.isEmpty() && committedName != QStringLiteral("conflict.bin")) {
    QFile committed(QDir(receiveRoot).filePath(committedName));
    QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
    QCOMPARE(committed.readAll(), sourceBytes);
  }
  if (policy == ConflictPolicy::Skip) {
    QVERIFY(!QFileInfo::exists(QDir(receiveRoot).filePath(QStringLiteral("conflict (1).bin"))));
  }
  if (expectCancellation) {
    std::optional<TransferOperationResult> cancelResult;
    for (qsizetype index = 0; index < receiverOperations.count(); ++index) {
      const auto *result = static_cast<const TransferOperationResult *>(
          receiverOperations.at(index).constFirst().constData()
      );
      if (result != nullptr && result->operation == TransferOperation::Cancel) {
        cancelResult = *result;
      }
    }
    QVERIFY(cancelResult.has_value());
    QCOMPARE(cancelResult->outcome, TransferOperationOutcome::Applied);
  } else {
    QCOMPARE(receiverLatest->progress.completedFiles, quint64{1});
    QCOMPARE(receiverLatest->progress.completedBytes, static_cast<quint64>(sourceBytes.size()));
  }
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
  QCOMPARE(prompts.count(), policy == ConflictPolicy::Ask ? 1 : 0);
}

void FileTransferRuntimeTests::interruptedIncomingFileResumesFromDurableCheckpoint_data()
{
  QTest::addColumn<bool>("reconstructReceiver");
  QTest::addColumn<QString>("recoveryMutation");
  QTest::addColumn<QString>("expectedDiagnostic");
  QTest::newRow("listener-restart") << false << QString{} << QString{};
  QTest::newRow("runtime-reconstruction") << true << QString{} << QString{};
  QTest::newRow("trust-revoked")
      << true << QStringLiteral("trust-revoked") << QStringLiteral("trust validation");
  QTest::newRow("fingerprint-changed")
      << true << QStringLiteral("fingerprint") << QStringLiteral("trust validation");
  QTest::newRow("descriptor-corrupt")
      << true << QStringLiteral("descriptor") << QStringLiteral("was skipped");
  QTest::newRow("part-length-mismatch")
      << true << QStringLiteral("part-length") << QStringLiteral("not hydrated");
}

void FileTransferRuntimeTests::interruptedIncomingFileResumesFromDurableCheckpoint()
{
  using namespace ::relaydesk::transfer;
  QFETCH(bool, reconstructReceiver);
  QFETCH(QString, recoveryMutation);
  QFETCH(QString, expectedDiagnostic);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(20 * 1024 * 1024 + 113, '\x6b');
  const auto sourcePath = directory.filePath(QStringLiteral("resume-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("resume-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("resume-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("resume-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Resume sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Resume receiver")), receiverModel
  );
  FileTransferRuntimeOptions senderOptions;
  senderOptions.listenAddress = QHostAddress::LocalHost;
  senderOptions.recoveryStateRoot = directory.filePath(QStringLiteral("sender-recovery"));
  senderOptions.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  auto receiverOptions = senderOptions;
  receiverOptions.recoveryStateRoot = directory.filePath(QStringLiteral("receiver-recovery"));
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, senderOptions);
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, receiverOptions
  );
  QStringList errors;
  bool receiverStoppedAtCheckpoint = false;
  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  quint64 interruptedBytes = 0;
  QStringList senderStates;
  QObject connectionContext;
  connect(&sender, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto error, auto, const QString &message) {
    if (!receiverStoppedAtCheckpoint || error != FileTransferRuntimeError::TransportFailed) {
      errors.append(QStringLiteral("sender: ") + message);
    }
  });
  connect(&sender, &IFileTransferService::transferChanged, &connectionContext, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending) {
      return;
    }
    senderLatest = snapshot;
    senderStates.append(QString::number(static_cast<int>(snapshot.state)));
  });
  const auto attachReceiver = [&](FileTransferRuntime &runtime) {
    auto *runtimePointer = &runtime;
    connect(runtimePointer, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto, auto, const QString &message) {
      errors.append(QStringLiteral("receiver: ") + message);
    });
    connect(runtimePointer, &IFileTransferService::incomingOffer, &connectionContext, [&, runtimePointer](const IncomingOffer &offer) {
      runtimePointer->accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
    });
    connect(runtimePointer, &IFileTransferService::transferAdded, &connectionContext, [&](const TransferSnapshot &snapshot) {
      if (snapshot.direction == TransferDirection::Receiving) {
        receiverLatest = snapshot;
      }
    });
    connect(runtimePointer, &IFileTransferService::transferChanged, &connectionContext, [&, runtimePointer](const TransferSnapshot &snapshot) {
      if (snapshot.direction != TransferDirection::Receiving) {
        return;
      }
      receiverLatest = snapshot;
      if (!receiverStoppedAtCheckpoint && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= 1024U * 1024U &&
          snapshot.progress.completedBytes < snapshot.progress.totalBytes) {
        receiverStoppedAtCheckpoint = true;
        interruptedBytes = snapshot.progress.completedBytes;
        runtimePointer->stop();
      }
    });
  };
  attachReceiver(*receiver);

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver->start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(receiverStoppedAtCheckpoint, 15'000);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderLatest.has_value() && senderLatest->state == TransferState::Interrupted &&
          receiverLatest.has_value() && receiverLatest->state == TransferState::Interrupted,
      10'000
  );
  QVERIFY(interruptedBytes >= 1024U * 1024U);
  const QString partPath = QDir(receiveRoot).filePath(
      QStringLiteral(".incoming/%1").arg(started.transferId->toString())
  );
  QVERIFY(QDir(partPath).exists());

  TransferRecoveryStore recoveryStore(receiverOptions.recoveryStateRoot);
  const auto persistedIncoming = recoveryStore.loadIncoming(*started.transferId);
  QVERIFY2(persistedIncoming.ok(), qPrintable(persistedIncoming.diagnostic));
  QCOMPARE(persistedIncoming.state->transferId, *started.transferId);
  QCOMPARE(persistedIncoming.state->localDeviceId, receiverId);
  QCOMPARE(persistedIncoming.state->peerDeviceId, senderId);

  if (reconstructReceiver) {
    receiver.reset();
    receiverLatest.reset();
    errors.clear();
    if (recoveryMutation == QStringLiteral("trust-revoked")) {
      QVERIFY(receiverTrust.revoke(senderId));
    } else if (recoveryMutation == QStringLiteral("fingerprint")) {
      auto changedTrust = receiverTrust.find(senderId);
      QVERIFY(changedTrust.has_value());
      changedTrust->fingerprintSha256 = QByteArray(32, '\x72');
      QVERIFY(receiverTrust.upsert(*changedTrust));
    } else if (recoveryMutation == QStringLiteral("descriptor")) {
      QFile descriptor(recoveryStore.incomingStatePath(*started.transferId));
      QVERIFY(descriptor.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QCOMPARE(descriptor.write("corrupt"), qint64{7});
    } else if (recoveryMutation == QStringLiteral("part-length")) {
      QDirIterator parts(
          partPath, QStringList{QStringLiteral("*.part")}, QDir::Files, QDirIterator::Subdirectories
      );
      QVERIFY(parts.hasNext());
      QFile part(parts.next());
      QVERIFY(part.open(QIODevice::Append));
      QCOMPARE(part.write("x"), qint64{1});
    }
    receiver = std::make_unique<FileTransferRuntime>(
        receiverId, receiverTrust, receiverDiscovery, identityPath, receiverOptions
    );
    attachReceiver(*receiver);
    QVERIFY2(receiver->start(&diagnostic), qPrintable(diagnostic));
    const auto hasHydratedTransfer = [&] {
      for (const auto &snapshot : receiver->activeTransfers()) {
        if (snapshot.id == *started.transferId && snapshot.state == TransferState::Interrupted) {
          return true;
        }
      }
      return false;
    };
    if (!expectedDiagnostic.isEmpty()) {
      QTRY_VERIFY_WITH_TIMEOUT(errors.join(QLatin1Char(';')).contains(expectedDiagnostic), 10'000);
      QVERIFY(receiver->activeTransfers().isEmpty());
      return;
    }
    QTRY_VERIFY_WITH_TIMEOUT(hasHydratedTransfer(), 10'000);
  } else {
    QVERIFY2(receiver->start(&diagnostic), qPrintable(diagnostic));
  }
  const auto restartedInfo = receiverDiscovery.service().localDevice();
  QVERIFY(restartedInfo.capabilities.resumeV1);
  QVERIFY(senderDiscovery.registry().observeAdvertisement(restartedInfo, QHostAddress::LocalHost));
  QVERIFY2(sender.connectPeer(receiverId, &diagnostic), qPrintable(diagnostic));

  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 30'000 &&
         (!senderLatest.has_value() || senderLatest->state != TransferState::Completed ||
          !receiverLatest.has_value() || receiverLatest->state != TransferState::Completed)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence =
      QStringLiteral("errors=[%1] states=[%2] stopped=%3 durable=%4 sender=%5 receiver=%6")
          .arg(errors.join(QStringLiteral("; ")), senderStates.join(QLatin1Char(',')))
          .arg(receiverStoppedAtCheckpoint)
          .arg(interruptedBytes)
          .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
          .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1);
  QVERIFY2(senderLatest.has_value() && senderLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY2(receiverLatest.has_value() && receiverLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY(senderStates.contains(QString::number(static_cast<int>(TransferState::Interrupted))));
  QVERIFY(senderStates.contains(QString::number(static_cast<int>(TransferState::Resuming))));
  QFile committed(QDir(receiveRoot).filePath(QStringLiteral("resume-source.bin")));
  QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
  QCOMPARE(committed.readAll(), sourceBytes);
  QVERIFY(QDir(partPath).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
  const QString resumeStatePath = QDir(receiveRoot).filePath(
      QStringLiteral(".incoming/resume-active/%1.resume.cbor").arg(started.transferId->toString())
  );
  QVERIFY(!QFileInfo::exists(resumeStatePath));
  QVERIFY(!QFileInfo::exists(recoveryStore.incomingStatePath(*started.transferId)));
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
}

void FileTransferRuntimeTests::incomingHydrationValidatesResolvedTargets_data()
{
  QTest::addColumn<QString>("mutation");
  QTest::addColumn<QString>("expectedDiagnostic");
  QTest::newRow("auto-rename-and-zero-byte") << QString{} << QString{};
  QTest::newRow("committed-target-deleted")
      << QStringLiteral("delete") << QStringLiteral("not hydrated");
  QTest::newRow("committed-target-tampered")
      << QStringLiteral("tamper") << QStringLiteral("not hydrated");
  QTest::newRow("zero-byte-target-deleted")
      << QStringLiteral("zero-delete") << QStringLiteral("not hydrated");
  QTest::newRow("legacy-v1-rejected")
      << QStringLiteral("legacy-v1") << QStringLiteral("legacy v1");
  QTest::newRow("resume-sidecar-symlink")
      << QStringLiteral("resume-symlink") << QStringLiteral("not hydrated");
  QTest::newRow("part-parent-symlink")
      << QStringLiteral("part-parent-symlink") << QStringLiteral("not hydrated");
}

void FileTransferRuntimeTests::incomingHydrationValidatesResolvedTargets()
{
  using namespace ::relaydesk::transfer;
  QFETCH(QString, mutation);
  QFETCH(QString, expectedDiagnostic);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const auto receiveRoot = directory.filePath(QStringLiteral("receive"));
  const auto recoveryRoot = directory.filePath(QStringLiteral("recovery"));
  QVERIFY(QDir().mkpath(receiveRoot));
  const auto receiverId = DeviceId::generate();
  const auto senderId = DeviceId::generate();
  const auto transferId = TransferId::generate();
  const auto emptyId = FileId::generate();
  const auto incompleteId = FileId::generate();
  const auto resolvedId = FileId::generate();
  const QByteArray incompleteContents("more");
  const QByteArray resolvedContents("done");
  const auto modifiedUtc = QDateTime::currentDateTimeUtc();
  QList<ManifestEntry> entries{
      {.id = emptyId,
       .relativeProtocolPath = QStringLiteral("empty.bin"),
       .type = ManifestEntryType::File,
       .modifiedUtc = modifiedUtc,
       .sha256 = QCryptographicHash::hash({}, QCryptographicHash::Sha256)},
      {.id = incompleteId,
       .relativeProtocolPath = QStringLiteral("incomplete.bin"),
       .type = ManifestEntryType::File,
       .size = static_cast<quint64>(incompleteContents.size()),
       .modifiedUtc = modifiedUtc,
       .sha256 = QCryptographicHash::hash(incompleteContents, QCryptographicHash::Sha256)},
      {.id = resolvedId,
       .relativeProtocolPath = QStringLiteral("resolved.bin"),
       .type = ManifestEntryType::File,
       .size = static_cast<quint64>(resolvedContents.size()),
       .modifiedUtc = modifiedUtc,
       .sha256 = QCryptographicHash::hash(resolvedContents, QCryptographicHash::Sha256)},
  };
  QList<PreparedManifestEntry> prepared;
  for (const auto &entry : std::as_const(entries)) {
    prepared.append({
        .canonicalSourcePath = QDir::rootPath(),
        .protocolCollisionKey = PathPolicy::validateRelative(entry.relativeProtocolPath).collisionKey,
        .entry = entry,
    });
  }
  const auto manifestSha = ManifestPageCodec::canonicalSha256(entries);
  TransferManifestSummary summary{
      .id = transferId,
      .displayName = QStringLiteral("resolved-targets"),
      .totalBytes = static_cast<quint64>(incompleteContents.size() + resolvedContents.size()),
      .fileCount = 3,
      .canonicalSha256 = manifestSha,
  };
  const auto plan = ManifestPageCodec::plan({.entries = prepared, .summary = summary});
  QVERIFY(plan.ok());
  const TransferOffer offer{
      .transferId = transferId,
      .displayName = summary.displayName,
      .totalBytes = summary.totalBytes,
      .fileCount = summary.fileCount,
      .manifestSha256 = manifestSha,
      .manifestPageCount = plan.plan->pageCount(),
      .requestedConflictPolicy = ConflictPolicy::AutoRename,
      .createdAtMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()),
  };
  const NegotiatedCapabilities capabilities{
      .protocolMajorVersion = 1,
      .features = {QStringLiteral("file.v1"), QStringLiteral("resume.v1")},
      .chunkBytes = 1024 * 1024,
      .maxPayloadBytes = 4 * 1024 * 1024,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 100,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Overwrite, ConflictPolicy::Skip},
      .localCanReceiveFiles = true,
      .peerCanReceiveFiles = true,
  };
  TransferRecoveryStore recoveryStore(recoveryRoot);
  QVERIFY(recoveryStore.saveIncoming({
      .transferId = transferId,
      .localDeviceId = receiverId,
      .peerDeviceId = senderId,
      .peerFingerprintSha256 = identity.fingerprintSha256,
      .peerDisplayName = QStringLiteral("Sender"),
      .offer = offer,
      .receiveOptions = {.destinationRoot = receiveRoot, .conflictPolicy = ConflictPolicy::AutoRename},
      .entries = entries,
      .pagePlan =
          {.entryCount = plan.plan->entryCount,
           .pageCount = plan.plan->pageCount(),
           .totalMetadataBytes = plan.plan->totalMetadataBytes},
      .negotiatedCapabilities = capabilities,
  }).ok());

  const QString resolvedTarget = QDir(receiveRoot).filePath(QStringLiteral("resolved (1).bin"));
  const QString emptyTarget = QDir(receiveRoot).filePath(QStringLiteral("empty.bin"));
  const QString partRelative = QStringLiteral(".incoming/%1/%2.part")
                                   .arg(transferId.toString(), incompleteId.toString());
  const QString partPath = QDir(receiveRoot).filePath(partRelative);
  QVERIFY(QDir().mkpath(QFileInfo(partPath).absolutePath()));
  for (const auto &file : QList<QPair<QString, QByteArray>>{
           {resolvedTarget, resolvedContents}, {emptyTarget, {}}, {partPath, incompleteContents.left(1)}}) {
    QFile output(file.first);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCOMPARE(output.write(file.second), file.second.size());
  }
  ResumeStore resumeStore(QDir(receiveRoot).filePath(QStringLiteral(".incoming/resume-active")));
  ResumeState resume{
      .transferId = transferId,
      .peerDeviceId = senderId,
      .manifestSha256 = manifestSha,
      .direction = ResumeDirection::Receiving,
      .files =
          {{.fileId = incompleteId,
            .relativeProtocolPath = QStringLiteral("incomplete.bin"),
            .durableOffset = 1,
            .totalBytes = static_cast<quint64>(incompleteContents.size()),
            .partRelativePath = partRelative}},
      .resolvedTargets =
          {{.fileId = resolvedId,
            .relativeTargetPath = QStringLiteral("resolved (1).bin"),
            .size = static_cast<quint64>(resolvedContents.size()),
            .sha256 = QCryptographicHash::hash(resolvedContents, QCryptographicHash::Sha256),
            .decision = IncomingConflictDecision::AutoRename},
           {.fileId = emptyId,
            .relativeTargetPath = QStringLiteral("empty.bin"),
            .size = 0,
            .sha256 = QCryptographicHash::hash({}, QCryptographicHash::Sha256),
            .decision = IncomingConflictDecision::Overwrite}},
      .updatedUtc = QDateTime::currentDateTimeUtc(),
  };
  QVERIFY(resumeStore.save(resume).ok());
  if (mutation == QStringLiteral("delete")) {
    QVERIFY(QFile::remove(resolvedTarget));
  } else if (mutation == QStringLiteral("tamper")) {
    QFile changed(resolvedTarget);
    QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(changed.write("evil"), qint64{4});
  } else if (mutation == QStringLiteral("zero-delete")) {
    QVERIFY(QFile::remove(emptyTarget));
  } else if (mutation == QStringLiteral("legacy-v1")) {
    QCborArray files;
    for (const auto &file : std::as_const(resume.files)) {
      files.append(QCborMap{
          {QCborValue(1), file.fileId.toBytes()},
          {QCborValue(2), file.relativeProtocolPath},
          {QCborValue(3), static_cast<qint64>(file.durableOffset)},
          {QCborValue(4), static_cast<qint64>(file.totalBytes)},
          {QCborValue(5), file.partRelativePath},
      });
    }
    const QCborMap legacy{
        {QCborValue(1), static_cast<qint64>(kLegacyResumeStateSchemaVersion)},
        {QCborValue(2), transferId.toBytes()},
        {QCborValue(3), senderId.toBytes()},
        {QCborValue(4), manifestSha},
        {QCborValue(5), QStringLiteral("receiving")},
        {QCborValue(6), files},
        {QCborValue(7), QDateTime::currentMSecsSinceEpoch()},
    };
    QFile legacyFile(resumeStore.statePath(transferId));
    QVERIFY(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const auto encoded = QCborValue(legacy).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
    QCOMPARE(legacyFile.write(encoded), encoded.size());
    legacyFile.close();
    const auto loadedLegacy = resumeStore.load(transferId);
    QVERIFY(loadedLegacy.ok());
    QCOMPARE(loadedLegacy.schemaVersion, kLegacyResumeStateSchemaVersion);
  } else if (mutation == QStringLiteral("resume-symlink")) {
    const QString statePath = resumeStore.statePath(transferId);
    const QString targetPath = statePath + QStringLiteral(".target");
    QVERIFY(QFile::rename(statePath, targetPath));
    if (!QFile::link(targetPath, statePath)) {
      QSKIP("the current Windows token cannot create a symbolic link");
    }
    if (!QFileInfo(statePath).isSymLink()) {
      QFile::remove(statePath);
      QSKIP("platform did not create a symbolic link");
    }
  } else if (mutation == QStringLiteral("part-parent-symlink")) {
    const QString partParent = QFileInfo(partPath).absolutePath();
    const QString targetParent = partParent + QStringLiteral(".target");
    QVERIFY(QDir().rename(partParent, targetParent));
    if (!QFile::link(targetParent, partParent)) {
      QSKIP("the current Windows token cannot create a directory symbolic link");
    }
    if (!QFileInfo(partParent).isSymLink()) {
      QFile::remove(partParent);
      QSKIP("platform did not create a directory symbolic link");
    }
  }

  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("trust.json")));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = recoveryRoot;
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  QStringList errors;
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto error, auto, const QString &message) {
    if (error != FileTransferRuntimeError::PeerUnavailable) {
      errors.append(message);
    }
  });
  QString diagnostic;
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  const auto isHydrated = [&] {
    for (const auto &snapshot : receiver.activeTransfers()) {
      if (snapshot.id == transferId && snapshot.state == TransferState::Interrupted) {
        return true;
      }
    }
    return false;
  };
  if (expectedDiagnostic.isEmpty()) {
    QTRY_VERIFY_WITH_TIMEOUT(isHydrated(), 10'000);
  } else {
    QTRY_VERIFY_WITH_TIMEOUT(errors.join(QLatin1Char(';')).contains(expectedDiagnostic), 10'000);
    QVERIFY(!isHydrated());
  }
}

void FileTransferRuntimeTests::stoppingSenderLeavesOutgoingAtResumableCheckpoint_data()
{
  QTest::addColumn<bool>("reconstructSender");
  QTest::addColumn<bool>("peerReadyBeforeHydration");
  QTest::addColumn<bool>("stopDuringHydration");
  QTest::addColumn<QString>("recoveryMutation");
  QTest::addColumn<QString>("expectedDiagnostic");
  QTest::newRow("listener-restart") << false << false << false << QString{} << QString{};
  QTest::newRow("runtime-reconstruction") << true << false << false << QString{} << QString{};
  QTest::newRow("late-discovery")
      << true << false << false << QStringLiteral("late-discovery") << QString{};
  QTest::newRow("peer-ready-before-hydration") << true << true << false << QString{} << QString{};
  QTest::newRow("stop-during-hydration") << true << false << true << QString{} << QString{};
  QTest::newRow("source-size-changed")
      << true << false << false << QStringLiteral("source-size") << QStringLiteral("not hydrated");
  QTest::newRow("source-hash-changed")
      << true << false << false << QStringLiteral("source-hash") << QStringLiteral("not hydrated");
  QTest::newRow("trust-revoked")
      << true << false << false << QStringLiteral("trust-revoked") << QStringLiteral("trust validation");
  QTest::newRow("fingerprint-changed")
      << true << false << false << QStringLiteral("fingerprint") << QStringLiteral("trust validation");
  QTest::newRow("descriptor-corrupt")
      << true << false << false << QStringLiteral("descriptor") << QStringLiteral("was skipped");
}

void FileTransferRuntimeTests::stoppingSenderLeavesOutgoingAtResumableCheckpoint()
{
  using namespace ::relaydesk::transfer;
  QFETCH(bool, reconstructSender);
  QFETCH(bool, peerReadyBeforeHydration);
  QFETCH(bool, stopDuringHydration);
  QFETCH(QString, recoveryMutation);
  QFETCH(QString, expectedDiagnostic);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(8 * 1024 * 1024 + 113, '\x4f');
  const auto sourcePath = directory.filePath(QStringLiteral("sender-stop-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("sender-stop-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("sender-stop-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("receiver-stop-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender stop sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Sender stop receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = directory.filePath(QStringLiteral("sender-stop-recovery"));
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  auto receiverOptions = options;
  receiverOptions.recoveryStateRoot = directory.filePath(QStringLiteral("receiver-stop-recovery"));
  std::unique_ptr<model::DeviceHomeModel> reconstructedSenderModel;
  std::unique_ptr<DeviceDiscoveryRuntime> reconstructedDiscovery;
  auto sender = std::make_unique<FileTransferRuntime>(
      senderId, senderTrust, senderDiscovery, identityPath, options
  );
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, receiverOptions);
  ResumeStore receiverResumeStore(
      QDir(receiveRoot).filePath(QStringLiteral(".incoming/resume-active"))
  );

  QStringList errors;
  bool senderStopRequested = false;
  bool senderStoppedAtCheckpoint = false;
  bool recordHydrationOrder = false;
  int eventOrder = 0;
  int peerReadyCount = 0;
  int peerReadyOrder = 0;
  int hydrationAddedCount = 0;
  int hydrationOrder = 0;
  bool hydrationVisibleInActiveTransfers = false;
  qsizetype connectionsBeforeHydration = -1;
  qsizetype clientsBeforeHydration = -1;
  QSemaphore workerStarted;
  QSemaphore workerRelease;
  bool workersReleased = false;
  const auto workerReleaseGuard = qScopeGuard([&] {
    if (!workersReleased) {
      workerRelease.release(2);
    }
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });

  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  quint64 interruptedBytes = 0;
  quint64 receiverDurableOffset = 0;
  QStringList senderStates;
  QList<quint64> senderResumingBytes;
  const auto attachSender = [&](FileTransferRuntime &runtime) {
    auto *runtimePointer = &runtime;
    connect(runtimePointer, &FileTransferRuntime::errorOccurred, this, [&](auto error, auto, const QString &message) {
      if (recoveryMutation == QStringLiteral("late-discovery") &&
          error == FileTransferRuntimeError::PeerUnavailable && peerReadyOrder == 0) {
        return;
      }
      if (!senderStoppedAtCheckpoint || error != FileTransferRuntimeError::TransportFailed) {
        errors.append(QStringLiteral("sender: ") + message);
      }
    });
    connect(
        runtimePointer, &IFileTransferService::transferAdded, this,
        [&, runtimePointer](const TransferSnapshot &snapshot) {
          if (snapshot.direction == TransferDirection::Sending) {
            senderLatest = snapshot;
            senderStates.append(QString::number(static_cast<int>(snapshot.state)));
            if (recordHydrationOrder && snapshot.state == TransferState::Interrupted) {
              ++hydrationAddedCount;
              hydrationOrder = ++eventOrder;
              for (const auto &active : runtimePointer->activeTransfers()) {
                if (active.id == snapshot.id && active.state == TransferState::Interrupted) {
                  hydrationVisibleInActiveTransfers = true;
                }
              }
            }
          }
        }
    );
    connect(
        runtimePointer, &IFileTransferService::transferChanged, this,
        [&, runtimePointer](const TransferSnapshot &snapshot) {
          if (snapshot.direction != TransferDirection::Sending) {
            return;
          }
          senderLatest = snapshot;
          senderStates.append(QString::number(static_cast<int>(snapshot.state)));
          if (snapshot.state == TransferState::Resuming) {
            senderResumingBytes.append(snapshot.progress.completedBytes);
          }
        }
    );
  };
  attachSender(*sender);
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
      if (!senderStopRequested && snapshot.state == TransferState::Transferring && senderLatest.has_value() &&
          senderLatest->state == TransferState::Transferring) {
        const auto resume = receiverResumeStore.load(snapshot.id);
        if (resume.ok() && resume.state->files.size() == 1 &&
            resume.state->files.constFirst().durableOffset > 0) {
          receiverDurableOffset = resume.state->files.constFirst().durableOffset;
          senderStopRequested = true;
          auto *runtimePointer = sender.get();
          QTimer::singleShot(0, runtimePointer, [&, runtimePointer] {
            interruptedBytes = senderLatest.has_value() ? senderLatest->progress.completedBytes : 0;
            senderStoppedAtCheckpoint = true;
            runtimePointer->stop();
          });
        }
      }
    }
  });

  QString diagnostic;
  QVERIFY2(sender->start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender->send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(senderStoppedAtCheckpoint, 15'000);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderLatest.has_value() && senderLatest->state == TransferState::Interrupted && senderLatest->canResume &&
          receiverLatest.has_value() && receiverLatest->state == TransferState::Interrupted,
      10'000
  );
  QVERIFY(interruptedBytes >= 1024U * 1024U);
  QVERIFY(interruptedBytes < static_cast<quint64>(sourceBytes.size()));
  QTRY_VERIFY_WITH_TIMEOUT(
      QFileInfo::exists(receiverResumeStore.statePath(*started.transferId)), 5'000
  );
  const auto receiverResume = receiverResumeStore.load(*started.transferId);
  QVERIFY2(receiverResume.ok(), qPrintable(receiverResume.diagnostic));
  QCOMPARE(receiverResume.state->files.size(), qsizetype{1});
  receiverDurableOffset = receiverResume.state->files.constFirst().durableOffset;
  QVERIFY(receiverDurableOffset > 0);

  TransferRecoveryStore recoveryStore(options.recoveryStateRoot);
  QTRY_VERIFY_WITH_TIMEOUT(
      QFileInfo::exists(recoveryStore.outgoingStatePath(*started.transferId)), 5'000
  );
  std::optional<OutgoingRecoveryState> persisted;
  QTRY_VERIFY_WITH_TIMEOUT([&] {
    const auto loaded = recoveryStore.loadOutgoing(*started.transferId);
    if (!loaded.ok() || loaded.state->progress.completedBytes != interruptedBytes) return false;
    persisted = *loaded.state;
    return true;
  }(), 5'000);
  QCOMPARE(persisted->transferId, *started.transferId);
  QCOMPARE(persisted->localDeviceId, senderId);
  QCOMPARE(persisted->peerDeviceId, receiverId);
  QCOMPARE(persisted->peerFingerprintSha256, identity.fingerprintSha256);
  QCOMPARE(persisted->sourceRoots.size(), qsizetype{1});
  QCOMPARE(persisted->sourceRoots.constFirst().canonicalPath, QFileInfo(sourcePath).canonicalFilePath());
  QCOMPARE(persisted->summary.totalBytes, static_cast<quint64>(sourceBytes.size()));
  QCOMPARE(persisted->pagePlan.entryCount, quint64{1});
  QCOMPARE(persisted->progress.completedBytes, interruptedBytes);

  if (reconstructSender) {
    sender.reset();
    senderLatest.reset();
    senderStates.clear();
    senderResumingBytes.clear();
    errors.clear();
    if (recoveryMutation == QStringLiteral("source-size")) {
      QFile changed(sourcePath);
      QVERIFY(changed.open(QIODevice::Append));
      QCOMPARE(changed.write("x"), qint64{1});
    } else if (recoveryMutation == QStringLiteral("source-hash")) {
      const auto modifiedUtc = QFileInfo(sourcePath).lastModified();
      QFile changed(sourcePath);
      QVERIFY(changed.open(QIODevice::ReadWrite));
      QCOMPARE(changed.write("y"), qint64{1});
      QVERIFY(changed.setFileTime(modifiedUtc, QFileDevice::FileModificationTime));
    } else if (recoveryMutation == QStringLiteral("trust-revoked")) {
      QVERIFY(senderTrust.revoke(receiverId));
    } else if (recoveryMutation == QStringLiteral("fingerprint")) {
      auto changedTrust = senderTrust.find(receiverId);
      QVERIFY(changedTrust.has_value());
      changedTrust->fingerprintSha256 = QByteArray(32, '\x71');
      QVERIFY(senderTrust.upsert(*changedTrust));
    } else if (recoveryMutation == QStringLiteral("descriptor")) {
      QFile descriptor(recoveryStore.outgoingStatePath(*started.transferId));
      QVERIFY(descriptor.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QCOMPARE(descriptor.write("corrupt"), qint64{7});
    }
    auto *hydrationDiscovery = &senderDiscovery;
    if (recoveryMutation == QStringLiteral("late-discovery")) {
      reconstructedSenderModel = std::make_unique<model::DeviceHomeModel>();
      reconstructedDiscovery = std::make_unique<DeviceDiscoveryRuntime>(
          localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Reconstructed sender")),
          *reconstructedSenderModel
      );
      hydrationDiscovery = reconstructedDiscovery.get();
    }
    sender = std::make_unique<FileTransferRuntime>(
        senderId, senderTrust, *hydrationDiscovery, identityPath, options
    );
    recordHydrationOrder = true;
    attachSender(*sender);
    connect(sender.get(), &FileTransferRuntime::peerReady, this, [&](DeviceId peer, const auto &) {
      if (peer == receiverId) {
        ++peerReadyCount;
        peerReadyOrder = ++eventOrder;
      }
    });
    if (peerReadyBeforeHydration || stopDuringHydration) {
      FileTransferRuntimeTestAccess::occupyWorkerPool(*sender, workerStarted, workerRelease);
      QVERIFY(workerStarted.tryAcquire(2, 5'000));
    }
    QVERIFY2(sender->start(&diagnostic), qPrintable(diagnostic));
    if (peerReadyBeforeHydration) {
      QVERIFY2(sender->connectPeer(receiverId, &diagnostic), qPrintable(diagnostic));
      QTRY_VERIFY_WITH_TIMEOUT(peerReadyOrder > 0, 10'000);
      connectionsBeforeHydration = FileTransferRuntimeTestAccess::peerConnectionCount(*sender);
      clientsBeforeHydration = FileTransferRuntimeTestAccess::clientCount(*sender);
      workerRelease.release(2);
      workersReleased = true;
    } else if (stopDuringHydration) {
      sender->stop();
      QVERIFY2(sender->start(&diagnostic), qPrintable(diagnostic));
      workerRelease.release(2);
      workersReleased = true;
    }
    if (!expectedDiagnostic.isEmpty()) {
      QTRY_VERIFY_WITH_TIMEOUT(
          errors.join(QLatin1Char(';')).contains(expectedDiagnostic), 10'000
      );
      QVERIFY(sender->activeTransfers().isEmpty());
      QVERIFY(QFileInfo::exists(recoveryStore.outgoingStatePath(*started.transferId)));
      return;
    }
    QTRY_VERIFY_WITH_TIMEOUT(hydrationAddedCount > 0, 10'000);
    QCOMPARE(hydrationAddedCount, 1);
    QVERIFY(hydrationVisibleInActiveTransfers);
    if (recoveryMutation == QStringLiteral("late-discovery")) {
      QVERIFY(hydrationDiscovery->registry().observeAdvertisement(
          receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
      ));
    }
    if (peerReadyBeforeHydration) {
      QVERIFY(peerReadyOrder < hydrationOrder);
      QCOMPARE(peerReadyCount, 1);
      QCOMPARE(FileTransferRuntimeTestAccess::peerConnectionCount(*sender), connectionsBeforeHydration);
      QCOMPARE(FileTransferRuntimeTestAccess::clientCount(*sender), clientsBeforeHydration);
    } else {
      QTRY_VERIFY_WITH_TIMEOUT(peerReadyOrder > 0, 10'000);
      QVERIFY(hydrationOrder < peerReadyOrder);
    }
  } else {
    QVERIFY2(sender->start(&diagnostic), qPrintable(diagnostic));
  }
  if (!reconstructSender) {
    QVERIFY2(sender->connectPeer(receiverId, &diagnostic), qPrintable(diagnostic));
  }

  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 30'000 && (!senderLatest.has_value() || senderLatest->state != TransferState::Completed ||
                                     !receiverLatest.has_value() || receiverLatest->state != TransferState::Completed)
  ) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence = QStringLiteral("errors=[%1] states=[%2] stopped=%3 checkpoint=%4 sender=%5 receiver=%6")
                            .arg(errors.join(QStringLiteral("; ")), senderStates.join(QLatin1Char(',')))
                            .arg(senderStoppedAtCheckpoint)
                            .arg(interruptedBytes)
                            .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
                            .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1);
  QVERIFY2(senderLatest.has_value() && senderLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY2(receiverLatest.has_value() && receiverLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY(senderStates.contains(QString::number(static_cast<int>(TransferState::Interrupted))));
  QVERIFY(senderStates.contains(QString::number(static_cast<int>(TransferState::Resuming))));
  QVERIFY(!senderResumingBytes.isEmpty());
  QVERIFY(senderResumingBytes.constFirst() >= receiverDurableOffset);
  if (reconstructSender) {
    QCOMPARE(senderResumingBytes.constLast(), receiverDurableOffset);
  }
  QTRY_VERIFY_WITH_TIMEOUT(
      !QFileInfo::exists(recoveryStore.outgoingStatePath(*started.transferId)), 5'000
  );
  QFile committed(QDir(receiveRoot).filePath(QStringLiteral("sender-stop-source.bin")));
  QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
  QCOMPARE(committed.readAll(), sourceBytes);
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
}

void FileTransferRuntimeTests::outgoingRecoverySaveFailureStopsBeforeStreaming()
{
  recoveryStoreFailureDoesNotPublishSuccess(true, false);
}

void FileTransferRuntimeTests::outgoingRecoveryRemovalFailureDoesNotPublishTerminalSuccess_data()
{
  QTest::addColumn<bool>("cancelAfterStart");
  QTest::newRow("completed") << false;
  QTest::newRow("cancelled") << true;
}

void FileTransferRuntimeTests::outgoingRecoveryRemovalFailureDoesNotPublishTerminalSuccess()
{
  QFETCH(bool, cancelAfterStart);
  recoveryStoreFailureDoesNotPublishSuccess(false, cancelAfterStart);
}

void FileTransferRuntimeTests::recoveryStoreFailureDoesNotPublishSuccess(
    bool failSave, bool cancelAfterStart
)
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(4 * 1024 * 1024 + 113, '\x39');
  const auto sourcePath = directory.filePath(QStringLiteral("recovery-failure-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("recovery-failure-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto recoveryRoot = directory.filePath(
      failSave ? QStringLiteral("blocked-recovery-root") : QStringLiteral("recovery-root")
  );
  if (failSave) {
    QFile blocker(recoveryRoot);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    QCOMPARE(blocker.write("blocked"), qint64{7});
  }

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("recovery-failure-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("recovery-failure-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Recovery failure sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Recovery failure receiver")),
      receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = recoveryRoot;
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  TransferRecoveryStore recoveryStore(recoveryRoot);
  QSignalSpy operations(&sender, &IFileTransferService::transferOperationFinished);
  QVERIFY(operations.isValid());

  QStringList errors;
  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  bool enteredTransferring = false;
  bool removalFailureInjected = false;
  QString injectionDiagnostic;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(message);
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
    }
  });
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending) {
      return;
    }
    senderLatest = snapshot;
    if (snapshot.state != TransferState::Transferring) {
      return;
    }
    enteredTransferring = true;
    if (failSave || removalFailureInjected) {
      return;
    }
    const auto descriptor = recoveryStore.outgoingStatePath(snapshot.id);
    if (!QFile::remove(descriptor) || !QDir().mkpath(descriptor)) {
      injectionDiagnostic = QStringLiteral("could not replace recovery descriptor with a directory");
      return;
    }
    removalFailureInjected = true;
    if (cancelAfterStart) {
      QTimer::singleShot(0, &sender, [&sender, transferId = snapshot.id] {
        sender.cancel(transferId, {});
      });
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(
      senderLatest.has_value() && senderLatest->state == TransferState::Failed, 15'000
  );

  if (failSave) {
    QVERIFY(!enteredTransferring);
    QVERIFY(errors.join(QLatin1Char(';')).contains(QStringLiteral("could not be saved")));
    return;
  }
  QVERIFY2(removalFailureInjected, qPrintable(injectionDiagnostic));
  QVERIFY(senderLatest->state != TransferState::Completed);
  QVERIFY(senderLatest->state != TransferState::Cancelled);
  QVERIFY(errors.join(QLatin1Char(';')).contains(QStringLiteral("could not be removed")));
  if (cancelAfterStart) {
    QVERIFY(!receiverLatest.has_value() || receiverLatest->state != TransferState::Cancelled);
    QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 5'000);
    const auto *operation = static_cast<const TransferOperationResult *>(
        operations.last().constFirst().constData()
    );
    QVERIFY(operation != nullptr);
    QCOMPARE(operation->operation, TransferOperation::Cancel);
    QCOMPARE(operation->outcome, TransferOperationOutcome::Rejected);
    QCOMPARE(operation->error, TransferOperationError::TransportFailed);
  }
}

void FileTransferRuntimeTests::incomingFolderCommitsEveryFileAndPreservesEmptyDirectories()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QString sourceRoot = directory.filePath(QStringLiteral("folder-payload"));
  const QString nested = QDir(sourceRoot).filePath(QStringLiteral("nested"));
  QVERIFY(QDir().mkpath(QDir(nested).filePath(QStringLiteral("empty"))));
  const QByteArray firstBytes(700'003, '\x31');
  const QByteArray secondBytes(1'100'009, '\x32');
  QFile first(QDir(sourceRoot).filePath(QStringLiteral("first.bin")));
  QVERIFY(first.open(QIODevice::WriteOnly));
  QCOMPARE(first.write(firstBytes), qint64(firstBytes.size()));
  first.close();
  QFile second(QDir(nested).filePath(QStringLiteral("second.bin")));
  QVERIFY(second.open(QIODevice::WriteOnly));
  QCOMPARE(second.write(secondBytes), qint64(secondBytes.size()));
  second.close();
  const QString receiveRoot = directory.filePath(QStringLiteral("multi-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("multi-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("multi-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Multi sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Multi receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  QStringList errors;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });
  std::optional<TransferSnapshot> senderLatest;
  std::optional<TransferSnapshot> receiverLatest;
  QList<quint64> receiverCompletedFiles;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending) {
      senderLatest = snapshot;
    }
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Receiving) {
      return;
    }
    receiverLatest = snapshot;
    if (snapshot.progress.completedFiles != 0 &&
        !receiverCompletedFiles.contains(snapshot.progress.completedFiles)) {
      receiverCompletedFiles.append(snapshot.progress.completedFiles);
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  auto receiverInfo = receiverDiscovery.service().localDevice();
  QVERIFY(senderDiscovery.registry().observeAdvertisement(receiverInfo, QHostAddress::LocalHost));
  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourceRoot)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 20'000 &&
         (!senderLatest.has_value() || senderLatest->state != TransferState::Completed ||
          !receiverLatest.has_value() || receiverLatest->state != TransferState::Completed)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence = QStringLiteral("errors=[%1] sender=%2 receiver=%3 receiverFiles=[%4]")
                            .arg(errors.join(QStringLiteral("; ")))
                            .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
                            .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1)
                            .arg([&]() {
                              QStringList values;
                              for (const auto value : receiverCompletedFiles) {
                                values.append(QString::number(value));
                              }
                              return values.join(QLatin1Char(','));
                            }());
  QVERIFY2(senderLatest.has_value() && senderLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY2(receiverLatest.has_value() && receiverLatest->state == TransferState::Completed, qPrintable(evidence));
  QCOMPARE(receiverLatest->progress.completedFiles, quint64{2});
  QCOMPARE(receiverLatest->progress.completedBytes, quint64(firstBytes.size() + secondBytes.size()));
  QCOMPARE(receiverCompletedFiles, QList<quint64>({1, 2}));
  const QDir committedRoot(QDir(receiveRoot).filePath(QStringLiteral("folder-payload")));
  QVERIFY(committedRoot.exists(QStringLiteral("nested/empty")));
  QFile committedFirst(committedRoot.filePath(QStringLiteral("first.bin")));
  QFile committedSecond(committedRoot.filePath(QStringLiteral("nested/second.bin")));
  QVERIFY(committedFirst.open(QIODevice::ReadOnly));
  QVERIFY(committedSecond.open(QIODevice::ReadOnly));
  QCOMPARE(committedFirst.readAll(), firstBytes);
  QCOMPARE(committedSecond.readAll(), secondBytes);
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
}

void FileTransferRuntimeTests::runtimeSourceUsesCanonicalSenderBoundary()
{
#if !defined(RELAYDESK_FILE_TRANSFER_RUNTIME_SOURCE_PATH)
  QFAIL("runtime source path compile definition is missing");
#else
  QFile source(QString::fromUtf8(RELAYDESK_FILE_TRANSFER_RUNTIME_SOURCE_PATH));
  QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
  const QByteArray contents = source.readAll();
  QVERIFY(contents.contains("TransferSenderPump"));
  QVERIFY(contents.contains("FileTlsFrameSink"));
  QVERIFY(contents.contains("SenderBackpressureLimits"));
  QVERIFY(!contents.contains("CapturingFrameSink"));
  QVERIFY(!contents.contains("writeHighWaterBytes() / 2"));
  QVERIFY(!contents.contains("singleShot(5"));
#endif
}

void FileTransferRuntimeTests::runtimeSourceComposesPlatformReceiver()
{
#if !defined(RELAYDESK_FILE_TRANSFER_RUNTIME_SOURCE_PATH)
  QFAIL("runtime source path compile definition is missing");
#else
  QFile source(QString::fromUtf8(RELAYDESK_FILE_TRANSFER_RUNTIME_SOURCE_PATH));
  QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
  const QByteArray contents = source.readAll();
  QVERIFY(contents.contains("createPlatformFileSafety"));
  QVERIFY(contents.contains(".fileV1 = true, .folderV1 = true, .resumeV1 = true"));
  QVERIFY(contents.contains("case MessageType::TransferOffer:"));
  QVERIFY(contents.contains("case MessageType::FileEnd:"));
#endif
}

void FileTransferRuntimeTests::runtimeEnablesComposedReceiverCapability()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x22'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.localCapabilities.features.append(QStringLiteral("file.receive.v1"));
  FileTransferRuntime runtime(localId, trust, discovery, identityPath, options);
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);
  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  QCOMPARE(discovery.service().localDevice().filePort, runtime.listeningPort());
  QVERIFY(discovery.service().localDevice().capabilities.fileV1);
  QCOMPARE(errors.count(), 0);
}

void FileTransferRuntimeTests::commandsSynchronizeTransfer(
    bool receiverControls, ::relaydesk::transfer::PartialDisposition partialDisposition, bool repeatCancel
)
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(12 * 1024 * 1024 + 113, '\x5a');
  const auto sourcePath = directory.filePath(QStringLiteral("commands-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("commands-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("commands-sender-trust.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("commands-receiver-trust.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Command sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Command receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = directory.filePath(QStringLiteral("commands-recovery"));
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  TransferRecoveryStore recoveryStore(options.recoveryStateRoot);
  QSignalSpy senderOperations(&sender, &IFileTransferService::transferOperationFinished);
  QSignalSpy receiverOperations(&receiver, &IFileTransferService::transferOperationFinished);
  QVERIFY(senderOperations.isValid());
  QVERIFY(receiverOperations.isValid());
  const auto pauseTransfer = [&](const TransferId &transferId) {
    if (receiverControls) {
      receiver.pause(transferId);
    } else {
      sender.pause(transferId);
    }
  };
  const auto resumeTransfer = [&](const TransferId &transferId) {
    if (receiverControls) {
      receiver.resume(transferId);
    } else {
      sender.resume(transferId);
    }
  };
  const auto cancelTransfer = [&](const TransferId &transferId) {
    const TransferCancelOptions options{.partialDisposition = partialDisposition};
    if (receiverControls) {
      receiver.cancel(transferId, options);
    } else {
      sender.cancel(transferId, options);
    }
  };
  QStringList errors;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(&receiver, &FileTransferRuntime::errorOccurred, this, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });
  connect(&receiver, &IFileTransferService::incomingOffer, this, [&](const IncomingOffer &offer) {
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });

  std::optional<TransferId> pausedTransfer;
  std::optional<TransferId> cancelledTransfer;
  bool pauseSent = false;
  bool cancelSent = false;
  std::optional<TransferSnapshot> senderPaused;
  std::optional<TransferSnapshot> receiverPaused;
  std::optional<TransferSnapshot> senderCancelled;
  std::optional<TransferSnapshot> receiverCancelled;
  QList<TransferState> receiverCancelStates;
  bool receiverCancellationEventLoopResponsive = false;
  bool duplicateCancelScheduled = false;
  bool duplicateCancelSent = false;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending) {
      return;
    }
    if (pausedTransfer.has_value() && snapshot.id == *pausedTransfer) {
      senderPaused = snapshot;
      if (!pauseSent && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= 1024U * 1024U) {
        pauseSent = true;
        pauseTransfer(snapshot.id);
      }
    }
    if (cancelledTransfer.has_value() && snapshot.id == *cancelledTransfer) {
      senderCancelled = snapshot;
    }
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Receiving) {
      return;
    }
    if (pausedTransfer.has_value() && snapshot.id == *pausedTransfer) {
      receiverPaused = snapshot;
    }
    if (cancelledTransfer.has_value() && snapshot.id == *cancelledTransfer) {
      receiverCancelled = snapshot;
      receiverCancelStates.append(snapshot.state);
      if (!cancelSent && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= 1024U * 1024U &&
          QFileInfo::exists(recoveryStore.outgoingStatePath(snapshot.id))) {
        cancelSent = true;
        cancelTransfer(snapshot.id);
      }
      if (snapshot.state == TransferState::Cancelling) {
        QTimer::singleShot(0, this, [&] { receiverCancellationEventLoopResponsive = true; });
      }
      if (repeatCancel && snapshot.state == TransferState::Cancelled && !duplicateCancelScheduled) {
        duplicateCancelScheduled = true;
        QTimer::singleShot(0, this, [&] {
          cancelTransfer(*cancelledTransfer);
          duplicateCancelSent = true;
        });
      }
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  const auto paused = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(paused.ok(), qPrintable(paused.diagnostic));
  QVERIFY(paused.transferId.has_value());
  pausedTransfer = *paused.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(
      pauseSent && senderPaused.has_value() && receiverPaused.has_value() &&
          senderPaused->state == TransferState::Paused && receiverPaused->state == TransferState::Paused,
      15'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      QFileInfo::exists(recoveryStore.outgoingStatePath(*pausedTransfer)), 5'000
  );
  const auto pausedReceiverBytes = receiverPaused->progress.completedBytes;
  QTest::qWait(200);
  QCOMPARE(receiverPaused->state, TransferState::Paused);
  QCOMPARE(receiverPaused->progress.completedBytes, pausedReceiverBytes);
  resumeTransfer(*pausedTransfer);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderPaused.has_value() && receiverPaused.has_value() && senderPaused->state == TransferState::Completed &&
          receiverPaused->state == TransferState::Completed,
      20'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      !QFileInfo::exists(recoveryStore.outgoingStatePath(*pausedTransfer)), 5'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      !QFileInfo::exists(recoveryStore.incomingStatePath(*pausedTransfer)), 5'000
  );

  const auto cancelled = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(cancelled.ok(), qPrintable(cancelled.diagnostic));
  QVERIFY(cancelled.transferId.has_value());
  cancelledTransfer = *cancelled.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(
      cancelSent && senderCancelled.has_value() && receiverCancelled.has_value() &&
          senderCancelled->state == TransferState::Cancelled && receiverCancelled->state == TransferState::Cancelled &&
          (!repeatCancel || duplicateCancelSent),
      15'000
  );
  QVERIFY(receiverCancellationEventLoopResponsive);
  QVERIFY(receiverCancelStates.indexOf(TransferState::Cancelling) >= 0);
  QVERIFY(receiverCancelStates.indexOf(TransferState::Cancelling) <
          receiverCancelStates.lastIndexOf(TransferState::Cancelled));
  const auto staging = QDir(receiveRoot).filePath(
      QStringLiteral(".incoming/%1").arg(cancelledTransfer->toString())
  );
  QCOMPARE(QFileInfo::exists(staging), partialDisposition == PartialDisposition::Keep);
  const auto resumeState = QDir(receiveRoot).filePath(
      QStringLiteral(".incoming/resume-active/%1.resume.cbor").arg(cancelledTransfer->toString())
  );
  QCOMPARE(QFileInfo::exists(resumeState), partialDisposition == PartialDisposition::Keep);
  QTRY_VERIFY_WITH_TIMEOUT(
      !QFileInfo::exists(recoveryStore.outgoingStatePath(*cancelledTransfer)), 5'000
  );
  QCOMPARE(
      QFileInfo::exists(recoveryStore.incomingStatePath(*cancelledTransfer)),
      partialDisposition == PartialDisposition::Keep
  );
  const auto &controllerOperations = receiverControls ? receiverOperations : senderOperations;
  std::optional<TransferOperationResult> lastCancelOperation;
  for (qsizetype index = 0; index < controllerOperations.count(); ++index) {
    const auto *operation = static_cast<const TransferOperationResult *>(
        controllerOperations.at(index).constFirst().constData()
    );
    QVERIFY(operation != nullptr);
    if (operation->transferId == *cancelledTransfer && operation->operation == TransferOperation::Cancel) {
      lastCancelOperation = *operation;
    }
  }
  QVERIFY(lastCancelOperation.has_value());
  QCOMPARE(
      lastCancelOperation->outcome,
      repeatCancel ? TransferOperationOutcome::Idempotent : TransferOperationOutcome::Applied
  );
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));
}

void FileTransferRuntimeTests::remoteCommandsSynchronizeIncomingTransfer()
{
  commandsSynchronizeTransfer(false, ::relaydesk::transfer::PartialDisposition::Remove);
}

void FileTransferRuntimeTests::incomingControlsSynchronizeOutgoingTransfer()
{
  commandsSynchronizeTransfer(true, ::relaydesk::transfer::PartialDisposition::Remove);
}

void FileTransferRuntimeTests::incomingCancelKeepsPartialDataAndIsIdempotent()
{
  commandsSynchronizeTransfer(true, ::relaydesk::transfer::PartialDisposition::Keep, true);
}

void FileTransferRuntimeTests::incomingControlsRejectTransportFailureWithoutLocalMutation_data()
{
  using namespace ::relaydesk::transfer;
  QTest::addColumn<TransferOperation>("operation");
  QTest::newRow("pause") << TransferOperation::Pause;
  QTest::newRow("resume") << TransferOperation::Resume;
  QTest::newRow("cancel") << TransferOperation::Cancel;
}

void FileTransferRuntimeTests::incomingControlsRejectTransportFailureWithoutLocalMutation()
{
  using namespace ::relaydesk::transfer;
  QFETCH(TransferOperation, operation);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  const QByteArray sourceBytes(12 * 1024 * 1024 + 113, '\x5a');
  const auto sourcePath = directory.filePath(QStringLiteral("transport-failure-source.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto receiveRoot = directory.filePath(QStringLiteral("transport-failure-received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(directory.filePath(QStringLiteral("transport-failure-sender.json")));
  TrustedDeviceStore receiverTrust(directory.filePath(QStringLiteral("transport-failure-receiver.json")));
  QVERIFY(senderTrust.upsert(trustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(trustedDevice(senderId, identity.fingerprintSha256)));
  model::DeviceHomeModel senderModel;
  model::DeviceHomeModel receiverModel;
  DeviceDiscoveryRuntime senderDiscovery(
      localDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderModel
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      localDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), receiverModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  std::optional<TransferSnapshot> receiverLatest;
  QSignalSpy operations(&receiver, &IFileTransferService::transferOperationFinished);
  QVERIFY(operations.isValid());
  QObject connectionContext;
  connect(&receiver, &IFileTransferService::incomingOffer, &connectionContext, [&](const IncomingOffer &offer) {
    receiver.accept(offer.offer.transferId, {.destinationRoot = receiveRoot});
  });
  connect(&receiver, &IFileTransferService::transferChanged, &connectionContext, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Receiving) {
      receiverLatest = snapshot;
    }
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QVERIFY(started.transferId.has_value());
  const auto transferId = *started.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(
      receiverLatest.has_value() && receiverLatest->id == transferId &&
          receiverLatest->state == TransferState::Transferring &&
          receiverLatest->progress.completedBytes >= 1024U * 1024U,
      15'000
  );

  if (operation == TransferOperation::Resume) {
    receiver.pause(transferId);
    QTRY_VERIFY_WITH_TIMEOUT(
        receiverLatest.has_value() && receiverLatest->state == TransferState::Paused, 5'000
    );
  }
  const auto before = *receiverLatest;
  const auto operationCountBefore = operations.count();
  FileTransferRuntimeTestAccess::removePeerChannel(receiver, senderId);
  switch (operation) {
  case TransferOperation::Pause:
    receiver.pause(transferId);
    break;
  case TransferOperation::Resume:
    receiver.resume(transferId);
    break;
  case TransferOperation::Cancel:
    receiver.cancel(transferId, {.partialDisposition = PartialDisposition::Remove});
    break;
  default:
    QFAIL("unexpected operation");
  }
  QCOMPARE(receiverLatest->state, before.state);
  QCOMPARE(receiverLatest->progress.completedBytes, before.progress.completedBytes);
  QCOMPARE(operations.count(), operationCountBefore + 1);
  const auto *result = static_cast<const TransferOperationResult *>(operations.last().constFirst().constData());
  QVERIFY(result != nullptr);
  QCOMPARE(result->transferId, transferId);
  QCOMPARE(result->operation, operation);
  QCOMPARE(result->outcome, TransferOperationOutcome::Rejected);
  QCOMPARE(result->error, TransferOperationError::TransportFailed);
}

void FileTransferRuntimeTests::unknownControlOperationsPublishTypedResults()
{
  using namespace ::relaydesk::transfer;

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x22'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntime runtime(
      localId, trust, discovery, directory.filePath(QStringLiteral("unused.pem"))
  );
  QSignalSpy results(&runtime, &IFileTransferService::transferOperationFinished);
  QVERIFY(results.isValid());
  const auto unknown = TransferId::generate();

  runtime.accept(unknown, {});
  runtime.reject(unknown, RejectReason::UserDeclined);
  runtime.pause(unknown);
  runtime.resume(unknown);
  runtime.cancel(unknown, {});
  runtime.retry(unknown);

  QCOMPARE(results.count(), 6);
  const QList<TransferOperation> expected{
      TransferOperation::Accept, TransferOperation::Reject, TransferOperation::Pause,
      TransferOperation::Resume, TransferOperation::Cancel, TransferOperation::Retry,
  };
  for (qsizetype index = 0; index < expected.size(); ++index) {
    const auto &stored = results.at(index).constFirst();
    const auto *result = static_cast<const TransferOperationResult *>(stored.constData());
    QVERIFY(result != nullptr);
    QCOMPARE(result->transferId, unknown);
    QCOMPARE(result->operation, expected.at(index));
    QCOMPARE(result->outcome, TransferOperationOutcome::Rejected);
    QCOMPARE(result->error, TransferOperationError::UnknownTransfer);
    QVERIFY(!result->ok());
  }
}

void FileTransferRuntimeTests::invalidIdentityFailsWithoutPublishingAListener()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discovery(
      localDevice(localId, QByteArray(32, '\x22'), QStringLiteral("Local")), deviceModel
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(
      localId, trust, discovery, directory.filePath(QStringLiteral("missing.pem")), options
  );
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY(!runtime.start(&diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(errors.count(), 1);
  QCOMPARE(
      qvariant_cast<FileTransferRuntimeError>(errors.first().at(0)),
      FileTransferRuntimeError::ListenerFailed
  );
}

QTEST_MAIN(FileTransferRuntimeTests)

#include "FileTransferRuntimeTests.moc"
