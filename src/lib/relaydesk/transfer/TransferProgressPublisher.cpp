// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferProgressPublisher.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace relaydesk::transfer {

TransferProgressPublisher::TransferProgressPublisher(
    TransferSnapshot initial, TransferProgressPublisherSettings settings
)
    : m_snapshot(std::move(initial)),
      m_settings(settings),
      m_lastSampleBytes(m_snapshot.progress.completedBytes)
{
}

ProgressPublishResult TransferProgressPublisher::update(
    quint64 completedBytes, quint64 completedFiles, TransferState state, QDateTime timestampUtc,
    QString currentRelativeDisplayPath
)
{
  if (!timestampUtc.isValid() || timestampUtc.toMSecsSinceEpoch() <= 0 || m_settings.publishInterval.count() <= 0 ||
      m_settings.ewmaWindow.count() <= 0) {
    return fail(ProgressPublishError::InvalidTimestamp, QStringLiteral("progress timestamp or settings are invalid"));
  }
  timestampUtc = timestampUtc.toUTC();
  if (m_lastSampleUtc.isValid() && timestampUtc < m_lastSampleUtc) {
    return fail(ProgressPublishError::ClockRollback, QStringLiteral("progress timestamp moved backwards"));
  }
  if (completedBytes < m_snapshot.progress.completedBytes || completedFiles < m_snapshot.progress.completedFiles) {
    return fail(ProgressPublishError::ProgressRegression, QStringLiteral("transfer progress cannot regress"));
  }
  if (m_snapshot.progress.totalBytes > 0 && completedBytes > m_snapshot.progress.totalBytes) {
    return fail(ProgressPublishError::CompletedExceedsTotal, QStringLiteral("completed bytes exceed known total"));
  }
  if (m_snapshot.progress.totalFiles > 0 && completedFiles > m_snapshot.progress.totalFiles) {
    return fail(ProgressPublishError::CompletedExceedsTotal, QStringLiteral("completed files exceed known total"));
  }

  const TransferState previousState = m_snapshot.state;
  const bool stateChanged = state != previousState;
  const bool activeInterval = isActive(previousState) && isActive(state);
  if (m_lastSampleUtc.isValid() && timestampUtc > m_lastSampleUtc && activeInterval) {
    const qint64 elapsedMs = m_lastSampleUtc.msecsTo(timestampUtc);
    const quint64 deltaBytes = completedBytes - m_lastSampleBytes;
    const double instantaneous = static_cast<double>(deltaBytes) * 1000.0 / static_cast<double>(elapsedMs);
    const double alpha =
        1.0 - std::exp(-static_cast<double>(elapsedMs) / static_cast<double>(m_settings.ewmaWindow.count()));
    m_ewmaBytesPerSecond =
        m_hasSpeedSample ? alpha * instantaneous + (1.0 - alpha) * m_ewmaBytesPerSecond : instantaneous;
    m_hasSpeedSample = true;
  }
  if (!isActive(state)) {
    m_snapshot.progress.bytesPerSecond = 0.0;
    m_snapshot.progress.estimatedRemaining.reset();
  } else {
    m_snapshot.progress.bytesPerSecond =
        std::isfinite(m_ewmaBytesPerSecond) && m_ewmaBytesPerSecond > 0.0 ? m_ewmaBytesPerSecond : 0.0;
  }
  m_snapshot.progress.completedBytes = completedBytes;
  m_snapshot.progress.completedFiles = completedFiles;
  m_snapshot.state = state;
  if (!currentRelativeDisplayPath.isEmpty()) {
    m_snapshot.currentRelativeDisplayPath = std::move(currentRelativeDisplayPath);
  }
  m_lastSampleUtc = timestampUtc;
  m_lastSampleBytes = completedBytes;

  if (isActive(state) && m_snapshot.progress.totalBytes > 0 && completedBytes < m_snapshot.progress.totalBytes &&
      m_snapshot.progress.bytesPerSecond > 0.0) {
    const long double remaining = static_cast<long double>(m_snapshot.progress.totalBytes - completedBytes);
    const long double seconds = remaining / static_cast<long double>(m_snapshot.progress.bytesPerSecond);
    if (std::isfinite(seconds) && seconds >= 0.0L &&
        seconds <= static_cast<long double>(std::chrono::seconds::max().count())) {
      m_snapshot.progress.estimatedRemaining =
          std::chrono::seconds{static_cast<std::chrono::seconds::rep>(std::ceil(seconds))};
    } else {
      m_snapshot.progress.estimatedRemaining.reset();
    }
  } else {
    m_snapshot.progress.estimatedRemaining.reset();
  }

  const bool intervalElapsed =
      !m_lastPublishedUtc.isValid() || m_lastPublishedUtc.msecsTo(timestampUtc) >= m_settings.publishInterval.count();
  if (!stateChanged && !intervalElapsed) {
    return {};
  }
  m_lastPublishedUtc = timestampUtc;
  return {.snapshot = m_snapshot};
}

const TransferSnapshot &TransferProgressPublisher::snapshot() const noexcept
{
  return m_snapshot;
}

bool TransferProgressPublisher::isActive(TransferState state) const noexcept
{
  return state == TransferState::Transferring || state == TransferState::Resuming;
}

ProgressPublishResult TransferProgressPublisher::fail(ProgressPublishError error, QString diagnostic) const
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

} // namespace relaydesk::transfer
