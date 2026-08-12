// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferControlStateMachine.h"

#include <cmath>
#include <utility>

namespace relaydesk::transfer {
namespace {

TransferControlResult failure(TransferControlError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool validProgress(const TransferProgress &progress)
{
  return progress.completedBytes <= progress.totalBytes && progress.completedFiles <= progress.totalFiles &&
         std::isfinite(progress.bytesPerSecond) && progress.bytesPerSecond >= 0.0 &&
         (!progress.estimatedRemaining.has_value() || progress.estimatedRemaining->count() >= 0);
}

} // namespace

TransferControlStateMachine::TransferControlStateMachine(TransferSnapshot initial, Clock clock)
    : m_snapshot(std::move(initial)),
      m_clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); })
{
}

TransferControlResult TransferControlStateMachine::initialize()
{
  if (m_initialized) {
    return {};
  }
  if (m_snapshot.id.isNull() || m_snapshot.peerId.value().isNull() || m_snapshot.displayName.isEmpty() ||
      m_snapshot.state != TransferState::Preparing || !validProgress(m_snapshot.progress) ||
      !m_snapshot.createdUtc.isValid() || m_snapshot.createdUtc.toMSecsSinceEpoch() <= 0 ||
      m_snapshot.finishedUtc.isValid() || !m_snapshot.errorMessageKey.isEmpty() || m_snapshot.errorCode != 0) {
    return failure(
        TransferControlError::InvalidInitialSnapshot, QStringLiteral("initial transfer snapshot is invalid")
    );
  }
  m_snapshot.createdUtc = m_snapshot.createdUtc.toUTC();
  m_initialized = true;
  updateActions();
  return {.changed = true};
}

TransferControlResult TransferControlStateMachine::advance(TransferState next)
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (next == m_snapshot.state) {
    return {};
  }
  if (isTerminal(m_snapshot.state)) {
    return failure(TransferControlError::TerminalState, QStringLiteral("terminal transfer state cannot transition"));
  }
  if (next == TransferState::Failed || next == TransferState::Cancelling || next == TransferState::Cancelled ||
      !canAdvance(next)) {
    return failure(TransferControlError::InvalidTransition, QStringLiteral("transfer state transition is invalid"));
  }
  if (next == TransferState::Completed && (m_snapshot.progress.completedBytes != m_snapshot.progress.totalBytes ||
                                           m_snapshot.progress.completedFiles != m_snapshot.progress.totalFiles)) {
    return failure(
        TransferControlError::CompletionIncomplete, QStringLiteral("incomplete progress cannot be marked completed")
    );
  }
  return setState(next);
}

TransferControlResult TransferControlStateMachine::pause()
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (m_snapshot.state == TransferState::Paused) {
    return {};
  }
  if (m_snapshot.state != TransferState::Transferring) {
    return failure(TransferControlError::InvalidTransition, QStringLiteral("only an active transfer can be paused"));
  }
  return setState(TransferState::Paused);
}

TransferControlResult TransferControlStateMachine::resume()
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (m_snapshot.state == TransferState::Resuming) {
    return {};
  }
  if (m_snapshot.state != TransferState::Paused && m_snapshot.state != TransferState::Interrupted) {
    return failure(
        TransferControlError::InvalidTransition, QStringLiteral("only a paused or interrupted transfer can resume")
    );
  }
  return setState(TransferState::Resuming);
}

TransferControlResult TransferControlStateMachine::interrupt()
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (m_snapshot.state == TransferState::Interrupted) {
    return {};
  }
  if (m_snapshot.state != TransferState::Transferring && m_snapshot.state != TransferState::Paused &&
      m_snapshot.state != TransferState::Resuming) {
    return failure(
        TransferControlError::InvalidTransition, QStringLiteral("current transfer state cannot be interrupted")
    );
  }
  return setState(TransferState::Interrupted);
}

TransferControlResult TransferControlStateMachine::cancel()
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (m_snapshot.state == TransferState::Cancelling || m_snapshot.state == TransferState::Cancelled) {
    return {};
  }
  if (isTerminal(m_snapshot.state)) {
    return failure(TransferControlError::TerminalState, QStringLiteral("terminal transfer cannot be cancelled"));
  }
  return setState(TransferState::Cancelling);
}

TransferControlResult TransferControlStateMachine::confirmCancelled()
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (m_snapshot.state == TransferState::Cancelled) {
    return {};
  }
  if (m_snapshot.state != TransferState::Cancelling) {
    return failure(TransferControlError::InvalidTransition, QStringLiteral("cancellation has not been requested"));
  }
  return setState(TransferState::Cancelled);
}

TransferControlResult TransferControlStateMachine::fail(int errorCode, QString errorMessageKey)
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (errorCode <= 0 || errorMessageKey.isEmpty()) {
    return failure(TransferControlError::InvalidFailure, QStringLiteral("failure requires an error code and key"));
  }
  if (m_snapshot.state == TransferState::Failed) {
    if (m_snapshot.errorCode == errorCode && m_snapshot.errorMessageKey == errorMessageKey) {
      return {};
    }
    return failure(TransferControlError::TerminalState, QStringLiteral("failed transfer cannot change its error"));
  }
  if (isTerminal(m_snapshot.state)) {
    return failure(TransferControlError::TerminalState, QStringLiteral("terminal transfer cannot fail again"));
  }
  m_snapshot.errorCode = errorCode;
  m_snapshot.errorMessageKey = std::move(errorMessageKey);
  return setState(TransferState::Failed);
}

