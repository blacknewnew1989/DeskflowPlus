// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferControlStateMachine.h"

#include <QtTest>

#include <cmath>

using namespace relaydesk::transfer;

namespace {

const auto kCreatedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);
const auto kFinishedUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'010'000LL, Qt::UTC);

TransferSnapshot initialSnapshot(quint64 totalBytes = 100, quint64 totalFiles = 2)
{
  return {
      .id = QUuid(QStringLiteral("11111111-2222-4333-8444-555555555555")),
      .peerId = *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .peerDisplayName = QStringLiteral("Peer"),
      .displayName = QStringLiteral("Documents"),
      .direction = TransferDirection::Sending,
      .state = TransferState::Preparing,
      .progress = {.totalBytes = totalBytes, .totalFiles = totalFiles},
      .createdUtc = kCreatedUtc,
  };
}

TransferControlStateMachine machine(quint64 totalBytes = 100, quint64 totalFiles = 2)
{
  return TransferControlStateMachine(initialSnapshot(totalBytes, totalFiles), [] { return kFinishedUtc; });
}

void advanceToTransferring(TransferControlStateMachine &state)
{
  QVERIFY(state.initialize().ok());
  QVERIFY(state.advance(TransferState::Offered).ok());
  QVERIFY(state.advance(TransferState::WaitingForAcceptance).ok());
  QVERIFY(state.advance(TransferState::Queued).ok());
  QVERIFY(state.advance(TransferState::Transferring).ok());
}

} // namespace

class TransferControlStateMachineTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initializesAndDerivesActions();
  void followsHappyPathAndCompletesOnlyAtTotals();
  void pauseResumeInterruptAreIdempotent();
  void cancelIsTwoPhaseAndRaceSafe();
  void failureIsTerminalAndStable();
  void rejectsInvalidTransitions();
  void progressIsMonotonicAndBounded();
  void rejectsInvalidInitialSnapshot();
};

void TransferControlStateMachineTests::initializesAndDerivesActions()
{
  auto state = machine();
  const auto initialized = state.initialize();
  QVERIFY(initialized.ok());
  QVERIFY(initialized.changed);
  QVERIFY(state.initialized());
  QCOMPARE(state.snapshot().state, TransferState::Preparing);
  QVERIFY(!state.snapshot().canPause);
  QVERIFY(!state.snapshot().canResume);
  QVERIFY(state.snapshot().canCancel);
  QVERIFY(!state.snapshot().canRetry);
  QVERIFY(!state.snapshot().finishedUtc.isValid());
  QVERIFY(QMetaType::fromType<TransferDirection>().isValid());
  QVERIFY(QMetaType::fromType<TransferState>().isValid());
  QVERIFY(QMetaType::fromType<TransferProgress>().isValid());
  QVERIFY(QMetaType::fromType<TransferSnapshot>().isValid());
  const auto duplicate = state.initialize();
  QVERIFY(duplicate.ok());
  QVERIFY(!duplicate.changed);
}

void TransferControlStateMachineTests::followsHappyPathAndCompletesOnlyAtTotals()
{
  auto state = machine();
  advanceToTransferring(state);
  QVERIFY(state.snapshot().canPause);
  QVERIFY(state.updateProgress(100, 2, 12.5, std::chrono::seconds{0}, QStringLiteral("done.bin")).ok());
  QVERIFY(state.advance(TransferState::Verifying).ok());
  QVERIFY(state.advance(TransferState::Committing).ok());
  const auto completed = state.advance(TransferState::Completed);
  QVERIFY(completed.ok());
  QCOMPARE(state.snapshot().state, TransferState::Completed);
  QCOMPARE(state.snapshot().finishedUtc, kFinishedUtc);
  QCOMPARE(state.snapshot().progress.bytesPerSecond, 0.0);
  QVERIFY(!state.snapshot().progress.estimatedRemaining.has_value());
  QVERIFY(!state.snapshot().canCancel);

  auto incomplete = machine();
  advanceToTransferring(incomplete);
  QVERIFY(incomplete.advance(TransferState::Verifying).ok());
  QVERIFY(incomplete.advance(TransferState::Committing).ok());
  QCOMPARE(incomplete.advance(TransferState::Completed).error, TransferControlError::CompletionIncomplete);
}

void TransferControlStateMachineTests::pauseResumeInterruptAreIdempotent()
{
  auto state = machine();
  advanceToTransferring(state);
  QVERIFY(state.pause().changed);
  QCOMPARE(state.snapshot().state, TransferState::Paused);
  QVERIFY(state.snapshot().canResume);
  QVERIFY(!state.pause().changed);

  QVERIFY(state.resume().changed);
  QCOMPARE(state.snapshot().state, TransferState::Resuming);
  QVERIFY(!state.resume().changed);
  QVERIFY(state.interrupt().changed);
  QCOMPARE(state.snapshot().state, TransferState::Interrupted);
  QVERIFY(!state.interrupt().changed);
  QVERIFY(state.resume().ok());
  QVERIFY(state.advance(TransferState::Transferring).ok());
  QVERIFY(state.interrupt().ok());
  QVERIFY(state.resume().ok());
  QVERIFY(state.advance(TransferState::Transferring).ok());
}

