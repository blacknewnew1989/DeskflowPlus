/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferCenterDock.h"

#include "relaydesk/model/TransferCenterModel.h"

#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

TransferSnapshot snapshot()
{
  return {
      .id = QUuid(QStringLiteral("11111111-1111-4111-8111-111111111111")),
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

} // namespace

class TransferCenterDockTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void showsEmptyAndUsesKeyboardAccessibleControls();
};

void TransferCenterDockTests::showsEmptyAndUsesKeyboardAccessibleControls()
{
  qRegisterMetaType<TransferSnapshot>();
  TransferCenterModel model;
  TransferCenterDock dock(model);
  dock.resize(480, 420);
  dock.show();
  auto *empty = dock.findChild<QLabel *>(QStringLiteral("relaydeskTransfersEmptyLabel"));
  auto *list = dock.findChild<QListView *>(QStringLiteral("relaydeskTransfersView"));
  auto *pause = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferPauseButton"));
  auto *resume = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferResumeButton"));
  auto *cancel = dock.findChild<QPushButton *>(QStringLiteral("relaydeskTransferCancelButton"));
  QVERIFY(empty != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(pause != nullptr);
  QVERIFY(resume != nullptr);
  QVERIFY(cancel != nullptr);
  QVERIFY(empty->isVisible());
  QCOMPARE(empty->text(), QStringLiteral("Transfers will appear here"));

  const auto active = snapshot();
  QVERIFY(model.upsertTransfer(active));
  QTRY_VERIFY(list->isVisible());
  list->setCurrentIndex(model.index(0, 0));
  QTRY_VERIFY(pause->isEnabled());
  QVERIFY(!resume->isEnabled());
  QVERIFY(cancel->isEnabled());
  QCOMPARE(list->accessibleName(), QStringLiteral("Transfers"));
  QCOMPARE(pause->accessibleName(), QStringLiteral("Pause"));
  QVERIFY(pause->focusPolicy() != Qt::NoFocus);

  QSignalSpy paused(&model, &TransferCenterModel::pauseRequested);
  pause->setFocus();
  QTest::keyClick(pause, Qt::Key_Space);
  QCOMPARE(paused.count(), 1);
  QSignalSpy cancelled(&model, &TransferCenterModel::cancelRequested);
  cancel->setFocus();
  QTest::keyClick(cancel, Qt::Key_Space);
  QCOMPARE(cancelled.count(), 1);
}

QTEST_MAIN(TransferCenterDockTests)

#include "TransferCenterDockTests.moc"
