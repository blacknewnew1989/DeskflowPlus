/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferRuntimeComposition.h"
#include "relaydesk/app/TransferHistoryRuntime.h"
#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "relaydesk/transfer/TransferRecoveryStore.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"
#include "relaydesk/widgets/TransferMiniBar.h"

#include "../FakePairingService.h"
#include "../TestTlsIdentity.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QScopeGuard>
#include <QToolButton>
#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QProgressBar>
#include <QStringList>
#include <QTimer>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::test;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

class FakeFileTransferService final : public IFileTransferService
{
public:
  TransferStartResult send(const DeviceId &, const QList<QUrl> &, const SendOptions &) override
  {
    ++sendCalls;
    return {.transferId = TransferId::generate()};
  }
  void accept(const TransferId &, const ReceiveOptions &) override
  {
  }
  void reject(const TransferId &, RejectReason) override
  {
  }
  void pause(const TransferId &) override
  {
  }
  void resume(const TransferId &) override
  {
  }
  void cancel(const TransferId &, const TransferCancelOptions &) override
  {
  }
  void retry(const TransferId &) override
  {
  }
  void resolveIncomingConflict(const TransferId &, const QUuid &, IncomingConflictDecision) override
  {
  }
  QList<TransferSnapshot> activeTransfers() const override
  {
    return {};
  }

  int sendCalls = 0;
};

struct Fixture
{
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingService};
  PermissionStatusModel permissions{PermissionPlatform::Other};
  DevicesDock devicesDock{devices, pairing, permissions};
  TransferCenterModel transfers;
  TransferCenterDock transferDock{transfers};
  TransferMiniBar transferMiniBar{transfers};
};

DeviceInfo loopbackDevice(DeviceId id, QByteArray fingerprint, QString name)
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

TrustedDevice loopbackTrustedDevice(DeviceId id, QByteArray fingerprint)
{
  return {
      .deviceId = std::move(id),
      .alias = QStringLiteral("Loopback peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = std::move(fingerprint),
  };
}

bool hasPartFile(const QString &root)
{
  QDirIterator iterator(root, {QStringLiteral("*.part")}, QDir::Files, QDirIterator::Subdirectories);
  return iterator.hasNext();
}

std::optional<qint64> partFileSize(const QString &root)
{
  QDirIterator iterator(root, {QStringLiteral("*.part")}, QDir::Files, QDirIterator::Subdirectories);
  if (!iterator.hasNext())
    return std::nullopt;
  return QFileInfo(iterator.next()).size();
}

} // namespace

class TransferRuntimeCompositionTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void ownsTypedServiceBindingAndLifecycle();
  void reportsUnavailableOrFailedStartupWithoutStopping();
  void loadsAndPersistsHistoryOffTheUiContract();
  void updatesIncomingOfferSettingsForSubsequentOffers();
  void loadsMoreThanOneHundredHistoryRecordsAtStartup();
  void appliesIncomingSettingsAndRefreshesReceiveRootSpaceAsync();
  void rejectedCompletedOpenShowsNonModalFeedbackWithoutChangingHistory();
  void asyncHistoryLoadAndPersistErrorsShowNonModalFeedbackWithoutDiagnostic();
  void productionIncomingOfferButtonsDriveLoopbackTransferAndTypedRejection();
  void productionTransferCenterButtonsPauseResumeAndCancelLoopbackTransfers();
  void productionTransferMiniBarReflectsAndControlsLoopbackTransfer();
  void productionHistoryRetryButtonReoffersChangedSource();
};

void TransferRuntimeCompositionTests::ownsTypedServiceBindingAndLifecycle()
{
  Fixture fixture;
  auto service = std::make_unique<FakeFileTransferService>();
  auto *serviceObserver = service.get();
  int starts = 0;
  int stops = 0;
  {
    TransferRuntimeComposition composition(
        std::move(service),
        {
            .start = [&](QString *) {
              ++starts;
              return true;
            },
            .stop = [&] { ++stops; },
        },
        fixture.devicesDock, fixture.transferDock,
        {
            .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
            .availableBytes = 4096,
        }
    );

    QVERIFY(!composition.isRunning());
    QVERIFY(composition.start());
    QVERIFY(composition.start());
    QVERIFY(composition.isRunning());
    QCOMPARE(starts, 1);

    Q_EMIT fixture.devicesDock.sendItemsRequested(
        DeviceId::generate(), {QUrl::fromLocalFile(QStringLiteral("C:/source/file.txt"))}, {}
    );
    QCOMPARE(serviceObserver->sendCalls, 1);
  }
  QCOMPARE(stops, 1);
}

void TransferRuntimeCompositionTests::reportsUnavailableOrFailedStartupWithoutStopping()
{
  Fixture fixture;
  int stops = 0;
  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {
          .start = [](QString *diagnostic) {
            if (diagnostic != nullptr) {
              *diagnostic = QStringLiteral("expected startup failure");
            }
            return false;
          },
          .stop = [&] { ++stops; },
      },
      fixture.devicesDock, fixture.transferDock,
      {
          .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
          .availableBytes = 4096,
      }
  );

  QString diagnostic;
  QVERIFY(!composition.start(&diagnostic));
  QCOMPARE(diagnostic, QStringLiteral("expected startup failure"));
  QVERIFY(!composition.isRunning());
  composition.stop();
  QCOMPARE(stops, 0);
}

void TransferRuntimeCompositionTests::loadsAndPersistsHistoryOffTheUiContract()
{
  Fixture fixture;
  auto service = std::make_unique<FakeFileTransferService>();
  auto *serviceObserver = service.get();
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto historyPath = temporary.filePath(QStringLiteral("history/transfers.jsonl"));
  TransferRuntimeComposition composition(
      std::move(service),
      {
          .start = [](QString *) { return true; },
          .stop = [] {},
      },
      fixture.devicesDock, fixture.transferDock,
      {
          .destinationRoot = temporary.path(),
          .availableBytes = 0,
      },
      historyPath
  );
  QVERIFY(composition.start());
  auto *historyRuntime = composition.findChild<TransferHistoryRuntime *>();
  QVERIFY(historyRuntime != nullptr);
  QString historyError;
  connect(
      historyRuntime, &TransferHistoryRuntime::historyError, this,
      [&](TransferHistoryError, const QString &diagnostic) { historyError = diagnostic; }
  );
  QTRY_VERIFY_WITH_TIMEOUT(composition.incomingOffers().settings().availableBytes > 0, 5000);

  const auto now = QDateTime::currentDateTimeUtc();
  const TransferSnapshot completed{
      .id = TransferId::generate(),
      .peerId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Project"),
      .direction = TransferDirection::Receiving,
      .state = TransferState::Completed,
      .progress = {.completedBytes = 1024, .totalBytes = 1024, .completedFiles = 1, .totalFiles = 1},
      .currentRelativeDisplayPath = QStringLiteral("Project/report.txt"),
      .createdUtc = now.addSecs(-2),
      .finishedUtc = now,
  };
  QVERIFY(TransferHistoryRuntime::recordForSnapshot(completed).has_value());
  QSignalSpy serviceChanges(serviceObserver, &IFileTransferService::transferChanged);
  Q_EMIT serviceObserver->transferChanged(completed);
  QCOMPARE(serviceChanges.count(), 1);
  QTRY_VERIFY_WITH_TIMEOUT(
      fixture.transfers.historyRecord(completed.id).has_value() || !historyError.isEmpty(), 5000
  );
  QVERIFY2(historyError.isEmpty(), qPrintable(historyError));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(historyPath), 5000);

  const TransferHistoryStore store(historyPath);
  const auto page = store.page();
  QVERIFY2(page.ok(), qPrintable(page.diagnostic));
  QCOMPARE(page.page.records.size(), 1);
  QCOMPARE(page.page.records.first().transferId, completed.id);
  QCOMPARE(page.page.records.first().completedRelativePath, QStringLiteral("Project/report.txt"));
  QCOMPARE(page.page.records.first().topLevelTargetRelativePath, QStringLiteral("Project"));
}

