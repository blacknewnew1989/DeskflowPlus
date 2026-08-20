/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/TransferCenterModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include <limits>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace relaydesk::transfer;

namespace {

const auto kBaseUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);

TransferSnapshot transferSnapshot(
    const QString &id, TransferDirection direction = TransferDirection::Sending,
    TransferState state = TransferState::Transferring, int seconds = 0, quint64 totalBytes = 100,
    quint64 totalFiles = 2
)
{
  const auto terminal = TransferControlStateMachine::isTerminal(state);
  return {
      .id = *TransferId::fromString(id),
      .peerId = *DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .peerDisplayName = direction == TransferDirection::Sending ? QStringLiteral("Studio Mac")
                                                                : QStringLiteral("Windows PC"),
      .displayName = direction == TransferDirection::Sending ? QStringLiteral("Project")
                                                             : QStringLiteral("Photos"),
      .direction = direction,
      .state = state,
      .progress = {
          .completedBytes = terminal && state == TransferState::Completed ? totalBytes : 25,
          .totalBytes = totalBytes,
          .completedFiles = terminal && state == TransferState::Completed ? totalFiles : 1,
          .totalFiles = totalFiles,
          .bytesPerSecond = terminal ? 0.0 : 12.5,
      },
      .currentRelativeDisplayPath = terminal ? QString() : QStringLiteral("folder/file.bin"),
      .errorCode = state == TransferState::Failed ? TransferErrorCode::SenderFailed : TransferErrorCode::None,
      .canPause = state == TransferState::Transferring,
      .canResume = state == TransferState::Paused || state == TransferState::Interrupted,
      .canCancel = !terminal,
      .canRetry = state == TransferState::Failed,
      .createdUtc = kBaseUtc.addSecs(seconds),
      .finishedUtc = terminal ? kBaseUtc.addSecs(seconds + 10) : QDateTime{},
  };
}

TransferHistoryRecord historyRecord(const QString &id, HistoryStatus status = HistoryStatus::Completed)
{
  return {
      .transferId = *TransferId::fromString(id),
      .peerDeviceId = *DeviceId::fromString(QStringLiteral("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff")),
      .peerDisplayName = QStringLiteral("History peer"),
      .displayName = QStringLiteral("Archive"),
      .direction = HistoryDirection::Receiving,
      .fileCount = 4,
      .totalBytes = 4096,
      .startedUtc = kBaseUtc.addSecs(-20),
      .finishedUtc = kBaseUtc.addSecs(-10),
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? TransferErrorCode::SenderFailed : TransferErrorCode::None,
      .completedRelativePath = status == HistoryStatus::Completed ? QStringLiteral("Archive/report.txt") : QString{},
      .topLevelTargetRelativePath = status == HistoryStatus::Completed ? QStringLiteral("Archive") : QString{},
  };
}

} // namespace

class TransferCenterModelTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void obeysAbstractItemModelContractAndUnifiesDirections();
  void updatesMovesAndRemovesByTransferId();
  void emitsImmutableControlIntentsOnlyWhenAllowed();
  void handlesZeroByteAndTerminalProgress();
  void importsHistoryWithoutDiskAccessOrDuplicateRows();
  void emitsValidatedHistoryOpenAndRetryIntents();
  void mapsStateAndErrorsToSafeVisibleStrings();
  void throttlesProgressUpdatesToFiveHertzButPublishesStateImmediately();
  void preservesServiceSpeedAndEta();
  void handlesUnknownAndCompletedEtaBoundaries();
  void throttlesTerminalNotificationsWithoutDroppingThem();
};

void TransferCenterModelTests::obeysAbstractItemModelContractAndUnifiesDirections()
{
  TransferCenterModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
  const auto olderSending = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Sending,
      TransferState::Transferring, 0
  );
  const auto newerReceiving = transferSnapshot(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferDirection::Receiving,
      TransferState::Queued, 10
  );
  const auto terminal = transferSnapshot(
      QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferDirection::Sending,
      TransferState::Completed, 20
  );
  model.setTransfers({olderSending, terminal, newerReceiving});

  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::TransferIdRole).toString(),
           newerReceiving.id.toString());
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::DirectionTextRole).toString(),
           QStringLiteral("Receiving"));
  QCOMPARE(model.data(model.index(1, 0), TransferCenterModel::DirectionTextRole).toString(),
           QStringLiteral("Sending"));
  QVERIFY(model.data(model.index(2, 0), TransferCenterModel::IsTerminalRole).toBool());
  QCOMPARE(model.roleNames().value(TransferCenterModel::PeerDisplayNameRole), QByteArray("peerDisplayName"));
  QVERIFY(model.data(model.index(0, 0), Qt::AccessibleTextRole).toString().contains(QStringLiteral("Photos")));
}

