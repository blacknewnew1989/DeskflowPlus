/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/TransferUiRuntime.h"

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/platform/PermissionSnapshot.h"
#include "relaydesk/transfer/TransferHistoryStore.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "relaydesk/widgets/DevicesDock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <optional>
#include <type_traits>
#include <utility>

using namespace deskflow::relaydesk;
using namespace relaydesk::transfer;

class QueuedBoundaryReceiver final : public QObject
{
  Q_OBJECT

public:
  std::optional<DeviceId> deviceId;
  std::optional<TransferId> transferId;
  std::optional<FileId> fileId;
  std::optional<IncomingOffer> incomingOffer;
  std::optional<TransferSnapshot> transferSnapshot;
  std::optional<TransferHistoryRecord> historyRecord;
  std::optional<SendOptions> sendOptions;
  std::optional<ReceiveOptions> receiveOptions;
  std::optional<TransferCancelOptions> cancelOptions;
  std::optional<PermissionSnapshot> permissionSnapshot;
  std::optional<DeviceId> readyPeerDeviceId;
  std::optional<NegotiatedCapabilities> negotiatedCapabilities;
  QThread *deliveryThread = nullptr;
  QThread *peerReadyDeliveryThread = nullptr;

public Q_SLOTS:
  void receive(
      DeviceId receivedDeviceId, TransferId receivedTransferId, FileId receivedFileId,
      IncomingOffer receivedIncomingOffer, TransferSnapshot receivedTransferSnapshot,
      TransferHistoryRecord receivedHistoryRecord, SendOptions receivedSendOptions,
      ReceiveOptions receivedReceiveOptions, TransferCancelOptions receivedCancelOptions,
      PermissionSnapshot receivedPermissionSnapshot
  )
  {
    deviceId = std::move(receivedDeviceId);
    transferId = std::move(receivedTransferId);
    fileId = std::move(receivedFileId);
    incomingOffer = std::move(receivedIncomingOffer);
    transferSnapshot = std::move(receivedTransferSnapshot);
    historyRecord = std::move(receivedHistoryRecord);
    sendOptions = std::move(receivedSendOptions);
    receiveOptions = std::move(receivedReceiveOptions);
    cancelOptions = std::move(receivedCancelOptions);
    permissionSnapshot = std::move(receivedPermissionSnapshot);
    deliveryThread = QThread::currentThread();
    Q_EMIT delivered();
  }

  void receivePeerReady(
      DeviceId receivedPeerDeviceId, NegotiatedCapabilities receivedCapabilities
  )
  {
    readyPeerDeviceId = std::move(receivedPeerDeviceId);
    negotiatedCapabilities = std::move(receivedCapabilities);
    peerReadyDeliveryThread = QThread::currentThread();
    Q_EMIT peerReadyDelivered();
  }

Q_SIGNALS:
  void delivered();
  void peerReadyDelivered();
};

namespace {

struct CapturedQtMessages
{
  QMutex mutex;
  QStringList messages;
};

CapturedQtMessages *g_capturedQtMessages = nullptr;

void captureQtMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
  if (g_capturedQtMessages == nullptr)
    return;
  const QMutexLocker locker(&g_capturedQtMessages->mutex);
  g_capturedQtMessages->messages.append(message);
}

class ScopedMessageCapture final
{
public:
  explicit ScopedMessageCapture(CapturedQtMessages &capture) : m_previous(qInstallMessageHandler(captureQtMessage))
  {
    g_capturedQtMessages = &capture;
  }

  ~ScopedMessageCapture()
  {
    g_capturedQtMessages = nullptr;
    qInstallMessageHandler(m_previous);
  }

private:
  QtMessageHandler m_previous;
};

class ScopedReceiverThread final
{
public:
  ScopedReceiverThread(QThread &thread, QueuedBoundaryReceiver &receiver)
      : m_thread(thread), m_receiver(receiver), m_ownerThread(QThread::currentThread())
  {
  }

  ~ScopedReceiverThread()
  {
    if (m_thread.isRunning()) {
      QMetaObject::invokeMethod(
          &m_receiver, [this]() { m_receiver.moveToThread(m_ownerThread); }, Qt::BlockingQueuedConnection
      );
      m_thread.quit();
      m_thread.wait();
    }
  }

private:
  QThread &m_thread;
  QueuedBoundaryReceiver &m_receiver;
  QThread *m_ownerThread;
};

