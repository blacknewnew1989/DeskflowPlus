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

#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFileInfo>

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
  void loadsMoreThanOneHundredHistoryRecordsAtStartup();
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

QTEST_MAIN(TransferRuntimeCompositionTests)

#include "TransferRuntimeCompositionTests.moc"
