/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/transfer/ControlMessageCodec.h"
#include "relaydesk/transfer/FileMessageCodec.h"
#include "relaydesk/transfer/ManifestPageCodec.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "../TestTlsIdentity.h"

#include <QSignalSpy>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <limits>

using namespace deskflow::relaydesk;

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
  void stoppingWhilePreparingPublishesRetryableFailure();
  void stoppingBeforeStreamingPublishesRetryableFailure_data();
  void stoppingBeforeStreamingPublishesRetryableFailure();
  void trustedPeersNegotiateIndependentFileChannel();
  void outgoingSingleFileStreamsThroughWorkerPump();
  void incomingSingleFileCommitsThroughPlatformReceiver();
  void incomingConflictPolicies_data();
  void incomingConflictPolicies();
  void interruptedIncomingFileResumesFromDurableCheckpoint();
  void stoppingSenderLeavesOutgoingAtResumableCheckpoint();
  void incomingFolderCommitsEveryFileAndPreservesEmptyDirectories();
  void runtimeSourceUsesCanonicalSenderBoundary();
  void runtimeSourceComposesPlatformReceiver();
  void runtimeEnablesComposedReceiverCapability();
  void unknownControlOperationsPublishTypedResults();
  void invalidIdentityFailsWithoutPublishingAListener();
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

  QTest::addColumn<ConflictPolicy>("policy");
  QTest::addColumn<QString>("committedName");
  QTest::addColumn<bool>("originalReplaced");
  QTest::newRow("auto-rename") << ConflictPolicy::AutoRename << QStringLiteral("conflict (1).bin") << false;
  QTest::newRow("overwrite") << ConflictPolicy::Overwrite << QStringLiteral("conflict.bin") << true;
  QTest::newRow("skip") << ConflictPolicy::Skip << QString{} << false;
  QTest::newRow("ask-user-accept") << ConflictPolicy::Ask << QStringLiteral("conflict (1).bin") << false;
}

void FileTransferRuntimeTests::incomingConflictPolicies()
{
  using namespace ::relaydesk::transfer;

  QFETCH(ConflictPolicy, policy);
  QFETCH(QString, committedName);
  QFETCH(bool, originalReplaced);
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
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));
  const auto started = sender.send(
      receiverId, {QUrl::fromLocalFile(sourcePath)}, {.conflictPolicy = policy}
  );
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 15'000 &&
         (!senderLatest.has_value() || senderLatest->state != TransferState::Completed ||
          !receiverLatest.has_value() || receiverLatest->state != TransferState::Completed)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  const auto evidence = QStringLiteral("policy=%1 errors=[%2] sender=%3 receiver=%4")
                            .arg(static_cast<int>(policy))
                            .arg(errors.join(QStringLiteral("; ")))
                            .arg(senderLatest.has_value() ? static_cast<int>(senderLatest->state) : -1)
                            .arg(receiverLatest.has_value() ? static_cast<int>(receiverLatest->state) : -1);
  QVERIFY2(senderLatest.has_value() && senderLatest->state == TransferState::Completed, qPrintable(evidence));
  QVERIFY2(receiverLatest.has_value() && receiverLatest->state == TransferState::Completed, qPrintable(evidence));
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
  QCOMPARE(receiverLatest->progress.completedFiles, quint64{1});
  QCOMPARE(receiverLatest->progress.completedBytes, static_cast<quint64>(sourceBytes.size()));
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
}

void FileTransferRuntimeTests::interruptedIncomingFileResumesFromDurableCheckpoint()
{
  using namespace ::relaydesk::transfer;

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
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);
  QStringList errors;
  bool receiverStoppedAtCheckpoint = false;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto error, auto, const QString &message) {
    if (!receiverStoppedAtCheckpoint || error != FileTransferRuntimeError::TransportFailed) {
      errors.append(QStringLiteral("sender: ") + message);
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
  QStringList senderStates;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending) {
      return;
    }
    senderLatest = snapshot;
    senderStates.append(QString::number(static_cast<int>(snapshot.state)));
  });
  connect(&receiver, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Receiving) {
      return;
    }
    receiverLatest = snapshot;
    if (!receiverStoppedAtCheckpoint && snapshot.state == TransferState::Transferring &&
        snapshot.progress.completedBytes >= 1024U * 1024U &&
        snapshot.progress.completedBytes < snapshot.progress.totalBytes) {
      receiverStoppedAtCheckpoint = true;
      interruptedBytes = snapshot.progress.completedBytes;
      receiver.stop();
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

  QVERIFY2(receiver.start(&diagnostic), qPrintable(diagnostic));
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
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
}

void FileTransferRuntimeTests::stoppingSenderLeavesOutgoingAtResumableCheckpoint()
{
  using namespace ::relaydesk::transfer;

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
  options.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  FileTransferRuntime receiver(receiverId, receiverTrust, receiverDiscovery, identityPath, options);

  QStringList errors;
  bool senderStopRequested = false;
  bool senderStoppedAtCheckpoint = false;
  connect(&sender, &FileTransferRuntime::errorOccurred, this, [&](auto error, auto, const QString &message) {
    if (!senderStoppedAtCheckpoint || error != FileTransferRuntimeError::TransportFailed) {
      errors.append(QStringLiteral("sender: ") + message);
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
  QStringList senderStates;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending) {
      return;
    }
    senderLatest = snapshot;
    senderStates.append(QString::number(static_cast<int>(snapshot.state)));
    if (!senderStopRequested && snapshot.state == TransferState::Transferring &&
        snapshot.progress.completedBytes >= 1024U * 1024U &&
        snapshot.progress.completedBytes < snapshot.progress.totalBytes) {
      senderStopRequested = true;
      interruptedBytes = snapshot.progress.completedBytes;
      QTimer::singleShot(0, &sender, [&] {
        senderStoppedAtCheckpoint = true;
        sender.stop();
      });
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
  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QTRY_VERIFY_WITH_TIMEOUT(senderStoppedAtCheckpoint, 15'000);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderLatest.has_value() && senderLatest->state == TransferState::Interrupted && senderLatest->canResume &&
          receiverLatest.has_value() && receiverLatest->state == TransferState::Interrupted,
      10'000
  );
  QVERIFY(interruptedBytes >= 1024U * 1024U);
  QVERIFY(interruptedBytes < static_cast<quint64>(sourceBytes.size()));

  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(sender.connectPeer(receiverId, &diagnostic), qPrintable(diagnostic));

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
  QFile committed(QDir(receiveRoot).filePath(QStringLiteral("sender-stop-source.bin")));
  QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.errorString()));
  QCOMPARE(committed.readAll(), sourceBytes);
  QVERIFY2(errors.isEmpty(), qPrintable(evidence));
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