using SendMethod = TransferStartResult (IFileTransferService::*)(
    const DeviceId &, const QList<QUrl> &, const SendOptions &
);
using AcceptMethod = void (IFileTransferService::*)(const TransferId &, const ReceiveOptions &);
using RejectMethod = void (IFileTransferService::*)(const TransferId &, RejectReason);
using TransferIdMethod = void (IFileTransferService::*)(const TransferId &);
using CancelMethod = void (IFileTransferService::*)(const TransferId &, const TransferCancelOptions &);
using ActiveTransfersMethod = QList<TransferSnapshot> (IFileTransferService::*)() const;
using IncomingOfferSignal = void (IFileTransferService::*)(IncomingOffer);
using TransferSnapshotSignal = void (IFileTransferService::*)(TransferSnapshot);
using TransferRemovedSignal = void (IFileTransferService::*)(TransferId);
using DiscoveryEndpointMethod = bool (DiscoveryService::*)(FileEndpointAnnouncement, QString *);
using DiscoveryRuntimeEndpointMethod = bool (DeviceDiscoveryRuntime::*)(FileEndpointAnnouncement, QString *);
using SendItemsIntent = void (widgets::DevicesDock::*)(DeviceId, QList<QUrl>, SendOptions);
using AcceptIntent = void (model::IncomingOfferModel::*)(TransferId, ReceiveOptions);
using RejectIntent = void (model::IncomingOfferModel::*)(TransferId, RejectReason);
using TransferIntent = void (model::TransferCenterModel::*)(TransferId);
using CancelIntent = void (model::TransferCenterModel::*)(TransferId, TransferCancelOptions);
using PeerReadySignal = void (FileTransferRuntime::*)(DeviceId, NegotiatedCapabilities);

static_assert(std::is_abstract_v<IFileTransferService>);
static_assert(std::is_base_of_v<IFileTransferService, FileTransferRuntime>);
static_assert(std::is_final_v<FileTransferRuntime>);
static_assert(!std::is_copy_constructible_v<IFileTransferService>);
static_assert(!std::is_copy_constructible_v<FileTransferRuntime>);

static_assert(std::is_same_v<decltype(&IFileTransferService::send), SendMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::accept), AcceptMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::reject), RejectMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::pause), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::resume), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::cancel), CancelMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::retry), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::activeTransfers), ActiveTransfersMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::incomingOffer), IncomingOfferSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferAdded), TransferSnapshotSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferChanged), TransferSnapshotSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferRemoved), TransferRemovedSignal>);
static_assert(std::is_same_v<decltype(&DiscoveryService::setFileEndpoint), DiscoveryEndpointMethod>);
static_assert(
    std::is_same_v<decltype(&DeviceDiscoveryRuntime::setFileEndpoint), DiscoveryRuntimeEndpointMethod>
);
static_assert(std::is_same_v<decltype(&widgets::DevicesDock::sendItemsRequested), SendItemsIntent>);
static_assert(std::is_same_v<decltype(&model::IncomingOfferModel::acceptRequested), AcceptIntent>);
static_assert(std::is_same_v<decltype(&model::IncomingOfferModel::rejectRequested), RejectIntent>);
static_assert(std::is_same_v<decltype(&model::TransferCenterModel::pauseRequested), TransferIntent>);
static_assert(std::is_same_v<decltype(&model::TransferCenterModel::resumeRequested), TransferIntent>);
static_assert(std::is_same_v<decltype(&model::TransferCenterModel::retryRequested), TransferIntent>);
static_assert(std::is_same_v<decltype(&model::TransferCenterModel::historyRetryRequested), TransferIntent>);
static_assert(std::is_same_v<decltype(&model::TransferCenterModel::cancelRequested), CancelIntent>);
static_assert(std::is_same_v<decltype(&FileTransferRuntime::peerReady), PeerReadySignal>);
static_assert(
    std::is_constructible_v<
        TransferUiRuntime, IFileTransferService &, widgets::DevicesDock &, widgets::TransferCenterDock &,
        model::IncomingOfferModel &
    >
);