void TransferRuntimeCompositionTests::updatesIncomingOfferSettingsForSubsequentOffers()
{
  Fixture fixture;
  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {.start = [](QString *) { return true; }, .stop = [] {}}, fixture.devicesDock, fixture.transferDock,
      {
          .destinationRoot = QStringLiteral("C:/RelayDesk"),
          .availableBytes = 4096,
          .autoAcceptTrustedDevices = false,
          .defaultConflictPolicy = ConflictPolicy::AutoRename,
      }
  );

  composition.setIncomingOfferSettings(
      {
          .destinationRoot = QStringLiteral("D:/Incoming"),
          .availableBytes = 4096,
          .autoAcceptTrustedDevices = true,
          .defaultConflictPolicy = ConflictPolicy::Ask,
      }
  );
  const auto settings = composition.incomingOffers().settings();
  QCOMPARE(settings.destinationRoot, QStringLiteral("D:/Incoming"));
  QVERIFY(settings.autoAcceptTrustedDevices);
  QCOMPARE(settings.defaultConflictPolicy, ConflictPolicy::Ask);
}

void TransferRuntimeCompositionTests::loadsMoreThanOneHundredHistoryRecordsAtStartup()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto historyPath = temporary.filePath(QStringLiteral("history/transfers.jsonl"));
  TransferHistoryStore store(historyPath);
  const auto now = QDateTime::currentDateTimeUtc();
  for (int index = 0; index < 101; ++index) {
    const TransferSnapshot completed{
        .id = TransferId::generate(),
        .peerId = DeviceId::generate(),
        .peerDisplayName = QStringLiteral("Studio Mac"),
        .displayName = QStringLiteral("Project"),
        .direction = TransferDirection::Receiving,
        .state = TransferState::Completed,
        .progress = {.completedBytes = 1024, .totalBytes = 1024, .completedFiles = 1, .totalFiles = 1},
        .createdUtc = now.addSecs(-index - 1),
        .finishedUtc = now.addSecs(-index),
    };
    const auto record = TransferHistoryRuntime::recordForSnapshot(completed);
    QVERIFY(record.has_value());
    QVERIFY(store.append(*record).ok());
  }

  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {.start = [](QString *) { return true; }, .stop = [] {}}, fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = temporary.path(), .availableBytes = 0}, historyPath
  );
  QVERIFY(composition.start());
  QTRY_COMPARE_WITH_TIMEOUT(fixture.transfers.rowCount(), 101, 5000);
}

void TransferRuntimeCompositionTests::appliesIncomingSettingsAndRefreshesReceiveRootSpaceAsync()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto historyPath = temporary.filePath(QStringLiteral("history/transfers.jsonl"));
  const auto updatedRoot = temporary.filePath(QStringLiteral("updated-receive-root"));
  QVERIFY(QDir().mkpath(updatedRoot));
  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {.start = [](QString *) { return true; }, .stop = [] {}}, fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = QStringLiteral("X:/stale-receive-root"), .availableBytes = 1}, historyPath
  );
  QVERIFY(composition.start());

  composition.setIncomingOfferSettings({
      .destinationRoot = updatedRoot,
      .availableBytes = 0,
      .autoAcceptTrustedDevices = true,
      .defaultConflictPolicy = ConflictPolicy::Ask,
  });
  QCOMPARE(composition.incomingOffers().settings().destinationRoot, updatedRoot);
  QCOMPARE(composition.incomingOffers().settings().defaultConflictPolicy, ConflictPolicy::Ask);
  QTRY_VERIFY_WITH_TIMEOUT(composition.incomingOffers().settings().availableBytes > 0, 5000);
  QCOMPARE(composition.incomingOffers().settings().destinationRoot, updatedRoot);
}

void TransferRuntimeCompositionTests::rejectedCompletedOpenShowsNonModalFeedbackWithoutChangingHistory()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto receiveRoot = temporary.filePath(QStringLiteral("receive"));
  const auto completedDirectory = QDir(receiveRoot).filePath(QStringLiteral("Archive"));
  QVERIFY(QDir().mkpath(completedDirectory));
  const auto completedPath = QDir(completedDirectory).filePath(QStringLiteral("report.txt"));
  QFile completedFile(completedPath);
  QVERIFY(completedFile.open(QIODevice::WriteOnly));
  QCOMPARE(completedFile.write("complete"), 8);

  const TransferHistoryRecord record{
      .transferId = TransferId::generate(),
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Archive"),
      .direction = HistoryDirection::Receiving,
      .fileCount = 1,
      .totalBytes = 8,
      .startedUtc = QDateTime::currentDateTimeUtc().addSecs(-1),
      .finishedUtc = QDateTime::currentDateTimeUtc(),
      .status = HistoryStatus::Completed,
      .completedRelativePath = QStringLiteral("Archive/report.txt"),
      .topLevelTargetRelativePath = QStringLiteral("Archive"),
  };
  fixture.transfers.setHistoryRecords({record});

  bool rejectOpen = true;
  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {.start = [](QString *) { return true; }, .stop = [] {}}, fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 0}, {},
      [receiveRoot, completedPath](const TransferHistoryRecord &) {
        return std::optional<ResolvedTransferCompletion>{{receiveRoot, completedPath}};
      },
      [&rejectOpen](const QUrl &) { return !rejectOpen; }
  );

  auto *feedback = fixture.transferDock.findChild<QLabel *>(QStringLiteral("relaydeskTransferFeedback"));
  auto *uiRuntime = composition.findChild<TransferUiRuntime *>();
  fixture.transferDock.show();
  auto *list = fixture.transferDock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *openFile = fixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferOpenFileButton"));
  QVERIFY(feedback != nullptr);
  QVERIFY(uiRuntime != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(openFile != nullptr);
  list->setCurrentIndex(fixture.transfers.index(0, 0));
  QTRY_VERIFY(openFile->isVisible());
  const auto selectedBeforeOpen = list->currentIndex();
  QSignalSpy opened(uiRuntime, &TransferUiRuntime::completionOpened);
  QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
  QVERIFY(fixture.transfers.requestOpenFolder(record.transferId));
  QCOMPARE(feedback->text(), QStringLiteral("Could not open the completed item. Try again."));
  QVERIFY(feedback->isVisible());
  QVERIFY(!feedback->text().contains(receiveRoot));
  QVERIFY(!feedback->text().contains(completedPath));
  QCOMPARE(fixture.transfers.historyRecord(record.transferId), std::optional<TransferHistoryRecord>{record});
  QCOMPARE(list->currentIndex(), selectedBeforeOpen);
  QVERIFY(openFile->isVisible());

  rejectOpen = false;
  QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
  QCOMPARE(opened.count(), 1);
  QVERIFY(!feedback->isVisible());
  QCOMPARE(list->currentIndex(), selectedBeforeOpen);
  QVERIFY(openFile->isVisible());
  QCOMPARE(fixture.transfers.historyRecord(record.transferId), std::optional<TransferHistoryRecord>{record});
}