void TransferControlStateMachineTests::cancelIsTwoPhaseAndRaceSafe()
{
  auto state = machine();
  advanceToTransferring(state);
  QVERIFY(state.cancel().changed);
  QCOMPARE(state.snapshot().state, TransferState::Cancelling);
  QVERIFY(!state.snapshot().canCancel);
  QVERIFY(!state.cancel().changed);
  QVERIFY(state.confirmCancelled().changed);
  QCOMPARE(state.snapshot().state, TransferState::Cancelled);
  QCOMPARE(state.snapshot().finishedUtc, kFinishedUtc);
  QVERIFY(!state.cancel().changed);
  QVERIFY(!state.confirmCancelled().changed);
  QCOMPARE(state.pause().error, TransferControlError::InvalidTransition);
}

void TransferControlStateMachineTests::failureIsTerminalAndStable()
{
  auto state = machine();
  advanceToTransferring(state);
  const auto failed = state.fail(4008, QStringLiteral("relaydesk.transfer.io_error"));
  QVERIFY(failed.ok());
  QCOMPARE(state.snapshot().state, TransferState::Failed);
  QCOMPARE(state.snapshot().errorCode, 4008);
  QCOMPARE(state.snapshot().errorMessageKey, QStringLiteral("relaydesk.transfer.io_error"));
  QVERIFY(state.snapshot().canRetry);
  QVERIFY(!state.fail(4008, QStringLiteral("relaydesk.transfer.io_error")).changed);
  QCOMPARE(
      state.fail(4009, QStringLiteral("relaydesk.transfer.other_error")).error, TransferControlError::TerminalState
  );
  QCOMPARE(state.advance(TransferState::Preparing).error, TransferControlError::TerminalState);
  QCOMPARE(state.cancel().error, TransferControlError::TerminalState);
}

void TransferControlStateMachineTests::rejectsInvalidTransitions()
{
  auto state = machine();
  QCOMPARE(state.pause().error, TransferControlError::InvalidInitialSnapshot);
  QVERIFY(state.initialize().ok());
  QCOMPARE(state.advance(TransferState::Completed).error, TransferControlError::InvalidTransition);
  QCOMPARE(state.advance(TransferState::Queued).error, TransferControlError::InvalidTransition);
  QCOMPARE(state.resume().error, TransferControlError::InvalidTransition);
  QCOMPARE(state.interrupt().error, TransferControlError::InvalidTransition);
  QCOMPARE(state.confirmCancelled().error, TransferControlError::InvalidTransition);
  QCOMPARE(state.fail(0, QStringLiteral("key")).error, TransferControlError::InvalidFailure);
  QCOMPARE(state.fail(1, {}).error, TransferControlError::InvalidFailure);

  QVERIFY(state.advance(TransferState::Offered).ok());
  QVERIFY(state.advance(TransferState::Rejected).ok());
  QCOMPARE(state.snapshot().finishedUtc, kFinishedUtc);
  QCOMPARE(state.advance(TransferState::Preparing).error, TransferControlError::TerminalState);
}

void TransferControlStateMachineTests::progressIsMonotonicAndBounded()
{
  auto state = machine();
  advanceToTransferring(state);
  auto changed = state.updateProgress(50, 1, 25.5, std::chrono::seconds{2}, QStringLiteral("first.bin"));
  QVERIFY(changed.ok());
  QVERIFY(changed.changed);
  QCOMPARE(state.snapshot().progress.completedBytes, quint64{50});
  QCOMPARE(state.snapshot().progress.completedFiles, quint64{1});
  QCOMPARE(state.snapshot().progress.estimatedRemaining, std::optional<std::chrono::seconds>{std::chrono::seconds{2}});
  QVERIFY(!state.updateProgress(50, 1, 25.5, std::chrono::seconds{2}, QStringLiteral("first.bin")).changed);

  QCOMPARE(state.updateProgress(49, 1, 1.0).error, TransferControlError::ProgressRegression);
  QCOMPARE(state.updateProgress(50, 0, 1.0).error, TransferControlError::ProgressRegression);
  QCOMPARE(state.updateProgress(101, 1, 1.0).error, TransferControlError::InvalidProgress);
  QCOMPARE(state.updateProgress(50, 3, 1.0).error, TransferControlError::InvalidProgress);
  QCOMPARE(state.updateProgress(50, 1, -1.0).error, TransferControlError::InvalidProgress);
  QCOMPARE(state.updateProgress(50, 1, std::nan("")).error, TransferControlError::InvalidProgress);
  QCOMPARE(state.updateProgress(50, 1, 1.0, std::chrono::seconds{-1}).error, TransferControlError::InvalidProgress);
}

void TransferControlStateMachineTests::rejectsInvalidInitialSnapshot()
{
  auto invalid = initialSnapshot();
  invalid.id = QUuid{};
  TransferControlStateMachine nullId(std::move(invalid));
  QCOMPARE(nullId.initialize().error, TransferControlError::InvalidInitialSnapshot);
  QVERIFY(!nullId.initialized());

  invalid = initialSnapshot();
  invalid.progress.completedBytes = 101;
  TransferControlStateMachine overflow(std::move(invalid));
  QCOMPARE(overflow.initialize().error, TransferControlError::InvalidInitialSnapshot);

  invalid = initialSnapshot();
  invalid.state = TransferState::Transferring;
  TransferControlStateMachine wrongState(std::move(invalid));
  QCOMPARE(wrongState.initialize().error, TransferControlError::InvalidInitialSnapshot);
}

QTEST_GUILESS_MAIN(TransferControlStateMachineTests)
#include "TransferControlStateMachineTests.moc"