static_assert(std::is_same_v<std::underlying_type_t<RejectReason>, quint32>);
static_assert(static_cast<quint32>(RejectReason::UserDeclined) == 1);
static_assert(static_cast<quint32>(RejectReason::NotTrusted) == 2);
static_assert(static_cast<quint32>(RejectReason::PolicyDenied) == 3);
static_assert(static_cast<quint32>(RejectReason::InsufficientSpace) == 4);
static_assert(static_cast<quint32>(RejectReason::TooManyFiles) == 5);
static_assert(static_cast<quint32>(RejectReason::PathInvalid) == 6);
static_assert(static_cast<quint32>(RejectReason::UnsupportedCapability) == 7);
static_assert(static_cast<quint32>(RejectReason::Busy) == 8);
static_assert(static_cast<quint32>(RejectReason::InternalError) == 9);
static_assert(std::is_same_v<std::underlying_type_t<TransferCancelReason>, quint32>);
static_assert(static_cast<quint32>(TransferCancelReason::UserRequested) == 1);
static_assert(static_cast<quint32>(TransferCancelReason::ApplicationShutdown) == 2);
static_assert(std::is_same_v<std::underlying_type_t<PartialDisposition>, quint8>);
static_assert(static_cast<quint8>(PartialDisposition::Keep) == 0);
static_assert(static_cast<quint8>(PartialDisposition::Remove) == 1);
static_assert(std::is_same_v<std::underlying_type_t<AcceptanceOrigin>, quint8>);
static_assert(static_cast<quint8>(AcceptanceOrigin::UserDecision) == 0);
static_assert(static_cast<quint8>(AcceptanceOrigin::TrustedDevicePolicy) == 1);
static_assert(std::is_same_v<std::underlying_type_t<TransferStartError>, quint32>);
static_assert(static_cast<quint32>(TransferStartError::None) == 0);
static_assert(static_cast<quint32>(TransferStartError::WrongThread) == 1);
static_assert(static_cast<quint32>(TransferStartError::InvalidRequest) == 2);
static_assert(static_cast<quint32>(TransferStartError::NotRunning) == 3);
static_assert(static_cast<quint32>(TransferStartError::PeerUnavailable) == 4);

static_assert(std::is_copy_constructible_v<DeviceId>);
static_assert(std::is_copy_constructible_v<DeviceInfo>);
static_assert(std::is_copy_constructible_v<DeviceSnapshot>);
static_assert(std::is_copy_constructible_v<IncomingOffer>);
static_assert(std::is_copy_constructible_v<TransferSnapshot>);
static_assert(std::is_copy_constructible_v<TransferHistoryRecord>);
static_assert(std::is_copy_constructible_v<SendOptions>);
static_assert(std::is_copy_constructible_v<ReceiveOptions>);
static_assert(std::is_copy_constructible_v<TransferCancelOptions>);
static_assert(std::is_copy_constructible_v<TransferStartResult>);
static_assert(std::is_copy_constructible_v<PermissionSnapshot>);
static_assert(std::is_copy_constructible_v<NegotiatedCapabilities>);
static_assert(std::is_copy_constructible_v<FileEndpointAnnouncement>);
static_assert(FileEndpointAnnouncement::disabled().isDisabled());
static_assert(FileEndpointAnnouncement::disabled().isValid());
static_assert(!FileEndpointAnnouncement{.port = 24801}.isValid());
static_assert(FileEndpointAnnouncement{.port = 24801, .fileV1 = true}.isValid());

} // namespace

class SharedInterfaceFreezeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void freezesServiceSignalsAndMetaTypes();
  void queuesSharedValuesAcrossThreadWithoutLoss();
  void publicUiHeadersContainOnlyTypedBusinessIntents();
  void defaultRuntimeCapabilitiesAreHonest();

Q_SIGNALS:
  void sharedValuesReady(
      DeviceId deviceId, TransferId transferId, FileId fileId, IncomingOffer incomingOffer,
      TransferSnapshot transferSnapshot, TransferHistoryRecord historyRecord, SendOptions sendOptions,
      ReceiveOptions receiveOptions, TransferCancelOptions cancelOptions,
      PermissionSnapshot permissionSnapshot
  );
};

void SharedInterfaceFreezeTests::freezesServiceSignalsAndMetaTypes()
{
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::incomingOffer).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferAdded).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferChanged).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferRemoved).isValid());
  QVERIFY(QMetaMethod::fromSignal(&FileTransferRuntime::peerReady).isValid());

  QVERIFY(QMetaType::fromType<DeviceId>().isValid());
  QVERIFY(QMetaType::fromType<DeviceCapabilities>().isValid());
  QVERIFY(QMetaType::fromType<DeviceInfo>().isValid());
  QVERIFY(QMetaType::fromType<DeviceSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<IncomingOffer>().isValid());
  QVERIFY(QMetaType::fromType<TransferSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<TransferHistoryRecord>().isValid());
  QVERIFY(QMetaType::fromType<SendOptions>().isValid());
  QVERIFY(QMetaType::fromType<ReceiveOptions>().isValid());
  QVERIFY(QMetaType::fromType<PartialDisposition>().isValid());
  QVERIFY(QMetaType::fromType<TransferCancelOptions>().isValid());
  QVERIFY(QMetaType::fromType<AcceptanceOrigin>().isValid());
  QVERIFY(QMetaType::fromType<RejectReason>().isValid());
  QVERIFY(QMetaType::fromType<TransferCancelReason>().isValid());
  QVERIFY(QMetaType::fromType<TransferStartError>().isValid());
  QVERIFY(QMetaType::fromType<TransferStartResult>().isValid());
  QVERIFY(QMetaType::fromType<PermissionSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<FileEndpointAnnouncement>().isValid());
  QVERIFY(QMetaType::fromType<NegotiatedCapabilities>().isValid());
}