void TransferCenterModelTests::updatesMovesAndRemovesByTransferId()
{
  qint64 now = 1'000;
  TransferCenterModel model([&now]() { return now; });
  const auto first = transferSnapshot(QStringLiteral("11111111-1111-4111-8111-111111111111"));
  const auto second = transferSnapshot(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferDirection::Receiving,
      TransferState::Transferring, -5
  );
  QVERIFY(model.upsertTransfer(first));
  QVERIFY(model.upsertTransfer(second));
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);

  auto updated = first;
  updated.progress.completedBytes = 75;
  updated.currentRelativeDisplayPath = QStringLiteral("next.bin");
  QVERIFY(model.upsertTransfer(updated));
  QCOMPARE(changed.count(), 0);
  now += 200;
  model.flushDueUpdates();
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.snapshot(first.id)->progress.completedBytes, 75ULL);

  updated.state = TransferState::Completed;
  updated.progress.completedBytes = updated.progress.totalBytes;
  updated.progress.completedFiles = updated.progress.totalFiles;
  updated.progress.bytesPerSecond = 0.0;
  updated.canPause = false;
  updated.canCancel = false;
  updated.finishedUtc = kBaseUtc.addSecs(30);
  QVERIFY(model.upsertTransfer(updated));
  QCOMPARE(moved.count(), 1);
  QCOMPARE(model.indexOf(updated.id), 1);
  QVERIFY(model.removeTransfer(updated.id));
  QVERIFY(!model.removeTransfer(updated.id));
  QCOMPARE(model.rowCount(), 1);
}

void TransferCenterModelTests::emitsImmutableControlIntentsOnlyWhenAllowed()
{
  qRegisterMetaType<TransferId>();
  qRegisterMetaType<TransferCancelOptions>();
  TransferCenterModel model;
  auto active = transferSnapshot(QStringLiteral("11111111-1111-4111-8111-111111111111"));
  QVERIFY(model.upsertTransfer(active));
  QSignalSpy pause(&model, &TransferCenterModel::pauseRequested);
  QSignalSpy resume(&model, &TransferCenterModel::resumeRequested);
  QSignalSpy cancel(&model, &TransferCenterModel::cancelRequested);

  QVERIFY(model.requestPause(active.id));
  QVERIFY(!model.requestResume(active.id));
  QVERIFY(model.requestCancel(active.id));
  QCOMPARE(pause.count(), 1);
  QCOMPARE(cancel.count(), 1);
  const auto arguments = pause.takeFirst();
  QCOMPARE(*static_cast<const TransferId *>(arguments.constFirst().constData()), active.id);
  QCOMPARE(*static_cast<const TransferId *>(cancel.constFirst().at(0).constData()), active.id);
  QCOMPARE(cancel.constFirst().at(1).value<TransferCancelOptions>(), TransferCancelOptions{});

  active.state = TransferState::Paused;
  active.canPause = false;
  active.canResume = true;
  QVERIFY(model.upsertTransfer(active));
  QVERIFY(model.requestResume(active.id));
  QCOMPARE(resume.count(), 1);
}

void TransferCenterModelTests::handlesZeroByteAndTerminalProgress()
{
  TransferCenterModel model;
  auto zeroActive = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Sending,
      TransferState::Preparing, 0, 0, 0
  );
  zeroActive.progress.completedBytes = 0;
  zeroActive.progress.completedFiles = 0;
  zeroActive.progress.bytesPerSecond = 0.0;
  zeroActive.canPause = false;
  QVERIFY(model.upsertTransfer(zeroActive));
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::ProgressValueRole).toDouble(), 0.0);
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::ProgressPercentRole).toInt(), 0);

  zeroActive.state = TransferState::Completed;
  zeroActive.finishedUtc = kBaseUtc.addSecs(1);
  zeroActive.canCancel = false;
  QVERIFY(model.upsertTransfer(zeroActive));
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::ProgressValueRole).toDouble(), 1.0);
  QCOMPARE(model.data(model.index(0, 0), TransferCenterModel::ProgressPercentRole).toInt(), 100);
  QVERIFY(!model.data(model.index(0, 0), TransferCenterModel::CanCancelRole).toBool());
}

