// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferProgressPublisher.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace relaydesk::transfer;

namespace {

TransferSnapshot initialSnapshot(quint64 totalBytes = 10'000)
{
  return {
      .id = QUuid::createUuid(),
      .peerId = deskflow::relaydesk::DeviceId::generate(),
      .state = TransferState::Preparing,
      .progress = {.totalBytes = totalBytes, .totalFiles = 1},
  };
}

QDateTime at(qint64 milliseconds)
{
  return QDateTime::fromMSecsSinceEpoch(1'800'000'000'000LL + milliseconds, Qt::UTC);
}

} // namespace

class TransferProgressPublisherTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void throttlesOrdinaryUpdatesAndPublishesTransitions();
  void computesEwmaAndBoundedEta();
  void excludesPauseAndStallThenResumes();
  void handlesZeroUnknownTotalRollbackAndRegression();
  void avoidsOverflowAtLargeTotals();
};

void TransferProgressPublisherTests::throttlesOrdinaryUpdatesAndPublishesTransitions()
{
  TransferProgressPublisher publisher(initialSnapshot());
  auto result = publisher.update(0, 0, TransferState::Transferring, at(0));
  QVERIFY(result.published());
  QVERIFY(publisher.update(100, 0, TransferState::Transferring, at(50)).ok());
  QVERIFY(!publisher.update(200, 0, TransferState::Transferring, at(199)).published());
  result = publisher.update(201, 0, TransferState::Transferring, at(200));
  QVERIFY(result.published());
  result = publisher.update(201, 0, TransferState::Paused, at(201));
  QVERIFY(result.published());
  result = publisher.update(201, 0, TransferState::Cancelled, at(202));
  QVERIFY(result.published());
  result = publisher.update(201, 0, TransferState::Cancelled, at(203));
  QVERIFY(!result.published());
}

void TransferProgressPublisherTests::computesEwmaAndBoundedEta()
{
  TransferProgressPublisher publisher(initialSnapshot(10'000));
  QVERIFY(publisher.update(0, 0, TransferState::Transferring, at(0)).published());
  auto result = publisher.update(1000, 0, TransferState::Transferring, at(1000));
  QVERIFY(result.published());
  QCOMPARE(result.snapshot->progress.bytesPerSecond, 1000.0);
  QCOMPARE(result.snapshot->progress.estimatedRemaining, std::optional<std::chrono::seconds>{std::chrono::seconds{9}});
  result = publisher.update(3000, 0, TransferState::Transferring, at(2000));
  QVERIFY(result.published());
  QVERIFY(result.snapshot->progress.bytesPerSecond > 1000.0);
  QVERIFY(result.snapshot->progress.bytesPerSecond < 2000.0);
  QVERIFY(result.snapshot->progress.estimatedRemaining.has_value());
}

void TransferProgressPublisherTests::excludesPauseAndStallThenResumes()
{
  TransferProgressPublisher publisher(initialSnapshot());
  QVERIFY(publisher.update(0, 0, TransferState::Transferring, at(0)).published());
  QVERIFY(publisher.update(1000, 0, TransferState::Transferring, at(1000)).published());
  auto result = publisher.update(1000, 0, TransferState::Paused, at(1100));
  QCOMPARE(result.snapshot->progress.bytesPerSecond, 0.0);
  QVERIFY(!result.snapshot->progress.estimatedRemaining.has_value());
  result = publisher.update(1000, 0, TransferState::Resuming, at(11'100));
  QVERIFY(result.published());
  QCOMPARE(result.snapshot->progress.bytesPerSecond, 1000.0);
  result = publisher.update(1000, 0, TransferState::Resuming, at(12'100));
  QVERIFY(result.snapshot->progress.bytesPerSecond > 0.0);
  QVERIFY(result.snapshot->progress.bytesPerSecond < 1000.0);
  result = publisher.update(2000, 0, TransferState::Resuming, at(13'100));
  QVERIFY(result.snapshot->progress.bytesPerSecond > 0.0);
}

void TransferProgressPublisherTests::handlesZeroUnknownTotalRollbackAndRegression()
{
  TransferProgressPublisher publisher(initialSnapshot(0));
  auto result = publisher.update(0, 0, TransferState::Transferring, at(0));
  QVERIFY(result.published());
  QVERIFY(!result.snapshot->progress.estimatedRemaining.has_value());
  result = publisher.update(100, 0, TransferState::Transferring, at(1000));
  QVERIFY(result.published());
  QVERIFY(!result.snapshot->progress.estimatedRemaining.has_value());
  QCOMPARE(publisher.update(101, 0, TransferState::Transferring, at(999)).error, ProgressPublishError::ClockRollback);
  QCOMPARE(
      publisher.update(99, 0, TransferState::Transferring, at(2000)).error, ProgressPublishError::ProgressRegression
  );
}

void TransferProgressPublisherTests::avoidsOverflowAtLargeTotals()
{
  TransferProgressPublisher publisher(initialSnapshot(std::numeric_limits<quint64>::max()));
  QVERIFY(publisher.update(0, 0, TransferState::Transferring, at(0)).published());
  const auto result = publisher.update(1, 0, TransferState::Transferring, at(1000));
  QVERIFY(result.published());
  QVERIFY(std::isfinite(result.snapshot->progress.bytesPerSecond));
  // The mathematical ETA is beyond chrono::seconds::max, so it remains
  // unknown instead of overflowing or wrapping negative.
  QVERIFY(!result.snapshot->progress.estimatedRemaining.has_value());
}

QTEST_MAIN(TransferProgressPublisherTests)

#include "TransferProgressPublisherTests.moc"