void SharedInterfaceFreezeTests::queuesSharedValuesAcrossThreadWithoutLoss()
{
  const auto deviceId = DeviceId::generate();
  const auto transferId = TransferId::generate();
  const auto fileId = FileId::generate();
  const IncomingOffer incomingOffer{
      .peerDeviceId = deviceId,
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .offer =
          {
              .transferId = transferId,
              .displayName = QStringLiteral("Project"),
              .totalBytes = 2048,
              .fileCount = 1,
              .manifestSha256 = QByteArray(kSha256Bytes, '\x2a'),
              .manifestPageCount = 1,
              .requestedConflictPolicy = ConflictPolicy::AutoRename,
          },
      .peerTrusted = true,
      .mayAutoAccept = true,
  };
  const auto now = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);
  const TransferSnapshot transferSnapshot{
      .id = transferId,
      .peerId = deviceId,
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Project"),
      .direction = TransferDirection::Receiving,
      .state = TransferState::Transferring,
      .progress = {.completedBytes = 512, .totalBytes = 2048, .totalFiles = 1, .bytesPerSecond = 128.0},
      .currentRelativeDisplayPath = QStringLiteral("Project/report.txt"),
      .canPause = true,
      .canCancel = true,
      .createdUtc = now,
  };
  const TransferHistoryRecord historyRecord{
      .transferId = transferId,
      .peerDeviceId = deviceId,
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Project"),
      .direction = HistoryDirection::Receiving,
      .fileCount = 1,
      .totalBytes = 2048,
      .startedUtc = now,
      .finishedUtc = now.addSecs(16),
      .status = HistoryStatus::Completed,
  };
  const SendOptions sendOptions{.conflictPolicy = ConflictPolicy::Ask};
  const ReceiveOptions receiveOptions{
      .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
      .conflictPolicy = ConflictPolicy::AutoRename,
      .failurePartialDisposition = PartialDisposition::Remove,
      .acceptanceOrigin = AcceptanceOrigin::TrustedDevicePolicy,
  };
  const TransferCancelOptions cancelOptions{
      .reason = TransferCancelReason::ApplicationShutdown,
      .partialDisposition = PartialDisposition::Remove,
  };
  const PermissionSnapshot permissionSnapshot{
      .platform = PermissionPlatform::MacOS,
      .entries = {{PermissionKind::MacLocalNetwork, PermissionState::Granted}},
      .checkedAtUtc = now,
  };
  const NegotiatedCapabilities negotiatedCapabilities{
      .protocolMajorVersion = 1,
      .features = {QStringLiteral("file.v1"), QStringLiteral("sha256")},
      .chunkBytes = 512U * 1024U,
      .maxPayloadBytes = 2U * 1024U * 1024U,
      .maxConcurrentTransfers = 3,
      .maxConcurrentFiles = 4,
      .maxManifestEntries = 12'345,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask},
  };

  QTemporaryDir runtimeDirectory;
  QVERIFY(runtimeDirectory.isValid());
  TrustedDeviceStore trustedDevices(
      runtimeDirectory.filePath(QStringLiteral("trusted-devices.json"))
  );
  model::DeviceHomeModel deviceModel;
  DeviceDiscoveryRuntime discoveryRuntime(
      DeviceInfo{
          .deviceId = DeviceId::generate(),
          .displayName = QStringLiteral("Queued boundary source"),
          .platform = QStringLiteral("windows"),
          .architecture = QStringLiteral("x86_64"),
          .appVersion = QStringLiteral("0.1.2"),
          .inputPort = 24800,
      },
      deviceModel
  );
  FileTransferRuntime runtime(
      discoveryRuntime.service().localDevice().deviceId, trustedDevices, discoveryRuntime, QString{}
  );

  QThread worker;
  QueuedBoundaryReceiver receiver;
  receiver.moveToThread(&worker);
  ScopedReceiverThread threadCleanup(worker, receiver);
  QVERIFY(connect(
      this, &SharedInterfaceFreezeTests::sharedValuesReady, &receiver, &QueuedBoundaryReceiver::receive,
      Qt::QueuedConnection
  ));
  QVERIFY(connect(
      &runtime, &FileTransferRuntime::peerReady, &receiver, &QueuedBoundaryReceiver::receivePeerReady,
      Qt::QueuedConnection
  ));
  QSignalSpy delivered(&receiver, &QueuedBoundaryReceiver::delivered);
  QSignalSpy peerReadyDelivered(&receiver, &QueuedBoundaryReceiver::peerReadyDelivered);
  QVERIFY(delivered.isValid());
  QVERIFY(peerReadyDelivered.isValid());
  worker.start();

  CapturedQtMessages captured;
  {
    ScopedMessageCapture capture(captured);
    Q_EMIT sharedValuesReady(
        deviceId, transferId, fileId, incomingOffer, transferSnapshot, historyRecord, sendOptions,
        receiveOptions, cancelOptions, permissionSnapshot
    );
    Q_EMIT runtime.peerReady(deviceId, negotiatedCapabilities);
    QTRY_COMPARE_WITH_TIMEOUT(delivered.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(peerReadyDelivered.count(), 1, 5000);
  }

  QCOMPARE(receiver.deliveryThread, &worker);
  QCOMPARE(receiver.deviceId, std::optional<DeviceId>{deviceId});
  QCOMPARE(receiver.transferId, std::optional<TransferId>{transferId});
  QCOMPARE(receiver.fileId, std::optional<FileId>{fileId});
  QCOMPARE(receiver.incomingOffer, std::optional<IncomingOffer>{incomingOffer});
  QCOMPARE(receiver.transferSnapshot, std::optional<TransferSnapshot>{transferSnapshot});
  QCOMPARE(receiver.historyRecord, std::optional<TransferHistoryRecord>{historyRecord});
  QCOMPARE(receiver.sendOptions, std::optional<SendOptions>{sendOptions});
  QCOMPARE(receiver.receiveOptions, std::optional<ReceiveOptions>{receiveOptions});
  QCOMPARE(receiver.cancelOptions, std::optional<TransferCancelOptions>{cancelOptions});
  QCOMPARE(receiver.permissionSnapshot, std::optional<PermissionSnapshot>{permissionSnapshot});
  QCOMPARE(receiver.readyPeerDeviceId, std::optional<DeviceId>{deviceId});
  QCOMPARE(
      receiver.negotiatedCapabilities,
      std::optional<NegotiatedCapabilities>{negotiatedCapabilities}
  );
  QCOMPARE(receiver.peerReadyDeliveryThread, &worker);
  const QMutexLocker locker(&captured.mutex);
  for (const auto &message : captured.messages)
    QVERIFY2(!message.contains(QStringLiteral("Cannot queue arguments")), qPrintable(message));
}

