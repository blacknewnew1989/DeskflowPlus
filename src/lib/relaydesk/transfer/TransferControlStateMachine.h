// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/transfer/Protocol.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <functional>
#include <optional>

namespace relaydesk::transfer {

enum class TransferDirection
{
  Sending,
  Receiving,
};

enum class TransferState
{
  Preparing,
  Offered,
  WaitingForAcceptance,
  Queued,
  Transferring,
  Paused,
  Interrupted,
  Resuming,
  Verifying,
  Committing,
  Completed,
  Rejected,
  Cancelling,
  Cancelled,
  Failed,
};

struct TransferProgress
{
  quint64 completedBytes = 0;
  quint64 totalBytes = 0;
  quint64 completedFiles = 0;
  quint64 totalFiles = 0;
  double bytesPerSecond = 0.0;
  std::optional<std::chrono::seconds> estimatedRemaining;

  [[nodiscard]] bool operator==(const TransferProgress &) const = default;
};

struct TransferSnapshot
{
  TransferId id;
  deskflow::relaydesk::DeviceId peerId;
  QString peerDisplayName;
  QString displayName;
  TransferDirection direction = TransferDirection::Sending;
  TransferState state = TransferState::Preparing;
  TransferProgress progress;
  QString currentRelativeDisplayPath;
  QString errorMessageKey;
  int errorCode = 0;
  bool canPause = false;
  bool canResume = false;
  bool canCancel = false;
  bool canRetry = false;
  QDateTime createdUtc;
  QDateTime finishedUtc;

  [[nodiscard]] bool operator==(const TransferSnapshot &) const = default;
};

enum class TransferControlError
{
  None,
  InvalidInitialSnapshot,
  InvalidTransition,
  TerminalState,
  InvalidProgress,
  ProgressRegression,
  CompletionIncomplete,
  InvalidFailure,
};

struct TransferControlResult
{
  TransferControlError error = TransferControlError::None;
  bool changed = false;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TransferControlError::None;
  }
};

class TransferControlStateMachine final
{
public:
  using Clock = std::function<QDateTime()>;

  explicit TransferControlStateMachine(TransferSnapshot initial, Clock clock = {});

  [[nodiscard]] TransferControlResult initialize();
  [[nodiscard]] TransferControlResult advance(TransferState next);
  [[nodiscard]] TransferControlResult pause();
  [[nodiscard]] TransferControlResult resume();
  [[nodiscard]] TransferControlResult interrupt();
  [[nodiscard]] TransferControlResult cancel();
  [[nodiscard]] TransferControlResult confirmCancelled();
  [[nodiscard]] TransferControlResult fail(int errorCode, QString errorMessageKey);
  [[nodiscard]] TransferControlResult updateProgress(
      quint64 completedBytes, quint64 completedFiles, double bytesPerSecond,
      std::optional<std::chrono::seconds> estimatedRemaining = std::nullopt, QString currentRelativeDisplayPath = {}
  );

  [[nodiscard]] const TransferSnapshot &snapshot() const noexcept;
  [[nodiscard]] bool initialized() const noexcept;

  [[nodiscard]] static bool isTerminal(TransferState state) noexcept;

private:
  [[nodiscard]] bool canAdvance(TransferState next) const noexcept;
  [[nodiscard]] TransferControlResult setState(TransferState next);
  void updateActions();
  void markFinished();

  TransferSnapshot m_snapshot;
  Clock m_clock;
  bool m_initialized = false;
};

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::TransferDirection)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferState)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferProgress)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferSnapshot)
