/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferRuntimeComposition.h"
#include "relaydesk/app/TransferHistoryRuntime.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include "../FakePairingService.h"

#include <QFile>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFileInfo>
#include <QDir>

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
};

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

QTEST_MAIN(TransferRuntimeCompositionTests)

#include "TransferRuntimeCompositionTests.moc"
