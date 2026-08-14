/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferCenterDock.h"

#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/widgets/TransferHistoryDetailsDialog.h"

#include <QAction>
#include <QLabel>
#include <QListView>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <optional>
#include <utility>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

TransferSnapshot snapshot()
{
  return {
      .id = *TransferId::fromString(QStringLiteral("11111111-1111-4111-8111-111111111111")),
      .peerId = *DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Project"),
      .direction = TransferDirection::Sending,
      .state = TransferState::Transferring,
      .progress = {.completedBytes = 25, .totalBytes = 100, .completedFiles = 1, .totalFiles = 2},
      .canPause = true,
      .canCancel = true,
      .createdUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC),
  };
}

TransferHistoryRecord
historyRecord(const QString &id, HistoryStatus status, quint64 fileCount = 1, int finishedOffset = 0)
{
  const auto started = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC).addSecs(finishedOffset);
  return {
      .transferId = *TransferId::fromString(id),
      .peerDeviceId = *DeviceId::fromString(QStringLiteral("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff")),
      .peerDisplayName = QStringLiteral("History peer"),
      .displayName = QStringLiteral("Archive"),
      .direction = HistoryDirection::Receiving,
      .fileCount = fileCount,
      .totalBytes = 4096,
      .startedUtc = started,
      .finishedUtc = started.addSecs(75),
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? TransferErrorCode::InternalError : TransferErrorCode::None,
  };
}

} // namespace

class TransferCenterDockTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void showsEmptyAndUsesKeyboardAccessibleControls();
  void presentsHistoryDetailsAndEmitsSafeKeyboardIntents();
};