void TransferCenterModelTests::importsHistoryWithoutDiskAccessOrDuplicateRows()
{
  TransferCenterModel model;
  const auto active = transferSnapshot(QStringLiteral("11111111-1111-4111-8111-111111111111"));
  auto duplicateHistory = historyRecord(active.id.toString());
  const auto history = historyRecord(QStringLiteral("22222222-2222-4222-8222-222222222222"));
  model.setTransfers({active});
  model.setHistoryRecords({duplicateHistory, history});

  QCOMPARE(model.rowCount(), 2);
  QVERIFY(!model.historyRecord(active.id).has_value());
  QVERIFY(model.historyRecord(history.transferId).has_value());
  const auto row = model.indexOf(history.transferId);
  QVERIFY(model.data(model.index(row, 0), TransferCenterModel::IsHistoricalRole).toBool());
  QVERIFY(!model.requestCancel(history.transferId));
}

void TransferCenterModelTests::emitsValidatedHistoryOpenAndRetryIntents()
{
  TransferCenterModel model;
  auto completed = historyRecord(QStringLiteral("11111111-1111-4111-8111-111111111111"));
  completed.fileCount = 1;
  auto failed = historyRecord(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), HistoryStatus::Failed
  );
  failed.errorCode = TransferErrorCode::InternalError;
  const auto liveFailed = transferSnapshot(
      QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferDirection::Sending,
      TransferState::Failed
  );
  model.setTransfers({liveFailed});
  model.setHistoryRecords({completed, failed});

  const auto completedIndex = model.index(model.indexOf(completed.transferId), 0);
  QVERIFY(model.data(completedIndex, TransferCenterModel::HasHistoryDetailsRole).toBool());
  QVERIFY(model.data(completedIndex, TransferCenterModel::CanOpenFolderRole).toBool());
  QVERIFY(model.data(completedIndex, TransferCenterModel::CanOpenFileRole).toBool());
  QVERIFY(!model.data(completedIndex, TransferCenterModel::CanRetryRole).toBool());

  const auto failedIndex = model.index(model.indexOf(failed.transferId), 0);
  QVERIFY(model.data(failedIndex, TransferCenterModel::HasHistoryDetailsRole).toBool());
  QVERIFY(!model.data(failedIndex, TransferCenterModel::CanOpenFolderRole).toBool());
  QVERIFY(!model.data(failedIndex, TransferCenterModel::CanOpenFileRole).toBool());
  QVERIFY(!model.data(failedIndex, TransferCenterModel::CanRetryRole).toBool());
  QVERIFY(!model.requestRetry(failed.transferId));
  model.setHistoryRetryAvailable(failed.transferId, true);
  QVERIFY(model.data(failedIndex, TransferCenterModel::CanRetryRole).toBool());

  std::optional<TransferHistoryRecord> folderIntent;
  std::optional<TransferHistoryRecord> fileIntent;
  std::optional<TransferId> historyRetryIntent;
  std::optional<TransferId> liveRetryIntent;
  connect(&model, &TransferCenterModel::openFolderRequested, this, [&](TransferHistoryRecord record) {
    folderIntent = std::move(record);
  });
  connect(&model, &TransferCenterModel::openFileRequested, this, [&](TransferHistoryRecord record) {
    fileIntent = std::move(record);
  });
  connect(&model, &TransferCenterModel::historyRetryRequested, this, [&](TransferId transferId) {
    historyRetryIntent = transferId;
  });
  connect(&model, &TransferCenterModel::retryRequested, this, [&](TransferId transferId) {
    liveRetryIntent = transferId;
  });

  QVERIFY(model.requestOpenFolder(completed.transferId));
  QVERIFY(model.requestOpenFile(completed.transferId));
  QVERIFY(model.requestRetry(failed.transferId));
  QVERIFY(!model.requestRetry(failed.transferId));
  QVERIFY(model.requestRetry(liveFailed.id));
  QVERIFY(folderIntent.has_value());
  QVERIFY(fileIntent.has_value());
  QVERIFY(historyRetryIntent.has_value());
  QVERIFY(liveRetryIntent.has_value());
  QCOMPARE(*folderIntent, completed);
  QCOMPARE(*fileIntent, completed);
  QCOMPARE(*historyRetryIntent, failed.transferId);
  QCOMPARE(*liveRetryIntent, liveFailed.id);

  auto replacement = completed;
  replacement.displayName = QStringLiteral("Replacement");
  model.setHistoryRecords({replacement, failed});
  QCOMPARE(folderIntent->displayName, QStringLiteral("Archive"));

  QVERIFY(!model.requestRetry(completed.transferId));
  QVERIFY(!model.requestOpenFolder(failed.transferId));
  QVERIFY(!model.requestOpenFile(failed.transferId));
  QVERIFY(!model.requestOpenFolder(liveFailed.id));
  QVERIFY(!model.requestOpenFile(liveFailed.id));
  QVERIFY(!model.requestRetry(
      *TransferId::fromString(QStringLiteral("44444444-4444-4444-8444-444444444444"))
  ));
}

