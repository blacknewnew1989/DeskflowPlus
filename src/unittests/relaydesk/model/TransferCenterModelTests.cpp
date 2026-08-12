/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/TransferCenterModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

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
      .id = QUuid(id),
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
      .errorMessageKey = state == TransferState::Failed ? QStringLiteral("relaydesk.transfer.io_error") : QString(),
      .errorCode = state == TransferState::Failed ? 4008 : 0,
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
      .transferId = QUuid(id),
      .peerDeviceId = *DeviceId::fromString(QStringLiteral("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff")),
      .peerDisplayName = QStringLiteral("History peer"),
      .displayName = QStringLiteral("Archive"),
      .direction = HistoryDirection::Receiving,
      .fileCount = 4,
      .totalBytes = 4096,
      .startedUtc = kBaseUtc.addSecs(-20),
      .finishedUtc = kBaseUtc.addSecs(-10),
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? 4008 : 0,
      .errorMessageKey = status == HistoryStatus::Failed ? QStringLiteral("relaydesk.transfer.io_error") : QString(),
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
  void mapsStateAndErrorsToSafeVisibleStrings();
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
           newerReceiving.id.toString(QUuid::WithoutBraces));
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
  TransferCenterModel model;
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
  qRegisterMetaType<TransferSnapshot>();
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
  const auto *emitted = static_cast<const TransferSnapshot *>(arguments.constFirst().constData());
  QVERIFY(emitted != nullptr);
  QCOMPARE(emitted->id, active.id);
  QCOMPARE(emitted->state, TransferState::Transferring);

  active.state = TransferState::Paused;
  active.canPause = false;
  active.canResume = true;
  QVERIFY(model.upsertTransfer(active));
  QCOMPARE(emitted->state, TransferState::Transferring);
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

void TransferCenterModelTests::mapsStateAndErrorsToSafeVisibleStrings()
{
  TransferCenterModel model;
  auto failed = transferSnapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferDirection::Receiving,
      TransferState::Failed
  );
  failed.errorMessageKey = QStringLiteral("remote.secret.backend_stacktrace");
  QVERIFY(model.upsertTransfer(failed));
  const auto index = model.index(0, 0);
  QCOMPARE(model.data(index, TransferCenterModel::StateTextRole).toString(), QStringLiteral("Failed"));
  QCOMPARE(model.data(index, TransferCenterModel::ErrorTextRole).toString(), QStringLiteral("Transfer failed. Try again."));
  QVERIFY(!model.data(index, TransferCenterModel::ErrorTextRole).toString().contains(QStringLiteral("stacktrace")));
  QCOMPARE(model.data(index, TransferCenterModel::CanRetryRole).toBool(), true);
  QCOMPARE(model.data(index, TransferCenterModel::CanCancelRole).toBool(), false);
}

QTEST_MAIN(TransferCenterModelTests)

#include "TransferCenterModelTests.moc"
