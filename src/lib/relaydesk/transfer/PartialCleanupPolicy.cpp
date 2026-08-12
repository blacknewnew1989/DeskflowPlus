// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/PartialCleanupPolicy.h"

#include "relaydesk/transfer/PathPolicy.h"

#include <QDir>
#include <QFileInfo>

#include <limits>
#include <utility>

namespace relaydesk::transfer {

PartialCleanupPolicy::PartialCleanupPolicy(const ResumeStore &store, PartialCleanupSettings settings)
    : m_store(store),
      m_settings(std::move(settings))
{
}

PartialCleanupListing PartialCleanupPolicy::listExpired(QDateTime nowUtc) const
{
  if (!nowUtc.isValid() || nowUtc.toMSecsSinceEpoch() <= 0 || !QDir::isAbsolutePath(m_settings.stagingRoot) ||
      m_settings.retention.count() < 0 || m_settings.retention.count() > std::numeric_limits<qint64>::max() / 1000) {
    return {
        .error = PartialCleanupError::InvalidConfiguration,
        .diagnostic = QStringLiteral("partial cleanup time or staging root is invalid"),
    };
  }
  const auto scanned = m_store.scan();
  if (!scanned.ok()) {
    return {
        .error = PartialCleanupError::ScanFailed,
        .diagnostic = scanned.diagnostic,
    };
  }

  PartialCleanupListing listing;
  for (const auto &issue : scanned.issues) {
    listing.issues.append({
        .path = issue.path,
        .error = PartialCleanupIssueError::ResumeStoreIssue,
        .diagnostic = issue.diagnostic,
    });
  }
  const qint64 cutoffMs = nowUtc.toUTC().toMSecsSinceEpoch() - m_settings.retention.count() * 1000;
  for (const ResumeState &state : scanned.states) {
    if (state.updatedUtc.toUTC().toMSecsSinceEpoch() > cutoffMs) {
      continue;
    }
    ExpiredPartialTransfer expired{.transferId = state.transferId, .updatedUtc = state.updatedUtc.toUTC()};
    bool safeAndConsistent = true;
    for (const ResumeFileState &file : state.files) {
      QString partAbsolutePath;
      const auto path = PathPolicy::joinLexicallyUnderRoot(
          m_settings.stagingRoot, file.partRelativePath, partAbsolutePath, m_settings.pathLimits
      );
      if (!path.ok || !file.partRelativePath.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)) {
        listing.issues.append({
            .transferId = state.transferId,
            .path = file.partRelativePath,
            .error = PartialCleanupIssueError::UnsafePartPath,
            .diagnostic = path.ok ? QStringLiteral("resume state references a non-part staging file") : path.diagnostic,
        });
        safeAndConsistent = false;
        continue;
      }
      QFileInfo info(partAbsolutePath);
      info.refresh();
      if (!info.exists() || info.isSymLink() || !info.isFile()) {
        listing.issues.append({
            .transferId = state.transferId,
            .path = partAbsolutePath,
            .error = PartialCleanupIssueError::MissingPart,
            .diagnostic = QStringLiteral("resume state staging file is missing or not a regular file"),
        });
        safeAndConsistent = false;
        continue;
      }
      if (info.size() < 0 || static_cast<quint64>(info.size()) != file.durableOffset) {
        listing.issues.append({
            .transferId = state.transferId,
            .path = partAbsolutePath,
            .error = PartialCleanupIssueError::PartSizeMismatch,
            .diagnostic = QStringLiteral("staging size does not match the durable resume offset"),
        });
        safeAndConsistent = false;
        continue;
      }
      expired.files.append({
          .fileId = file.fileId,
          .relativeProtocolPath = file.relativeProtocolPath,
          .partAbsolutePath = partAbsolutePath,
          .durableOffset = file.durableOffset,
          .totalBytes = file.totalBytes,
      });
    }
    if (safeAndConsistent && !expired.files.isEmpty()) {
      listing.expired.append(std::move(expired));
    }
  }
  return listing;
}

} // namespace relaydesk::transfer