void TransferCenterModelTests::mapsStateAndErrorsToSafeVisibleStrings()
{
  TransferCenterModel model;
  auto failed = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Receiving,
      TransferState::Failed
  );
  failed.errorCode = TransferErrorCode::InternalError;
  QVERIFY(model.upsertTransfer(failed));
  const auto index = model.index(0, 0);
  QCOMPARE(model.data(index, TransferCenterModel::StateTextRole).toString(), QStringLiteral("Failed"));
  QCOMPARE(model.data(index, TransferCenterModel::ErrorTextRole).toString(), QStringLiteral("Transfer failed. Try again."));
  QVERIFY(!model.data(index, TransferCenterModel::ErrorTextRole).toString().contains(QStringLiteral("stacktrace")));
  QCOMPARE(model.data(index, TransferCenterModel::CanRetryRole).toBool(), true);
  QCOMPARE(model.data(index, TransferCenterModel::CanCancelRole).toBool(), false);
}

void TransferCenterModelTests::throttlesProgressUpdatesToFiveHertzButPublishesStateImmediately()
{
  qint64 now = 10'000;
  TransferCenterModel model([&now]() { return now; });
  auto active = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Sending,
      TransferState::Transferring, 0, 1000, 10
  );
  active.progress.completedBytes = 0;
  active.progress.completedFiles = 0;
  active.progress.bytesPerSecond = 100.0;
  QVERIFY(model.upsertTransfer(active));
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

  for (int sample = 1; sample <= 100; ++sample) {
    now = 10'000 + sample * 10;
    active.progress.completedBytes = static_cast<quint64>(sample * 5);
    active.progress.completedFiles = static_cast<quint64>(sample / 10);
    QVERIFY(model.upsertTransfer(active));
    model.flushDueUpdates();
  }
  QCOMPARE(changed.count(), 5);
  QCOMPARE(model.snapshot(active.id)->progress.completedBytes, 500ULL);

  now += 10;
  active.state = TransferState::Paused;
  active.canPause = false;
  active.canResume = true;
  active.progress.bytesPerSecond = 0.0;
  QVERIFY(model.upsertTransfer(active));
  QCOMPARE(changed.count(), 6);
  QCOMPARE(model.snapshot(active.id)->state, TransferState::Paused);
  QCOMPARE(model.snapshot(active.id)->progress.bytesPerSecond, 0.0);
}