void TransferCenterDockTests::showsEmptyAndUsesKeyboardAccessibleControls()
{
  qRegisterMetaType<TransferId>();
  qRegisterMetaType<TransferCancelOptions>();
  TransferCenterModel model;
  TransferCenterDock dock(model);
  QCOMPARE(dock.minimumWidth(), 320);
  dock.resize(532, 300);
  dock.show();
  auto *empty = dock.findChild<QLabel *>(QStringLiteral("relaydeskTransfersEmptyLabel"));
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *pause = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferPauseButton"));
  auto *resume = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferResumeButton"));
  auto *cancel = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferCancelButton"));
  auto *more = dock.findChild<QToolButton *>(QStringLiteral("relaydeskTransferMoreButton"));
  auto *cancelAction = dock.findChild<QAction *>(QStringLiteral("relaydeskTransferCancelMenuAction"));
  QVERIFY(empty != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(pause != nullptr);
  QVERIFY(resume != nullptr);
  QVERIFY(cancel != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(cancelAction != nullptr);
  QVERIFY(empty->isVisible());
  QCOMPARE(empty->text(), QStringLiteral("Transfers will appear here"));

  const auto active = snapshot();
  QVERIFY(model.upsertTransfer(active));
  QTRY_VERIFY(list->isVisible());
  list->setCurrentIndex(model.index(0, 0));
  QTRY_VERIFY(pause->isEnabled());
  QTRY_VERIFY(pause->isVisible());
  QVERIFY(!resume->isVisible());
  QVERIFY(!cancel->isVisible());
  QVERIFY(cancel->isEnabled());
  QVERIFY(more->isVisible());
  QVERIFY(cancelAction->isVisible());
  QCOMPARE(more->accessibleName(), QStringLiteral("Cancel"));
  const auto rowHeight = list->sizeHintForRow(0);
  QVERIFY2(rowHeight >= 52 && rowHeight <= 64, qPrintable(QStringLiteral("transfer row height: %1").arg(rowHeight)));
  QCOMPARE(list->accessibleName(), QStringLiteral("Transfers"));
  QCOMPARE(pause->accessibleName(), QStringLiteral("Pause"));
  QVERIFY(pause->focusPolicy() != Qt::NoFocus);

  QSignalSpy paused(&model, &TransferCenterModel::pauseRequested);
  pause->setFocus();
  QTest::keyClick(pause, Qt::Key_Space);
  QCOMPARE(paused.count(), 1);
  QSignalSpy cancelled(&model, &TransferCenterModel::cancelRequested);
  cancelAction->trigger();
  QCOMPARE(cancelled.count(), 1);
}

void TransferCenterDockTests::presentsHistoryDetailsAndEmitsSafeKeyboardIntents()
{
  TransferCenterModel model;
  const auto completed =
      historyRecord(QStringLiteral("11111111-1111-4111-8111-111111111111"), HistoryStatus::Completed);
  const auto failed =
      historyRecord(QStringLiteral("22222222-2222-4222-8222-222222222222"), HistoryStatus::Failed, 3, 100);
  model.setHistoryRecords({completed, failed});

  TransferCenterDock dock(model);
  dock.resize(560, 500);
  dock.show();
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *details = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferDetailsButton"));
  auto *openFolder = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferOpenFolderButton"));
  auto *openFile = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferOpenFileButton"));
  auto *retry = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferRetryButton"));
  auto *more = dock.findChild<QToolButton *>(QStringLiteral("relaydeskTransferMoreButton"));
  auto *detailsAction = dock.findChild<QAction *>(QStringLiteral("relaydeskTransferDetailsMenuAction"));
  auto *openFolderAction = dock.findChild<QAction *>(QStringLiteral("relaydeskTransferOpenFolderMenuAction"));
  QVERIFY(list != nullptr);
  QVERIFY(details != nullptr);
  QVERIFY(openFolder != nullptr);
  QVERIFY(openFile != nullptr);
  QVERIFY(retry != nullptr);
  QVERIFY(more != nullptr);
  QVERIFY(detailsAction != nullptr);
  QVERIFY(openFolderAction != nullptr);

  std::optional<TransferHistoryRecord> folderIntent;
  std::optional<TransferHistoryRecord> fileIntent;
  std::optional<TransferId> retryIntent;
  connect(&model, &TransferCenterModel::openFolderRequested, this, [&](TransferHistoryRecord record) {
    folderIntent = std::move(record);
  });
  connect(&model, &TransferCenterModel::openFileRequested, this, [&](TransferHistoryRecord record) {
    fileIntent = std::move(record);
  });
  connect(&model, &TransferCenterModel::historyRetryRequested, this, [&](TransferId transferId) {
    retryIntent = transferId;
  });

  list->setCurrentIndex(model.index(model.indexOf(completed.transferId), 0));
  QTRY_VERIFY(openFile->isVisible());
  QVERIFY(!details->isVisible());
  QVERIFY(!openFolder->isVisible());
  QTRY_VERIFY(openFile->isVisible());
  QVERIFY(!retry->isVisible());
  QVERIFY(more->isVisible());
  QVERIFY(detailsAction->isVisible());
  QVERIFY(openFolderAction->isVisible());
  QCOMPARE(details->accessibleName(), QStringLiteral("Details"));
  QCOMPARE(openFolder->accessibleName(), QStringLiteral("Open folder"));
  QCOMPARE(openFile->accessibleName(), QStringLiteral("Open file"));
  QVERIFY(details->focusPolicy() != Qt::NoFocus);
  QVERIFY(openFolder->focusPolicy() != Qt::NoFocus);

  openFolderAction->trigger();
  openFile->setFocus();
  QTest::keyClick(openFile, Qt::Key_Space);
  QVERIFY(folderIntent.has_value());
  QVERIFY(fileIntent.has_value());
  QCOMPARE(*folderIntent, completed);
  QCOMPARE(*fileIntent, completed);

  detailsAction->trigger();
  QTRY_VERIFY(dock.findChild<TransferHistoryDetailsDialog *>() != nullptr);
  auto *completedDialog = dock.findChild<TransferHistoryDetailsDialog *>();
  QVERIFY(completedDialog->isVisible());
  QCOMPARE(completedDialog->record(), completed);
  QCOMPARE(completedDialog->accessibleName(), QStringLiteral("Transfer details"));
  auto *name = completedDialog->findChild<QLabel *>(QStringLiteral("relaydeskTransferHistoryNameValue"));
  auto *duration = completedDialog->findChild<QLabel *>(QStringLiteral("relaydeskTransferHistoryDurationValue"));
  auto *close = completedDialog->findChild<QPushButton *>(QStringLiteral("relaydeskTransferHistoryCloseButton"));
  QVERIFY(name != nullptr);
  QVERIFY(duration != nullptr);
  QVERIFY(close != nullptr);
  QCOMPARE(name->text(), QStringLiteral("Archive"));
  QCOMPARE(duration->text(), QStringLiteral("2 minutes"));
  QCOMPARE(close->accessibleName(), QStringLiteral("Close"));
  QVERIFY(close->focusPolicy() != Qt::NoFocus);
  QPointer<TransferHistoryDetailsDialog> completedGuard(completedDialog);
  QTest::keyClick(completedDialog, Qt::Key_Escape);
  QTRY_VERIFY(completedGuard.isNull());

  list->setCurrentIndex(model.index(model.indexOf(failed.transferId), 0));
  QTRY_VERIFY(retry->isVisible());
  QVERIFY(!details->isVisible());
  QVERIFY(!openFolder->isVisible());
  QVERIFY(!openFile->isVisible());
  QVERIFY(detailsAction->isVisible());
  QCOMPARE(retry->accessibleName(), QStringLiteral("Retry"));
  retry->setFocus();
  QTest::keyClick(retry, Qt::Key_Space);
  QVERIFY(retryIntent.has_value());
  QCOMPARE(*retryIntent, failed.transferId);

  detailsAction->trigger();
  QTRY_VERIFY(dock.findChild<TransferHistoryDetailsDialog *>() != nullptr);
  auto *failedDialog = dock.findChild<TransferHistoryDetailsDialog *>();
  auto *error = failedDialog->findChild<QLabel *>(QStringLiteral("relaydeskTransferHistoryErrorValue"));
  QVERIFY(error != nullptr);
  QCOMPARE(error->text(), QStringLiteral("Transfer failed. Try again."));
  QVERIFY(!error->text().contains(QStringLiteral("stacktrace")));
  QVERIFY(!error->text().contains(QStringLiteral("4008")));
}

QTEST_MAIN(TransferCenterDockTests)

#include "TransferCenterDockTests.moc"