void SharedInterfaceFreezeTests::publicUiHeadersContainOnlyTypedBusinessIntents()
{
  const QDir testSource(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
  const QDir relaydeskSource(testSource.absoluteFilePath(QStringLiteral("../../../lib/relaydesk")));
  const QStringList headers{
      QStringLiteral("widgets/DevicesDock.h"),
      QStringLiteral("model/IncomingOfferModel.h"),
      QStringLiteral("model/TransferCenterModel.h"),
      QStringLiteral("app/TransferUiRuntime.h"),
  };
  const QList<QRegularExpression> forbidden{
      QRegularExpression(QStringLiteral("\\bFrame\\b")),
      QRegularExpression(QStringLiteral("\\bTransferAccept\\b")),
      QRegularExpression(QStringLiteral("\\bTransferReject\\b")),
  };

  for (const auto &relativePath : headers) {
    QFile header(relaydeskSource.absoluteFilePath(relativePath));
    QVERIFY2(header.open(QIODevice::ReadOnly), qPrintable(header.errorString()));
    const auto source = QString::fromUtf8(header.readAll());
    for (const auto &pattern : forbidden) {
      QVERIFY2(
          !pattern.match(source).hasMatch(),
          qPrintable(relativePath + QStringLiteral(" exposes forbidden wire type ") + pattern.pattern())
      );
    }
  }
}

void SharedInterfaceFreezeTests::defaultRuntimeCapabilitiesAreHonest()
{
  const FileTransferRuntimeOptions options;
  QCOMPARE(options.localCapabilities.features, QStringList({QStringLiteral("file.v1"), QStringLiteral("sha256")}));
  QCOMPARE(
      options.localCapabilities.conflictPolicies, QList<ConflictPolicy>({ConflictPolicy::AutoRename})
  );
}

QTEST_GUILESS_MAIN(SharedInterfaceFreezeTests)
#include "SharedInterfaceFreezeTests.moc"