void TransferCenterModelTests::preservesServiceSpeedAndEta()
{
  qint64 now = 20'000;
  TransferCenterModel model([&now]() { return now; });
  auto active = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Receiving,
      TransferState::Transferring, 0, 1000, 1
  );
  active.progress.completedBytes = 100;
  active.progress.completedFiles = 0;
  active.progress.bytesPerSecond = 100.0;
  active.progress.estimatedRemaining = std::chrono::seconds{9};
  QVERIFY(model.upsertTransfer(active));
  QCOMPARE(model.snapshot(active.id)->progress.bytesPerSecond, 100.0);
  QCOMPARE(model.snapshot(active.id)->progress.estimatedRemaining, std::chrono::seconds{9});

  now += 200;
  active.progress.completedBytes = 200;
  active.progress.bytesPerSecond = 300.0;
  active.progress.estimatedRemaining = std::chrono::seconds{6};
  QVERIFY(model.upsertTransfer(active));
  QCOMPARE(model.snapshot(active.id)->progress.bytesPerSecond, 300.0);
  QCOMPARE(model.snapshot(active.id)->progress.estimatedRemaining, std::chrono::seconds{6});
  const auto index = model.index(model.indexOf(active.id), 0);
  QVERIFY(model.data(index, TransferCenterModel::SpeedTextRole).toString().endsWith(QStringLiteral("/s")));
  QCOMPARE(model.data(index, TransferCenterModel::EtaTextRole).toString(), QStringLiteral("6 seconds remaining"));
  QVERIFY(model.data(index, Qt::AccessibleTextRole).toString().contains(QStringLiteral("6 seconds remaining")));

  now += 200;
  active.progress.completedBytes = 201;
  active.progress.bytesPerSecond = 0.25;
  active.progress.estimatedRemaining = std::chrono::seconds{10};
  QVERIFY(model.upsertTransfer(active));
  const auto lowSpeedText = model.data(index, TransferCenterModel::SpeedTextRole).toString();
  QVERIFY(lowSpeedText.endsWith(QStringLiteral("/s")));
  QVERIFY(lowSpeedText != QStringLiteral("Speed unavailable"));
}

void TransferCenterModelTests::handlesUnknownAndCompletedEtaBoundaries()
{
  TransferCenterModel model;
  auto zero = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Sending,
      TransferState::Transferring, 0, 0, 0
  );
  zero.progress.completedBytes = 0;
  zero.progress.completedFiles = 0;
  zero.progress.bytesPerSecond = 0.0;
  QVERIFY(model.upsertTransfer(zero));
  auto index = model.index(model.indexOf(zero.id), 0);
  QVERIFY(!model.snapshot(zero.id)->progress.estimatedRemaining.has_value());
  QCOMPARE(model.data(index, TransferCenterModel::SpeedTextRole).toString(), QStringLiteral("Speed unavailable"));
  QCOMPARE(model.data(index, TransferCenterModel::EtaTextRole).toString(), QStringLiteral("Calculating time remaining"));

  auto longRunning = transferSnapshot(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferDirection::Sending,
      TransferState::Transferring, 1, std::numeric_limits<quint64>::max(), 1
  );
  longRunning.progress.completedBytes = 0;
  longRunning.progress.completedFiles = 0;
  longRunning.progress.bytesPerSecond = 1.0;
  longRunning.progress.estimatedRemaining = std::chrono::hours{24 * 99};
  QVERIFY(model.upsertTransfer(longRunning));
  index = model.index(model.indexOf(longRunning.id), 0);
  QCOMPARE(model.data(index, TransferCenterModel::EtaTextRole).toString(), QStringLiteral("99 days or more remaining"));

  auto finishing = transferSnapshot(
      QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferDirection::Receiving,
      TransferState::Transferring, 2, 100, 1
  );
  finishing.progress.completedBytes = 100;
  finishing.progress.completedFiles = 1;
  finishing.progress.bytesPerSecond = 100.0;
  finishing.progress.estimatedRemaining = std::chrono::seconds{0};
  QVERIFY(model.upsertTransfer(finishing));
  index = model.index(model.indexOf(finishing.id), 0);
  QCOMPARE(model.snapshot(finishing.id)->progress.estimatedRemaining, std::chrono::seconds{0});
  QCOMPARE(model.data(index, TransferCenterModel::EtaTextRole).toString(), QStringLiteral("0 seconds remaining"));

  finishing.state = TransferState::Completed;
  finishing.canPause = false;
  finishing.canCancel = false;
  finishing.progress.bytesPerSecond = 0.0;
  finishing.finishedUtc = kBaseUtc.addSecs(20);
  QVERIFY(model.upsertTransfer(finishing));
  index = model.index(model.indexOf(finishing.id), 0);
  QVERIFY(model.data(index, TransferCenterModel::SpeedTextRole).toString().isEmpty());
  QVERIFY(model.data(index, TransferCenterModel::EtaTextRole).toString().isEmpty());

  auto paused = zero;
  paused.state = TransferState::Paused;
  paused.canPause = false;
  paused.canResume = true;
  QVERIFY(model.upsertTransfer(paused));
  index = model.index(model.indexOf(paused.id), 0);
  QVERIFY(model.data(index, TransferCenterModel::SpeedTextRole).toString().isEmpty());
  QVERIFY(model.data(index, TransferCenterModel::EtaTextRole).toString().isEmpty());

  paused.state = TransferState::Resuming;
  QVERIFY(model.upsertTransfer(paused));
  index = model.index(model.indexOf(paused.id), 0);
  QCOMPARE(model.data(index, TransferCenterModel::SpeedTextRole).toString(), QStringLiteral("Speed unavailable"));
  QCOMPARE(
      model.data(index, TransferCenterModel::EtaTextRole).toString(), QStringLiteral("Calculating time remaining")
  );
}

