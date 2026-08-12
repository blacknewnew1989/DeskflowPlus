// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/PathPolicy.h"
#include "relaydesk/transfer/Protocol.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QUuid>

#include <variant>

namespace relaydesk::transfer {

inline constexpr quint32 kDefaultConflictRenameAttempts = 10'000;

enum class ConflictResolverError
{
  UnsafePath,
  UnsupportedPolicy,
  CandidateExhausted,
  InvalidReservation,
};

struct UseTarget
{
  QString absolutePath;
  QString relativeProtocolPath;
  QUuid reservationId;

  [[nodiscard]] bool operator==(const UseTarget &) const = default;
};

struct SkipTarget
{
  QString absolutePath;

  [[nodiscard]] bool operator==(const SkipTarget &) const = default;
};

struct AskTarget
{
  QUuid conflictId;
  QString absolutePath;

  [[nodiscard]] bool operator==(const AskTarget &) const = default;
};

struct ConflictFailure
{
  ConflictResolverError error = ConflictResolverError::UnsafePath;
  PathError pathError = PathError::None;
  QString diagnostic;

  [[nodiscard]] bool operator==(const ConflictFailure &) const = default;
};

using ConflictDecision = std::variant<UseTarget, SkipTarget, AskTarget, ConflictFailure>;

struct ConflictResolveRequest
{
  QString targetRoot;
  QString relativeProtocolPath;
  ConflictPolicy policy = ConflictPolicy::AutoRename;
  PathLimits pathLimits;
  quint32 maximumRenameAttempts = kDefaultConflictRenameAttempts;
};

class ConflictResolver final
{
public:
  [[nodiscard]] ConflictDecision resolve(const ConflictResolveRequest &request);

  // Retry is used when an external writer creates the reserved target before
  // staging commit. Releasing and selecting the next candidate is one locked
  // operation, so another in-process transfer cannot steal the gap.
  [[nodiscard]] ConflictDecision retry(const ConflictResolveRequest &request, const QUuid &reservationId);
  [[nodiscard]] bool release(const QUuid &reservationId);

  [[nodiscard]] qsizetype reservationCount() const;

private:
  [[nodiscard]] ConflictDecision resolveLocked(const ConflictResolveRequest &request);

  mutable QMutex m_mutex;
  QHash<QUuid, QString> m_reservations;
};

} // namespace relaydesk::transfer
