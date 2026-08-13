/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/DeviceDiscoveryRuntime.h"

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/platform/PermissionSnapshot.h"
#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QMetaMethod>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
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
  QThread *deliveryThread = nullptr;

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

Q_SIGNALS:
  void delivered();
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

  QThread worker;
  QueuedBoundaryReceiver receiver;
  receiver.moveToThread(&worker);
  ScopedReceiverThread threadCleanup(worker, receiver);
  QVERIFY(connect(
      this, &SharedInterfaceFreezeTests::sharedValuesReady, &receiver, &QueuedBoundaryReceiver::receive,
      Qt::QueuedConnection
  ));
  QSignalSpy delivered(&receiver, &QueuedBoundaryReceiver::delivered);
  QVERIFY(delivered.isValid());
  worker.start();

  CapturedQtMessages captured;
  {
    ScopedMessageCapture capture(captured);
    Q_EMIT sharedValuesReady(
        deviceId, transferId, fileId, incomingOffer, transferSnapshot, historyRecord, sendOptions,
        receiveOptions, cancelOptions, permissionSnapshot
    );
    QTRY_COMPARE_WITH_TIMEOUT(delivered.count(), 1, 5000);
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
  const QMutexLocker locker(&captured.mutex);
  for (const auto &message : captured.messages)
    QVERIFY2(!message.contains(QStringLiteral("Cannot queue arguments")), qPrintable(message));
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