void TransferCenterModelTests::throttlesTerminalNotificationsWithoutDroppingThem()
{
  qRegisterMetaType<TransferSnapshot>();
  qint64 now = 30'000;
  TransferCenterModel model([&now]() { return now; });
  QSignalSpy notifications(&model, &TransferCenterModel::notificationRequested);
  auto first = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Sending,
      TransferState::Transferring
  );
  QVERIFY(model.upsertTransfer(first));
  first.state = TransferState::Completed;
  first.progress.completedBytes = first.progress.totalBytes;
  first.progress.completedFiles = first.progress.totalFiles;
  first.progress.bytesPerSecond = 0.0;
  first.canPause = false;
  first.canCancel = false;
  first.finishedUtc = kBaseUtc.addSecs(10);
  QVERIFY(model.upsertTransfer(first));
  QCOMPARE(notifications.count(), 1);
  QVERIFY(model.upsertTransfer(first));
  QCOMPARE(notifications.count(), 1);

  auto second = transferSnapshot(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferDirection::Receiving,
      TransferState::Failed, 1
  );
  second.errorCode = TransferErrorCode::InternalError;
  QVERIFY(model.upsertTransfer(second));
  QCOMPARE(notifications.count(), 1);
  now += 1999;
  model.flushDueUpdates();
  QCOMPARE(notifications.count(), 1);
  now += 1;
  model.flushDueUpdates();
  QCOMPARE(notifications.count(), 2);

  const auto firstArguments = notifications.at(0);
  const auto *firstSnapshot = static_cast<const TransferSnapshot *>(firstArguments.at(0).constData());
  QVERIFY(firstSnapshot != nullptr);
  QCOMPARE(firstSnapshot->id, first.id);
  QCOMPARE(firstArguments.at(1).toString(), QStringLiteral("Transfer completed"));
  QCOMPARE(firstArguments.at(2).toString(), QStringLiteral("Project · Studio Mac"));
  const auto secondArguments = notifications.at(1);
  QCOMPARE(secondArguments.at(1).toString(), QStringLiteral("Transfer failed"));
  QVERIFY(!secondArguments.at(2).toString().contains(QStringLiteral("diagnostic")));

  auto third = transferSnapshot(
      QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferDirection::Receiving,
      TransferState::Cancelled, 2
  );
  QVERIFY(model.upsertTransfer(third));
  QCOMPARE(notifications.count(), 2);
  model.setTransfers({first, second, third});
  now += 2000;
  model.flushDueUpdates();
  QCOMPARE(notifications.count(), 2);
  QVERIFY(model.removeTransfer(first.id));
  QVERIFY(model.upsertTransfer(first));
  QCOMPARE(notifications.count(), 3);

  auto removedWhileQueued = transferSnapshot(
      QStringLiteral("44444444-4444-4444-8444-444444444444"), TransferDirection::Sending,
      TransferState::Completed, 3
  );
  QVERIFY(model.upsertTransfer(removedWhileQueued));
  QCOMPARE(notifications.count(), 3);
  QVERIFY(model.removeTransfer(removedWhileQueued.id));
  now += 2000;
  model.flushDueUpdates();
  QCOMPARE(notifications.count(), 3);
}

QTEST_MAIN(TransferCenterModelTests)

#include "TransferCenterModelTests.moc"
