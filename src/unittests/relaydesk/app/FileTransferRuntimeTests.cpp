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
  void trustedPeersNegotiateIndependentFileChannel();
  void outgoingSingleFileStreamsThroughWorkerPump();
  void incomingSingleFileCommitsThroughPlatformReceiver();
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
  QVERIFY(!discovery.service().localDevice().capabilities.folderV1);
  QVERIFY(!discovery.service().localDevice().capabilities.resumeV1);
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
  QVERIFY(!discovery.service().localDevice().capabilities.folderV1);
  QVERIFY(!discovery.service().localDevice().capabilities.resumeV1);
  QCOMPARE(started.count(), 2);
  QCOMPARE(errors.count(), 0);
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
  QVERIFY(contents.contains("FileEndpointAnnouncement{.port = listeningPort(), .fileV1 = true}"));
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