TransferControlResult TransferControlStateMachine::updateProgress(
    quint64 completedBytes, quint64 completedFiles, double bytesPerSecond,
    std::optional<std::chrono::seconds> estimatedRemaining, QString currentRelativeDisplayPath
)
{
  if (!m_initialized) {
    return failure(TransferControlError::InvalidInitialSnapshot, QStringLiteral("transfer state is not initialized"));
  }
  if (isTerminal(m_snapshot.state) || m_snapshot.state == TransferState::Cancelling) {
    return failure(TransferControlError::TerminalState, QStringLiteral("transfer progress is no longer mutable"));
  }
  if (completedBytes > m_snapshot.progress.totalBytes || completedFiles > m_snapshot.progress.totalFiles ||
      !std::isfinite(bytesPerSecond) || bytesPerSecond < 0.0 ||
      (estimatedRemaining.has_value() && estimatedRemaining->count() < 0)) {
    return failure(TransferControlError::InvalidProgress, QStringLiteral("transfer progress is invalid"));
  }
  if (completedBytes < m_snapshot.progress.completedBytes || completedFiles < m_snapshot.progress.completedFiles) {
    return failure(TransferControlError::ProgressRegression, QStringLiteral("transfer progress cannot move backward"));
  }

  const bool changed = completedBytes != m_snapshot.progress.completedBytes ||
                       completedFiles != m_snapshot.progress.completedFiles ||
                       bytesPerSecond != m_snapshot.progress.bytesPerSecond ||
                       estimatedRemaining != m_snapshot.progress.estimatedRemaining ||
                       currentRelativeDisplayPath != m_snapshot.currentRelativeDisplayPath;
  if (!changed) {
    return {};
  }
  m_snapshot.progress.completedBytes = completedBytes;
  m_snapshot.progress.completedFiles = completedFiles;
  m_snapshot.progress.bytesPerSecond = bytesPerSecond;
  m_snapshot.progress.estimatedRemaining = estimatedRemaining;
  m_snapshot.currentRelativeDisplayPath = std::move(currentRelativeDisplayPath);
  return {.changed = true};
}

const TransferSnapshot &TransferControlStateMachine::snapshot() const noexcept
{
  return m_snapshot;
}

bool TransferControlStateMachine::initialized() const noexcept
{
  return m_initialized;
}

bool TransferControlStateMachine::isTerminal(TransferState state) noexcept
{
  return state == TransferState::Completed || state == TransferState::Rejected || state == TransferState::Cancelled ||
         state == TransferState::Failed;
}

bool TransferControlStateMachine::canAdvance(TransferState next) const noexcept
{
  switch (m_snapshot.state) {
  case TransferState::Preparing:
    return next == TransferState::Offered;
  case TransferState::Offered:
    return next == TransferState::WaitingForAcceptance || next == TransferState::Rejected;
  case TransferState::WaitingForAcceptance:
    return next == TransferState::Queued || next == TransferState::Rejected;
  case TransferState::Queued:
    return next == TransferState::Transferring;
  case TransferState::Transferring:
    return next == TransferState::Paused || next == TransferState::Interrupted || next == TransferState::Verifying;
  case TransferState::Paused:
    return next == TransferState::Resuming || next == TransferState::Interrupted;
  case TransferState::Interrupted:
    return next == TransferState::Resuming;
  case TransferState::Resuming:
    return next == TransferState::Transferring || next == TransferState::Interrupted;
  case TransferState::Verifying:
    return next == TransferState::Committing;
  case TransferState::Committing:
    return next == TransferState::Completed;
  case TransferState::Completed:
  case TransferState::Rejected:
  case TransferState::Cancelling:
  case TransferState::Cancelled:
  case TransferState::Failed:
    return false;
  }
  return false;
}

TransferControlResult TransferControlStateMachine::setState(TransferState next)
{
  m_snapshot.state = next;
  if (isTerminal(next)) {
    markFinished();
  }
  updateActions();
  return {.changed = true};
}

void TransferControlStateMachine::updateActions()
{
  m_snapshot.canPause = m_snapshot.state == TransferState::Transferring;
  m_snapshot.canResume = m_snapshot.state == TransferState::Paused || m_snapshot.state == TransferState::Interrupted;
  m_snapshot.canCancel = !isTerminal(m_snapshot.state) && m_snapshot.state != TransferState::Cancelling;
  m_snapshot.canRetry = m_snapshot.state == TransferState::Failed;
}

void TransferControlStateMachine::markFinished()
{
  const QDateTime now = m_clock().toUTC();
  m_snapshot.finishedUtc = now.isValid() && now >= m_snapshot.createdUtc ? now : m_snapshot.createdUtc;
  m_snapshot.progress.bytesPerSecond = 0.0;
  m_snapshot.progress.estimatedRemaining.reset();
}

} // namespace relaydesk::transfer
