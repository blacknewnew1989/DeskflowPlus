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
  QVERIFY(firstDiscovery.registry().observeAdvertisement(
      secondDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

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
        Frame response{
            .type = MessageType::Capabilities,
            .metadata = CapabilityCodec::encode(options.localCapabilities, &encodeDiagnostic),
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