void TransferRuntimeCompositionTests::asyncHistoryLoadAndPersistErrorsShowNonModalFeedbackWithoutDiagnostic()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto receiveRoot = temporary.filePath(QStringLiteral("receive"));
  const auto completedDirectory = QDir(receiveRoot).filePath(QStringLiteral("Archive"));
  QVERIFY(QDir().mkpath(completedDirectory));
  const auto completedPath = QDir(completedDirectory).filePath(QStringLiteral("report.txt"));
  QFile completedFile(completedPath);
  QVERIFY(completedFile.open(QIODevice::WriteOnly));
  QCOMPARE(completedFile.write("complete"), 8);
  auto service = std::make_unique<FakeFileTransferService>();
  auto *serviceObserver = service.get();
  bool rejectOpen = true;

  TransferRuntimeComposition composition(
      std::move(service),
      {.start = [](QString *) { return true; }, .stop = [] {}}, fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 0}, temporary.path(),
      [receiveRoot, completedPath](const TransferHistoryRecord &) {
        return std::optional<ResolvedTransferCompletion>{{receiveRoot, completedPath}};
      },
      [&rejectOpen](const QUrl &) { return !rejectOpen; }
  );
  auto *historyRuntime = composition.findChild<TransferHistoryRuntime *>();
  QVERIFY(historyRuntime != nullptr);
  QSignalSpy historyErrors(historyRuntime, &TransferHistoryRuntime::historyError);
  auto *feedback = fixture.transferDock.findChild<QLabel *>(QStringLiteral("relaydeskTransferFeedback"));
  QVERIFY(feedback != nullptr);
  fixture.transferDock.show();

  QVERIFY(composition.start());
  QTRY_VERIFY_WITH_TIMEOUT(historyErrors.count() > 0, 5000);
  Q_EMIT serviceObserver->transferChanged({
      .id = TransferId::generate(),
      .peerId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Archive"),
      .direction = TransferDirection::Receiving,
      .state = TransferState::Completed,
      .progress = {.completedBytes = 8, .totalBytes = 8, .completedFiles = 1, .totalFiles = 1},
      .currentRelativeDisplayPath = QStringLiteral("Archive/report.txt"),
      .createdUtc = QDateTime::currentDateTimeUtc().addSecs(-1),
      .finishedUtc = QDateTime::currentDateTimeUtc(),
  });
  QTRY_COMPARE_WITH_TIMEOUT(historyErrors.count(), 2, 5000);
  QCOMPARE(feedback->text(), QStringLiteral("Transfer history could not be updated. Try again."));
  QVERIFY(feedback->isVisible());
  QVERIFY(!feedback->text().contains(temporary.path()));

  const TransferHistoryRecord record{
      .transferId = TransferId::generate(),
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Archive"),
      .direction = HistoryDirection::Receiving,
      .fileCount = 1,
      .totalBytes = 8,
      .startedUtc = QDateTime::currentDateTimeUtc().addSecs(-1),
      .finishedUtc = QDateTime::currentDateTimeUtc(),
      .status = HistoryStatus::Completed,
      .completedRelativePath = QStringLiteral("Archive/report.txt"),
      .topLevelTargetRelativePath = QStringLiteral("Archive"),
  };
  fixture.transfers.setHistoryRecords({record});
  auto *list = fixture.transferDock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *openFile = fixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferOpenFileButton"));
  QVERIFY(list != nullptr);
  QVERIFY(openFile != nullptr);
  list->setCurrentIndex(fixture.transfers.index(0, 0));
  QTRY_VERIFY(openFile->isVisible());
  const auto selectedBeforeOpen = list->currentIndex();

  QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
  QCOMPARE(feedback->text(), QStringLiteral("Could not open the completed item. Try again."));
  QVERIFY(feedback->isVisible());
  rejectOpen = false;
  QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
  QCOMPARE(feedback->text(), QStringLiteral("Transfer history could not be updated. Try again."));
  QVERIFY(feedback->isVisible());
  QCOMPARE(list->currentIndex(), selectedBeforeOpen);
  QVERIFY(openFile->isVisible());
  QCOMPARE(fixture.transfers.historyRecord(record.transferId), std::optional<TransferHistoryRecord>{record});
}

