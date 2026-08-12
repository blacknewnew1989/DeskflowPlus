// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/TransferControlStateMachine.h"

#include <QDateTime>

#include <chrono>
#include <optional>

namespace relaydesk::transfer {

inline constexpr auto kDefaultProgressPublishInterval = std::chrono::milliseconds{200};
inline constexpr auto kDefaultProgressEwmaWindow = std::chrono::seconds{7};

enum class ProgressPublishError
{
  None,
  InvalidTimestamp,
  ClockRollback,
  ProgressRegression,
  TotalChanged,
  CompletedExceedsTotal,
};

struct ProgressPublishResult
{
  std::optional<TransferSnapshot> snapshot;
  ProgressPublishError error = ProgressPublishError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ProgressPublishError::None;
  }

  [[nodiscard]] bool published() const noexcept
  {
    return snapshot.has_value() && ok();
  }
};

struct TransferProgressPublisherSettings
{
  std::chrono::milliseconds publishInterval = kDefaultProgressPublishInterval;
  std::chrono::milliseconds ewmaWindow =
      std::chrono::duration_cast<std::chrono::milliseconds>(kDefaultProgressEwmaWindow);
};

class TransferProgressPublisher final
{
public:
  explicit TransferProgressPublisher(TransferSnapshot initial, TransferProgressPublisherSettings settings = {});

  // Caller supplies a monotonic wall-clock sample. Ordinary updates publish
  // at most every publishInterval; state changes and terminal states publish
  // immediately. Paused/interrupted time never contributes to speed.
  [[nodiscard]] ProgressPublishResult update(
      quint64 completedBytes, quint64 completedFiles, TransferState state, QDateTime timestampUtc,
      QString currentRelativeDisplayPath = {}
  );

  [[nodiscard]] const TransferSnapshot &snapshot() const noexcept;

private:
  [[nodiscard]] bool isActive(TransferState state) const noexcept;
  [[nodiscard]] ProgressPublishResult fail(ProgressPublishError error, QString diagnostic) const;

  TransferSnapshot m_snapshot;
  TransferProgressPublisherSettings m_settings;
  QDateTime m_lastSampleUtc;
  QDateTime m_lastPublishedUtc;
  quint64 m_lastSampleBytes = 0;
  double m_ewmaBytesPerSecond = 0.0;
  bool m_hasSpeedSample = false;
};

} // namespace relaydesk::transfer
