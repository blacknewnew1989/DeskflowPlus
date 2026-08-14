/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferMiniBar.h"

#include "relaydesk/model/TransferCenterModel.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

TransferSnapshot
snapshot(const QString &id, const QString &name, TransferState state, int createdOffset = 0, int finishedOffset = 0)
{
  return {
      .id = *TransferId::fromString(id),
      .peerId = *DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = name,
      .direction = TransferDirection::Sending,
      .state = state,
      .progress =
          {
              .completedBytes = 25,
              .totalBytes = 100,
              .completedFiles = 1,
              .totalFiles = 2,
              .bytesPerSecond = 2048.0,
          },
      .canPause = state == TransferState::Transferring,
      .canResume = state == TransferState::Paused,
      .canRetry = state == TransferState::Failed,
      .createdUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, QTimeZone::UTC).addSecs(createdOffset),
      .finishedUtc = finishedOffset > 0
                         ? QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, QTimeZone::UTC).addSecs(finishedOffset)
                         : QDateTime{},
  };
}

} // namespace

class TransferMiniBarTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void followsVisibilityAndPresentsActiveTransfer();
  void selectsActiveThenMostRecentAndDispatchesPrimaryAction();
};

void TransferMiniBarTests::followsVisibilityAndPresentsActiveTransfer()
{
  qRegisterMetaType<TransferId>();
  TransferCenterModel model;
  QWidget host;
  auto *layout = new QVBoxLayout(&host);
  auto *bar = new TransferMiniBar(model, &host);
  layout->addWidget(bar);
  host.resize(560, 100);
  host.show();

  QCOMPARE(bar->minimumHeight(), 52);
  QCOMPARE(bar->maximumHeight(), 52);
  QVERIFY(bar->isHidden());

  const auto active = snapshot(
      QStringLiteral("11111111-1111-4111-8111-111111111111"), QStringLiteral("Quarterly archive"),
      TransferState::Transferring
  );
  QVERIFY(model.upsertTransfer(active));
  QTRY_VERIFY(bar->isVisible());
  QCOMPARE(bar->height(), 52);

  auto *title = bar->findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarTitle"));
  auto *metrics = bar->findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarMetrics"));
  auto *progress = bar->findChild<QProgressBar *>(QStringLiteral("relaydeskTransferMiniBarProgress"));
  auto *action = bar->findChild<QPushButton *>(QStringLiteral("relaydeskTransferMiniBarPrimaryAction"));
  QVERIFY(title != nullptr);
  QVERIFY(metrics != nullptr);
  QVERIFY(progress != nullptr);
  QVERIFY(action != nullptr);
  QCOMPARE(title->text(), active.displayName);
  const auto row = model.index(model.indexOf(active.id), 0);
  const auto expectedMetrics = row.data(TransferCenterModel::ProgressTextRole).toString() + QStringLiteral(" · ") +
                               row.data(TransferCenterModel::SpeedTextRole).toString();
  QCOMPARE(metrics->text(), expectedMetrics);
  QCOMPARE(progress->value(), 25);
  QVERIFY(action->isVisible());
  QCOMPARE(action->text(), QStringLiteral("Pause"));
  QCOMPARE(bar->accessibleName(), row.data(TransferCenterModel::AccessibleSummaryRole).toString());

  QSignalSpy details(bar, &TransferMiniBar::detailsRequested);
  QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
  QCOMPARE(details.count(), 1);
  bar->setFocus();
  QTest::keyClick(bar, Qt::Key_Return);
  QCOMPARE(details.count(), 2);

  std::optional<TransferId> pausedId;
  connect(&model, &TransferCenterModel::pauseRequested, this, [&](TransferId transferId) { pausedId = transferId; });
  QSignalSpy paused(&model, &TransferCenterModel::pauseRequested);
  QTest::mouseClick(action, Qt::LeftButton);
  QCOMPARE(paused.count(), 1);
  QVERIFY(pausedId.has_value());
  QCOMPARE(*pausedId, active.id);
  QCOMPARE(details.count(), 2);

  QVERIFY(model.removeTransfer(active.id));
  QTRY_VERIFY(bar->isHidden());
}

void TransferMiniBarTests::selectsActiveThenMostRecentAndDispatchesPrimaryAction()
{
  qRegisterMetaType<TransferId>();
  TransferCenterModel model;
  QWidget host;
  auto *layout = new QVBoxLayout(&host);
  auto *bar = new TransferMiniBar(model, &host);
  layout->addWidget(bar);
  host.resize(560, 100);
  host.show();

  const auto completed = snapshot(
      QStringLiteral("22222222-2222-4222-8222-222222222222"), QStringLiteral("Newest completed"),
      TransferState::Completed, 50, 200
  );
  const auto paused = snapshot(
      QStringLiteral("33333333-3333-4333-8333-333333333333"), QStringLiteral("Paused project"), TransferState::Paused,
      10
  );
  model.setTransfers({completed, paused});

  auto *title = bar->findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarTitle"));
  auto *metrics = bar->findChild<QLabel *>(QStringLiteral("relaydeskTransferMiniBarMetrics"));
  auto *action = bar->findChild<QPushButton *>(QStringLiteral("relaydeskTransferMiniBarPrimaryAction"));
  QVERIFY(title != nullptr);
  QVERIFY(metrics != nullptr);
  QVERIFY(action != nullptr);
  QTRY_COMPARE(title->text(), paused.displayName);
  QCOMPARE(action->text(), QStringLiteral("Resume"));
  QVERIFY(metrics->text().contains(model.index(0, 0).data(TransferCenterModel::StateTextRole).toString()));

  std::optional<TransferId> resumedId;
  connect(&model, &TransferCenterModel::resumeRequested, this, [&](TransferId transferId) { resumedId = transferId; });
  QSignalSpy resumed(&model, &TransferCenterModel::resumeRequested);
  action->click();
  QCOMPARE(resumed.count(), 1);
  QVERIFY(resumedId.has_value());
  QCOMPARE(*resumedId, paused.id);

  const auto failed = snapshot(
      QStringLiteral("44444444-4444-4444-8444-444444444444"), QStringLiteral("Failed package"), TransferState::Failed,
      100, 300
  );
  model.setTransfers({failed});
  QTRY_COMPARE(title->text(), failed.displayName);
  QCOMPARE(action->text(), QStringLiteral("Retry"));
  std::optional<TransferId> retriedId;
  connect(&model, &TransferCenterModel::retryRequested, this, [&](TransferId transferId) { retriedId = transferId; });
  QSignalSpy retried(&model, &TransferCenterModel::retryRequested);
  action->click();
  QCOMPARE(retried.count(), 1);
  QVERIFY(retriedId.has_value());
  QCOMPARE(*retriedId, failed.id);

  const auto older = snapshot(
      QStringLiteral("55555555-5555-4555-8555-555555555555"), QStringLiteral("Older completed"),
      TransferState::Completed, 5, 100
  );
  model.setTransfers({older, completed});
  QTRY_COMPARE(title->text(), completed.displayName);
  QVERIFY(!action->isVisible());
}

QTEST_MAIN(TransferMiniBarTests)

#include "TransferMiniBarTests.moc"