void TransferRuntimeCompositionTests::productionIncomingOfferButtonsDriveLoopbackTransferAndTypedRejection()
{
  QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/relaydesk-r4-incoming-offer-XXXXXX"));
  QVERIFY2(temporary.isValid(), qPrintable(temporary.errorString()));
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(temporary);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const auto receiveRoot = temporary.filePath(QStringLiteral("received"));
  QVERIFY(QDir().mkpath(receiveRoot));
  const auto acceptedSourcePath = temporary.filePath(QStringLiteral("accepted-source.bin"));
  const auto rejectedSourcePath = temporary.filePath(QStringLiteral("rejected-source.bin"));
  const QByteArray acceptedContents(1'048'579, '\x5a');
  const QByteArray rejectedContents("this file must not be received");
  for (const auto &[path, contents] : {std::pair{acceptedSourcePath, acceptedContents},
                                       std::pair{rejectedSourcePath, rejectedContents}}) {
    QFile source(path);
    QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(source.errorString()));
    QCOMPARE(source.write(contents), static_cast<qint64>(contents.size()));
  }

  Fixture fixture;
  fixture.devicesDock.resize(480, 720);
  fixture.devicesDock.show();
  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(temporary.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(temporary.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(loopbackTrustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(loopbackTrustedDevice(senderId, identity.fingerprintSha256)));
  DeviceHomeModel senderDevices;
  DeviceDiscoveryRuntime senderDiscovery(
      loopbackDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderDevices
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      loopbackDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), fixture.devices
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, options
  );
  auto *receiverRuntime = receiver.get();
  TransferRuntimeComposition composition(
      std::move(receiver),
      {
          .start = [receiverRuntime](QString *diagnostic) { return receiverRuntime->start(diagnostic); },
          .stop = [receiverRuntime] { receiverRuntime->stop(); },
      },
      fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 20U * 1024U * 1024U}
  );

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(composition.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  auto *panel = fixture.devicesDock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *accept = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *reject = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskRejectIncomingOfferButton"));
  QVERIFY(panel != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(reject != nullptr);
  std::optional<TransferSnapshot> senderAccepted;
  connect(&sender, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending)
      senderAccepted = snapshot;
  });

  const auto acceptedStart = sender.send(receiverId, {QUrl::fromLocalFile(acceptedSourcePath)}, {});
  QVERIFY2(acceptedStart.ok(), qPrintable(acceptedStart.diagnostic));
  QVERIFY(acceptedStart.transferId.has_value());
  QTRY_VERIFY_WITH_TIMEOUT(panel->isVisible() && accept->isVisible() && accept->isEnabled(), 10'000);
  QCOMPARE(composition.incomingOffers().status(), IncomingOfferModel::Status::AwaitingDecision);
  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderAccepted.has_value() && senderAccepted->id == *acceptedStart.transferId &&
          senderAccepted->state == TransferState::Completed,
      20'000
  );
  const auto acceptedTargetPath = QDir(receiveRoot).filePath(QStringLiteral("accepted-source.bin"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(acceptedTargetPath), 10'000);
  QFile acceptedTarget(acceptedTargetPath);
  QVERIFY(acceptedTarget.open(QIODevice::ReadOnly));
  QCOMPARE(
      QCryptographicHash::hash(acceptedTarget.readAll(), QCryptographicHash::Sha256),
      QCryptographicHash::hash(acceptedContents, QCryptographicHash::Sha256)
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      fixture.transfers.snapshot(*acceptedStart.transferId).has_value() &&
          fixture.transfers.snapshot(*acceptedStart.transferId)->state == TransferState::Completed,
      10'000
  );
  fixture.transferDock.show();
  auto *transferList = fixture.transferDock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  QVERIFY(transferList != nullptr);
  const auto acceptedRow = fixture.transfers.indexOf(*acceptedStart.transferId);
  QVERIFY(acceptedRow >= 0);
  const auto acceptedIndex = fixture.transfers.index(acceptedRow, 0);
  QVERIFY(acceptedIndex.isValid());
  transferList->setCurrentIndex(acceptedIndex);
  const auto acceptedSnapshot = fixture.transfers.snapshot(*acceptedStart.transferId);
  QVERIFY(acceptedSnapshot.has_value());
  QCOMPARE(acceptedSnapshot->state, TransferState::Completed);
  const auto rowCountBeforeReject = fixture.transfers.rowCount();
  const auto selectedBeforeReject = transferList->currentIndex();
  QVERIFY(!panel->isVisible());
  QVERIFY(!hasPartFile(receiveRoot));

  std::optional<TransferSnapshot> senderRejected;
  const auto rejectedStart = sender.send(receiverId, {QUrl::fromLocalFile(rejectedSourcePath)}, {});
  QVERIFY2(rejectedStart.ok(), qPrintable(rejectedStart.diagnostic));
  QVERIFY(rejectedStart.transferId.has_value());
  QTRY_VERIFY_WITH_TIMEOUT(panel->isVisible() && reject->isVisible(), 10'000);
  QTest::mouseClick(reject, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderAccepted.has_value() && senderAccepted->id == *rejectedStart.transferId &&
          senderAccepted->state == TransferState::Rejected,
      10'000
  );
  senderRejected = senderAccepted;
  QCOMPARE(senderRejected->state, TransferState::Rejected);
  QVERIFY(!QFileInfo::exists(QDir(receiveRoot).filePath(QStringLiteral("rejected-source.bin"))));
  QVERIFY(!hasPartFile(receiveRoot));
  QVERIFY(!panel->isVisible());
  QVERIFY(!fixture.transfers.snapshot(*rejectedStart.transferId).has_value());
  QCOMPARE(fixture.transfers.snapshot(*acceptedStart.transferId), acceptedSnapshot);
  QCOMPARE(fixture.transfers.rowCount(), rowCountBeforeReject);
  QCOMPARE(transferList->currentIndex(), selectedBeforeReject);

  composition.stop();
  sender.stop();
}

void TransferRuntimeCompositionTests::productionTransferMiniBarReflectsAndControlsLoopbackTransfer()
{
  QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/relaydesk-r4-mini-bar-XXXXXX"));
  QVERIFY2(temporary.isValid(), qPrintable(temporary.errorString()));
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(temporary);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const QByteArray sourceBytes(32 * 1024 * 1024 + 37, '\x5a');
  const auto sourcePath = temporary.filePath(QStringLiteral("mini-bar-source.bin"));
  QFile source(sourcePath);
  QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(source.errorString()));
  QCOMPARE(source.write(sourceBytes), static_cast<qint64>(sourceBytes.size()));
  source.close();
  const auto receiveRoot = temporary.filePath(QStringLiteral("received"));
  QVERIFY(QDir().mkpath(receiveRoot));

  Fixture fixture;
  fixture.devicesDock.resize(480, 720);
  fixture.devicesDock.show();
  fixture.transferMiniBar.resize(560, 52);
  QVERIFY(fixture.transferMiniBar.isHidden());
  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(temporary.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(temporary.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(loopbackTrustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(loopbackTrustedDevice(senderId, identity.fingerprintSha256)));
  DeviceHomeModel senderDevices;
  DeviceDiscoveryRuntime senderDiscovery(
      loopbackDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderDevices
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      loopbackDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), fixture.devices
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.tlsSettings.maxQueuedWriteBytes = 64U * 1024U;
  options.localCapabilities.preferredChunkBytes = 4U * 1024U;
  options.localCapabilities.maxPayloadBytes = 16U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, options
  );
  auto *receiverRuntime = receiver.get();
  TransferRuntimeComposition composition(
      std::move(receiver),
      {
          .start = [receiverRuntime](QString *diagnostic) { return receiverRuntime->start(diagnostic); },
          .stop = [receiverRuntime] { receiverRuntime->stop(); },
      },
      fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 128U * 1024U * 1024U}
  );

  auto *offerPanel = fixture.devicesDock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *accept = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *title = fixture.transferMiniBar.findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarTitle"));
  auto *metrics = fixture.transferMiniBar.findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarMetrics"));
  auto *progress = fixture.transferMiniBar.findChild<QProgressBar *>(QStringLiteral("relaydeskTransferMiniBarProgress"));
  auto *primary =
      fixture.transferMiniBar.findChild<QPushButton *>(QStringLiteral("relaydeskTransferMiniBarPrimaryAction"));
  QVERIFY(offerPanel != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(title != nullptr);
  QVERIFY(metrics != nullptr);
  QVERIFY(progress != nullptr);
  QVERIFY(primary != nullptr);

  std::optional<TransferSnapshot> senderSnapshot;
  std::optional<TransferSnapshot> receiverSnapshot;
  QStringList errors;
  QObject connectionContext;
  connect(&sender, &IFileTransferService::transferChanged, &connectionContext, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction == TransferDirection::Sending)
      senderSnapshot = snapshot;
  });
  connect(
      receiverRuntime, &IFileTransferService::transferChanged, &connectionContext,
      [&](const TransferSnapshot &snapshot) {
        if (snapshot.direction == TransferDirection::Receiving)
          receiverSnapshot = snapshot;
      }
  );
  connect(&sender, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(receiverRuntime, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });

  QString diagnostic;
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(composition.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  const auto started = sender.send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(started.ok(), qPrintable(started.diagnostic));
  QVERIFY(started.transferId.has_value());
  QTRY_VERIFY_WITH_TIMEOUT(offerPanel->isVisible() && accept->isVisible() && accept->isEnabled(), 10'000);
  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      fixture.transfers.snapshot(*started.transferId).has_value() && fixture.transferMiniBar.isVisible() &&
          progress->value() > 0 && primary->isVisible() && primary->isEnabled() &&
          primary->text() == QStringLiteral("Pause"),
      20'000
  );
  const auto miniBarMatchesModel = [&] {
    const auto row = fixture.transfers.indexOf(*started.transferId);
    if (row < 0)
      return false;
    const auto index = fixture.transfers.index(row, 0);
    QStringList expectedMetrics{index.data(TransferCenterModel::ProgressTextRole).toString()};
    const auto speed = index.data(TransferCenterModel::SpeedTextRole).toString();
    expectedMetrics.append(
        speed.isEmpty() ? index.data(TransferCenterModel::StateTextRole).toString() : speed
    );
    expectedMetrics.removeAll({});
    return title->text() == index.data(TransferCenterModel::DisplayNameRole).toString() &&
           progress->value() == index.data(TransferCenterModel::ProgressPercentRole).toInt() &&
           metrics->text() == expectedMetrics.join(QStringLiteral(" · "));
  };
  QTRY_VERIFY_WITH_TIMEOUT(miniBarMatchesModel(), 5'000);

  QSignalSpy details(&fixture.transferMiniBar, &TransferMiniBar::detailsRequested);
  QTest::mouseClick(&fixture.transferMiniBar, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
  fixture.transferMiniBar.setFocus();
  QTest::keyClick(&fixture.transferMiniBar, Qt::Key_Return);
  QCOMPARE(details.count(), 2);

  QTest::mouseClick(primary, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderSnapshot.has_value() && receiverSnapshot.has_value() && senderSnapshot->state == TransferState::Paused &&
          receiverSnapshot->state == TransferState::Paused && primary->text() == QStringLiteral("Resume"),
      15'000
  );
  const auto senderPausedBytes = senderSnapshot->progress.completedBytes;
  const auto receiverPausedBytes = receiverSnapshot->progress.completedBytes;
  QVERIFY(senderPausedBytes > 0);
  QVERIFY(receiverPausedBytes > 0);
  QTest::qWait(400);
  QCOMPARE(senderSnapshot->progress.completedBytes, senderPausedBytes);
  QCOMPARE(receiverSnapshot->progress.completedBytes, receiverPausedBytes);

  QTest::mouseClick(primary, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderSnapshot.has_value() && receiverSnapshot.has_value() && senderSnapshot->state == TransferState::Completed &&
          receiverSnapshot->state == TransferState::Completed,
      90'000
  );
  const auto targetPath = QDir(receiveRoot).filePath(QStringLiteral("mini-bar-source.bin"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(targetPath), 10'000);
  QFile target(targetPath);
  QVERIFY(target.open(QIODevice::ReadOnly));
  QCOMPARE(
      QCryptographicHash::hash(target.readAll(), QCryptographicHash::Sha256),
      QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256)
  );
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

  composition.stop();
  sender.stop();
}

void TransferRuntimeCompositionTests::productionTransferCenterButtonsPauseResumeAndCancelLoopbackTransfers()
{
  QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/relaydesk-r4-controls-XXXXXX"));
  QVERIFY2(temporary.isValid(), qPrintable(temporary.errorString()));
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(temporary);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const QByteArray sourceBytes(32 * 1024 * 1024 + 37, '\x5a');
  const auto pausedSourcePath = temporary.filePath(QStringLiteral("paused-source.bin"));
  const auto cancelledSourcePath = temporary.filePath(QStringLiteral("cancelled-source.bin"));
  for (const auto &path : {pausedSourcePath, cancelledSourcePath}) {
    QFile source(path);
    QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(source.errorString()));
    QCOMPARE(source.write(sourceBytes), static_cast<qint64>(sourceBytes.size()));
  }
  const auto receiveRoot = temporary.filePath(QStringLiteral("received"));
  const auto recoveryRoot = temporary.filePath(QStringLiteral("recovery"));
  QVERIFY(QDir().mkpath(receiveRoot));

  Fixture fixture;
  fixture.devicesDock.resize(480, 720);
  fixture.devicesDock.show();
  fixture.transferDock.resize(560, 480);
  fixture.transferDock.show();
  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(temporary.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(temporary.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(loopbackTrustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(loopbackTrustedDevice(senderId, identity.fingerprintSha256)));
  DeviceHomeModel senderDevices;
  DeviceDiscoveryRuntime senderDiscovery(
      loopbackDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderDevices
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      loopbackDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), fixture.devices
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = recoveryRoot;
  options.tlsSettings.maxQueuedWriteBytes = 64U * 1024U;
  options.localCapabilities.preferredChunkBytes = 4U * 1024U;
  options.localCapabilities.maxPayloadBytes = 16U * 1024U;
  FileTransferRuntime sender(senderId, senderTrust, senderDiscovery, identityPath, options);
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, options
  );
  auto *receiverRuntime = receiver.get();
  TransferRuntimeComposition composition(
      std::move(receiver),
      {
          .start = [receiverRuntime](QString *diagnostic) { return receiverRuntime->start(diagnostic); },
          .stop = [receiverRuntime] { receiverRuntime->stop(); },
      },
      fixture.devicesDock, fixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 128U * 1024U * 1024U}
  );
  TransferRecoveryStore recoveryStore(recoveryRoot);

  auto *offerPanel = fixture.devicesDock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *accept = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *transferList = fixture.transferDock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *pause = fixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferPauseButton"));
  auto *resume = fixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferResumeButton"));
  auto *more = fixture.transferDock.findChild<QToolButton *>(QStringLiteral("relaydeskTransferMoreButton"));
  auto *cancel = fixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferCancelButton"));
  auto *cancelAction = fixture.transferDock.findChild<QAction *>(QStringLiteral("relaydeskTransferCancelMenuAction"));
  QVERIFY(offerPanel != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(transferList != nullptr);
  QVERIFY(pause != nullptr);
  QVERIFY(resume != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(cancel != nullptr);
  QVERIFY(cancelAction != nullptr);

  std::optional<TransferSnapshot> senderPauseSnapshot;
  std::optional<TransferSnapshot> receiverPauseSnapshot;
  std::optional<TransferSnapshot> senderCancelSnapshot;
  std::optional<TransferSnapshot> receiverCancelSnapshot;
  QList<TransferState> receiverCancelStates;
  QStringList errors;
  std::optional<TransferId> pausedTransfer;
  std::optional<TransferId> cancelledTransfer;
  bool pauseClickQueued = false;
  bool pauseClicked = false;
  bool cancelClickQueued = false;
  bool cancelClicked = false;
  QString stage = QStringLiteral("setup");
  QObject connectionContext;
  const auto stopOnFailure = qScopeGuard([&] {
    if (!QTest::currentTestFailed()) {
      return;
    }
    qWarning().noquote() << QStringLiteral("controls-stage=%1 errors=%2")
                                .arg(stage, errors.join(QStringLiteral("; ")));
    composition.stop();
    sender.stop();
  });
  const auto advanceStage = [&](QStringView next) {
    stage = next.toString();
    qInfo().noquote() << QStringLiteral("controls-stage=%1").arg(stage);
  };
  connect(&sender, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("sender: ") + message);
  });
  connect(receiverRuntime, &FileTransferRuntime::errorOccurred, &connectionContext, [&](auto, auto, const QString &message) {
    errors.append(QStringLiteral("receiver: ") + message);
  });

  connect(&sender, &IFileTransferService::transferChanged, &connectionContext, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending)
      return;
    if (pausedTransfer.has_value() && snapshot.id == *pausedTransfer)
      senderPauseSnapshot = snapshot;
    if (cancelledTransfer.has_value() && snapshot.id == *cancelledTransfer)
      senderCancelSnapshot = snapshot;
  });
  connect(receiverRuntime, &IFileTransferService::transferChanged, &connectionContext, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Receiving)
      return;
    if (pausedTransfer.has_value() && snapshot.id == *pausedTransfer)
      receiverPauseSnapshot = snapshot;
    if (cancelledTransfer.has_value() && snapshot.id == *cancelledTransfer) {
      receiverCancelSnapshot = snapshot;
      receiverCancelStates.append(snapshot.state);
    }
  });
  const auto snapshotEvidence = [&](const std::optional<TransferSnapshot> &runtime, const TransferId &transferId) {
    const auto model = fixture.transfers.snapshot(transferId);
    const auto describe = [](const std::optional<TransferSnapshot> &snapshot) {
      return snapshot.has_value()
                 ? QStringLiteral("state=%1 bytes=%2/%3")
                       .arg(static_cast<int>(snapshot->state))
                       .arg(snapshot->progress.completedBytes)
                       .arg(snapshot->progress.totalBytes)
                 : QStringLiteral("none");
    };
    const auto part = partFileSize(receiveRoot);
    return QStringLiteral("runtime=[%1] model=[%2] part=%3")
        .arg(describe(runtime), describe(model), part.has_value() ? QString::number(*part) : QStringLiteral("none"));
  };
  const auto queuePauseClick = [&] {
    if (!pausedTransfer.has_value() || pauseClickQueued) {
      return;
    }
    const auto snapshot = fixture.transfers.snapshot(*pausedTransfer);
    if (!snapshot.has_value() || snapshot->state != TransferState::Transferring || !snapshot->canPause) {
      return;
    }
    const auto row = fixture.transfers.indexOf(*pausedTransfer);
    if (row < 0) {
      return;
    }
    pauseClickQueued = true;
    transferList->setCurrentIndex(fixture.transfers.index(row, 0));
    QTimer::singleShot(0, &connectionContext, [&] {
      const auto current = fixture.transfers.snapshot(*pausedTransfer);
      if (current.has_value() && current->state == TransferState::Transferring && current->canPause &&
          pause->isVisible() && pause->isEnabled()) {
        advanceStage(u"pause-click");
        pauseClicked = true;
        QTest::mouseClick(pause, Qt::LeftButton);
      }
    });
  };
  const auto queueCancelClick = [&] {
    if (!cancelledTransfer.has_value() || cancelClickQueued) {
      return;
    }
    const auto snapshot = fixture.transfers.snapshot(*cancelledTransfer);
    if (!snapshot.has_value() || snapshot->state != TransferState::Transferring || !snapshot->canCancel) {
      return;
    }
    const auto row = fixture.transfers.indexOf(*cancelledTransfer);
    if (row < 0) {
      return;
    }
    cancelClickQueued = true;
    transferList->setCurrentIndex(fixture.transfers.index(row, 0));
    QTimer::singleShot(0, &connectionContext, [&] {
      const auto current = fixture.transfers.snapshot(*cancelledTransfer);
      if (!current.has_value() || current->state != TransferState::Transferring || !current->canCancel ||
          !more->isVisible() || !cancelAction->isVisible() || !cancelAction->isEnabled()) {
        return;
      }
      auto *menu = more->menu();
      if (menu == nullptr) {
        return;
      }
      QTimer::singleShot(0, &connectionContext, [&, menu] {
        if (!menu->isVisible()) {
          return;
        }
        const auto actionRect = menu->actionGeometry(cancelAction);
        if (!actionRect.isValid()) {
          return;
        }
        advanceStage(u"cancel-click");
        cancelClicked = true;
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, actionRect.center());
      });
      QTest::mouseClick(more, Qt::LeftButton);
    });
  };
  connect(
      &fixture.transfers, &QAbstractItemModel::rowsInserted, &connectionContext,
      [&](const QModelIndex &, int, int) {
        queuePauseClick();
        queueCancelClick();
      }
  );
  connect(
      &fixture.transfers, &QAbstractItemModel::dataChanged, &connectionContext,
      [&](const QModelIndex &, const QModelIndex &, const QList<int> &) {
        queuePauseClick();
        queueCancelClick();
      }
  );

  QString diagnostic;
  advanceStage(u"start");
  QVERIFY2(sender.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(composition.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  const auto pausedStart = sender.send(receiverId, {QUrl::fromLocalFile(pausedSourcePath)}, {});
  QVERIFY2(pausedStart.ok(), qPrintable(pausedStart.diagnostic));
  QVERIFY(pausedStart.transferId.has_value());
  pausedTransfer = *pausedStart.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(offerPanel->isVisible() && accept->isVisible() && accept->isEnabled(), 10'000);
  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY2_WITH_TIMEOUT(
      pauseClicked,
      qPrintable(snapshotEvidence(receiverPauseSnapshot, *pausedTransfer)),
      20'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      senderPauseSnapshot.has_value() && receiverPauseSnapshot.has_value() &&
          senderPauseSnapshot->state == TransferState::Paused && receiverPauseSnapshot->state == TransferState::Paused,
      15'000
  );
  advanceStage(u"paused");
  const auto pausedSenderBytes = senderPauseSnapshot->progress.completedBytes;
  const auto pausedReceiverBytes = receiverPauseSnapshot->progress.completedBytes;
  const auto pausedPartSize = partFileSize(receiveRoot);
  QVERIFY(pausedPartSize.has_value());
  QTest::qWait(400);
  QCOMPARE(senderPauseSnapshot->state, TransferState::Paused);
  QCOMPARE(receiverPauseSnapshot->state, TransferState::Paused);
  QCOMPARE(senderPauseSnapshot->progress.completedBytes, pausedSenderBytes);
  QCOMPARE(receiverPauseSnapshot->progress.completedBytes, pausedReceiverBytes);
  QCOMPARE(partFileSize(receiveRoot), pausedPartSize);
  QTRY_VERIFY_WITH_TIMEOUT(resume->isVisible() && resume->isEnabled(), 5'000);
  advanceStage(u"resume");
  QTest::mouseClick(resume, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      senderPauseSnapshot.has_value() && receiverPauseSnapshot.has_value() &&
          senderPauseSnapshot->state == TransferState::Completed && receiverPauseSnapshot->state == TransferState::Completed,
      90'000
  );
  advanceStage(u"completed");
  const auto pausedTargetPath = QDir(receiveRoot).filePath(QStringLiteral("paused-source.bin"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pausedTargetPath), 10'000);
  QFile pausedTarget(pausedTargetPath);
  QVERIFY(pausedTarget.open(QIODevice::ReadOnly));
  QCOMPARE(
      QCryptographicHash::hash(pausedTarget.readAll(), QCryptographicHash::Sha256),
      QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256)
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      !QFileInfo::exists(recoveryStore.outgoingStatePath(*pausedTransfer)) &&
          !QFileInfo::exists(recoveryStore.incomingStatePath(*pausedTransfer)),
      5'000
  );
  QVERIFY(!hasPartFile(receiveRoot));

  const auto cancelledStart = sender.send(receiverId, {QUrl::fromLocalFile(cancelledSourcePath)}, {});
  QVERIFY2(cancelledStart.ok(), qPrintable(cancelledStart.diagnostic));
  QVERIFY(cancelledStart.transferId.has_value());
  cancelledTransfer = *cancelledStart.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(offerPanel->isVisible() && accept->isVisible() && accept->isEnabled(), 10'000);
  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY2_WITH_TIMEOUT(
      cancelClicked,
      qPrintable(snapshotEvidence(receiverCancelSnapshot, *cancelledTransfer)),
      20'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      senderCancelSnapshot.has_value() && receiverCancelSnapshot.has_value() &&
          senderCancelSnapshot->state == TransferState::Cancelled && receiverCancelSnapshot->state == TransferState::Cancelled,
      15'000
  );
  advanceStage(u"cancelled");
  QVERIFY(receiverCancelStates.indexOf(TransferState::Cancelling) >= 0);
  QVERIFY(receiverCancelStates.indexOf(TransferState::Cancelling) <
          receiverCancelStates.lastIndexOf(TransferState::Cancelled));
  QVERIFY(!QFileInfo::exists(QDir(receiveRoot).filePath(QStringLiteral("cancelled-source.bin"))));
  QVERIFY(hasPartFile(receiveRoot));
  QVERIFY(QFileInfo::exists(recoveryStore.incomingStatePath(*cancelledTransfer)));
  QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(recoveryStore.outgoingStatePath(*cancelledTransfer)), 5'000);
  QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

  advanceStage(u"teardown");
  composition.stop();
  sender.stop();
}

void TransferRuntimeCompositionTests::productionHistoryRetryButtonReoffersChangedSource()
{
  QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/relaydesk-r4-history-retry-XXXXXX"));
  QVERIFY2(temporary.isValid(), qPrintable(temporary.errorString()));
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(temporary);
  const auto identity = TlsIdentityAdapter::inspect(identityPath);
  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));

  const QByteArray originalContents(2 * 1024 * 1024 + 17, '\x5a');
  const QByteArray replacementContents(2 * 1024 * 1024 + 17, '\x3c');
  const auto sourcePath = temporary.filePath(QStringLiteral("retry-source.bin"));
  QFile source(sourcePath);
  QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(source.errorString()));
  QCOMPARE(source.write(originalContents), static_cast<qint64>(originalContents.size()));
  source.close();
  const auto receiveRoot = temporary.filePath(QStringLiteral("received"));
  const auto recoveryRoot = temporary.filePath(QStringLiteral("recovery"));
  QVERIFY(QDir().mkpath(receiveRoot));

  Fixture senderFixture;
  Fixture receiverFixture;
  senderFixture.transferDock.resize(560, 480);
  senderFixture.transferDock.show();
  receiverFixture.devicesDock.resize(480, 720);
  receiverFixture.devicesDock.show();
  const auto senderId = DeviceId::generate();
  const auto receiverId = DeviceId::generate();
  TrustedDeviceStore senderTrust(temporary.filePath(QStringLiteral("sender-trust.json")));
  TrustedDeviceStore receiverTrust(temporary.filePath(QStringLiteral("receiver-trust.json")));
  QVERIFY(senderTrust.upsert(loopbackTrustedDevice(receiverId, identity.fingerprintSha256)));
  QVERIFY(receiverTrust.upsert(loopbackTrustedDevice(senderId, identity.fingerprintSha256)));
  DeviceDiscoveryRuntime senderDiscovery(
      loopbackDevice(senderId, identity.fingerprintSha256, QStringLiteral("Sender")), senderFixture.devices
  );
  DeviceDiscoveryRuntime receiverDiscovery(
      loopbackDevice(receiverId, identity.fingerprintSha256, QStringLiteral("Receiver")), receiverFixture.devices
  );
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  options.recoveryStateRoot = recoveryRoot;
  options.tlsSettings.maxQueuedWriteBytes = 64U * 1024U;
  options.localCapabilities.preferredChunkBytes = 4U * 1024U;
  options.localCapabilities.maxPayloadBytes = 16U * 1024U;
  auto sender = std::make_unique<FileTransferRuntime>(senderId, senderTrust, senderDiscovery, identityPath, options);
  auto receiver = std::make_unique<FileTransferRuntime>(
      receiverId, receiverTrust, receiverDiscovery, identityPath, options
  );
  auto *senderRuntime = sender.get();
  auto *receiverRuntime = receiver.get();
  TransferRuntimeComposition senderComposition(
      std::move(sender),
      {.start = [senderRuntime](QString *diagnostic) { return senderRuntime->start(diagnostic); },
       .stop = [senderRuntime] { senderRuntime->stop(); }},
      senderFixture.devicesDock, senderFixture.transferDock,
      {.destinationRoot = temporary.filePath(QStringLiteral("sender-history")), .availableBytes = 0},
      temporary.filePath(QStringLiteral("sender-history/transfers.jsonl"))
  );
  TransferRuntimeComposition receiverComposition(
      std::move(receiver),
      {.start = [receiverRuntime](QString *diagnostic) { return receiverRuntime->start(diagnostic); },
       .stop = [receiverRuntime] { receiverRuntime->stop(); }},
      receiverFixture.devicesDock, receiverFixture.transferDock,
      {.destinationRoot = receiveRoot, .availableBytes = 128U * 1024U * 1024U}
  );
  TransferRecoveryStore recoveryStore(recoveryRoot);

  auto *offerPanel = receiverFixture.devicesDock.findChild<QFrame *>(QStringLiteral("relaydeskIncomingOfferPanel"));
  auto *accept = receiverFixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  auto *senderList = senderFixture.transferDock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *retry = senderFixture.transferDock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferRetryButton"));
  QVERIFY(offerPanel != nullptr);
  QVERIFY(accept != nullptr);
  QVERIFY(senderList != nullptr);
  QVERIFY(retry != nullptr);

  std::optional<TransferSnapshot> failedSnapshot;
  std::optional<TransferSnapshot> completedSnapshot;
  QList<TransferOperationResult> retryOperations;
  connect(senderRuntime, &IFileTransferService::transferChanged, this, [&](const TransferSnapshot &snapshot) {
    if (snapshot.direction != TransferDirection::Sending)
      return;
    if (snapshot.state == TransferState::Failed)
      failedSnapshot = snapshot;
    if (snapshot.state == TransferState::Completed)
      completedSnapshot = snapshot;
  });
  connect(
      senderRuntime, &IFileTransferService::transferOperationFinished, this,
      [&](const TransferOperationResult &result) {
        if (result.operation == TransferOperation::Retry)
          retryOperations.append(result);
      }
  );

  QString diagnostic;
  QVERIFY2(senderComposition.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY2(receiverComposition.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(senderDiscovery.registry().observeAdvertisement(
      receiverDiscovery.service().localDevice(), QHostAddress::LocalHost
  ));

  const auto initialStart = senderRuntime->send(receiverId, {QUrl::fromLocalFile(sourcePath)}, {});
  QVERIFY2(initialStart.ok(), qPrintable(initialStart.diagnostic));
  QVERIFY(initialStart.transferId.has_value());
  const auto initialId = *initialStart.transferId;
  QTRY_VERIFY_WITH_TIMEOUT(offerPanel->isVisible() && accept->isVisible() && accept->isEnabled(), 10'000);
  QFile changedSource(sourcePath);
  QVERIFY2(changedSource.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(changedSource.errorString()));
  QCOMPARE(changedSource.write(replacementContents), static_cast<qint64>(replacementContents.size()));
  changedSource.close();
  QTest::mouseClick(accept, Qt::LeftButton);

  QTRY_VERIFY_WITH_TIMEOUT(
      failedSnapshot.has_value() && failedSnapshot->id == initialId && failedSnapshot->state == TransferState::Failed &&
          failedSnapshot->canRetry,
      20'000
  );
  QVERIFY(failedSnapshot->errorCode != TransferErrorCode::None);
  QTRY_VERIFY_WITH_TIMEOUT(senderFixture.transfers.historyRecord(initialId).has_value(), 10'000);
  const auto failedHistory = senderFixture.transfers.historyRecord(initialId);
  QVERIFY(failedHistory.has_value());
  QCOMPARE(failedHistory->status, HistoryStatus::Failed);
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recoveryStore.incomingStatePath(initialId)), 5'000);
  const auto initialIncomingRecovery = recoveryStore.loadIncoming(initialId);
  QVERIFY2(initialIncomingRecovery.ok(), qPrintable(initialIncomingRecovery.diagnostic));
  const auto initialSidecarPath = recoveryStore.incomingStatePath(initialId);
  QFile initialSidecar(initialSidecarPath);
  QVERIFY2(initialSidecar.open(QIODevice::ReadOnly), qPrintable(initialSidecar.errorString()));
  const auto initialSidecarContents = initialSidecar.readAll();
  QVERIFY(!initialSidecarContents.isEmpty());
  QVERIFY(!hasPartFile(receiveRoot));
  const bool initialOutgoingRecoveryExists = QFileInfo::exists(recoveryStore.outgoingStatePath(initialId));
  const auto failedRow = senderFixture.transfers.indexOf(initialId);
  QVERIFY(failedRow >= 0);
  senderList->setCurrentIndex(senderFixture.transfers.index(failedRow, 0));
  QTRY_VERIFY_WITH_TIMEOUT(retry->isVisible() && retry->isEnabled(), 5'000);
  QTest::mouseClick(retry, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      !senderFixture.transfers.data(
          senderFixture.transfers.index(failedRow, 0), TransferCenterModel::CanRetryRole
      ).toBool(),
      5'000
  );
  QVERIFY(!retry->isVisible() || !retry->isEnabled());
  QTRY_VERIFY_WITH_TIMEOUT(
      !retryOperations.isEmpty() && retryOperations.last().outcome == TransferOperationOutcome::Applied,
      10'000
  );
  QTRY_VERIFY_WITH_TIMEOUT(
      offerPanel->isVisible() && accept->isVisible() && accept->isEnabled() &&
          receiverComposition.incomingOffers().offer().has_value() &&
          receiverComposition.incomingOffers().offer()->offer.transferId != initialId,
      10'000
  );
  const auto retriedOfferId = receiverComposition.incomingOffers().offer()->offer.transferId;
  QVERIFY(senderFixture.transfers.historyRecord(initialId).has_value());
  QCOMPARE(senderFixture.transfers.historyRecord(initialId)->status, HistoryStatus::Failed);
  QCOMPARE(QFileInfo::exists(recoveryStore.outgoingStatePath(initialId)), initialOutgoingRecoveryExists);
  QVERIFY(QFileInfo::exists(initialSidecarPath));
  QFile retrySidecar(initialSidecarPath);
  QVERIFY2(retrySidecar.open(QIODevice::ReadOnly), qPrintable(retrySidecar.errorString()));
  QCOMPARE(retrySidecar.readAll(), initialSidecarContents);
  const auto retryIncomingRecovery = recoveryStore.loadIncoming(initialId);
  QVERIFY2(retryIncomingRecovery.ok(), qPrintable(retryIncomingRecovery.diagnostic));
  QCOMPARE(*retryIncomingRecovery.state, *initialIncomingRecovery.state);
  QVERIFY(!hasPartFile(receiveRoot));

  QTest::mouseClick(accept, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(
      completedSnapshot.has_value() && completedSnapshot->id == retriedOfferId &&
          completedSnapshot->state == TransferState::Completed,
      30'000
  );
  const auto retriedId = completedSnapshot->id;
  const auto targetPath = QDir(receiveRoot).filePath(QStringLiteral("retry-source.bin"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(targetPath), 10'000);
  QFile target(targetPath);
  QVERIFY(target.open(QIODevice::ReadOnly));
  QCOMPARE(
      QCryptographicHash::hash(target.readAll(), QCryptographicHash::Sha256),
      QCryptographicHash::hash(replacementContents, QCryptographicHash::Sha256)
  );
  QTRY_VERIFY_WITH_TIMEOUT(senderFixture.transfers.historyRecord(retriedId).has_value(), 10'000);
  QCOMPARE(senderFixture.transfers.historyRecord(retriedId)->status, HistoryStatus::Completed);
  QCOMPARE(senderFixture.transfers.historyRecord(initialId)->status, HistoryStatus::Failed);
  QVERIFY(QFileInfo::exists(initialSidecarPath));
  QFile completedSidecar(initialSidecarPath);
  QVERIFY2(completedSidecar.open(QIODevice::ReadOnly), qPrintable(completedSidecar.errorString()));
  QCOMPARE(completedSidecar.readAll(), initialSidecarContents);
  const auto completedIncomingRecovery = recoveryStore.loadIncoming(initialId);
  QVERIFY2(completedIncomingRecovery.ok(), qPrintable(completedIncomingRecovery.diagnostic));
  QCOMPARE(*completedIncomingRecovery.state, *initialIncomingRecovery.state);
  QVERIFY(!hasPartFile(receiveRoot));

  receiverComposition.stop();
  senderComposition.stop();
}

QTEST_MAIN(TransferRuntimeCompositionTests)

#include "TransferRuntimeCompositionTests.moc"
