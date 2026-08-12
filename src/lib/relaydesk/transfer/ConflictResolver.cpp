// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ConflictResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include <utility>

namespace relaydesk::transfer {
namespace {

ConflictDecision fail(ConflictResolverError error, QString diagnostic, PathError pathError = PathError::None)
{
  return ConflictFailure{error, pathError, std::move(diagnostic)};
}

QString renamedFileName(const QString &fileName, quint32 index)
{
  if (index == 0) {
    return fileName;
  }
  const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
  const bool hasExtension = dot > 0 && dot + 1 < fileName.size();
  const QString stem = hasExtension ? fileName.first(dot) : fileName;
  const QString extension = hasExtension ? fileName.sliced(dot) : QString{};
  return QStringLiteral("%1 (%2)%3").arg(stem).arg(index).arg(extension);
}

QString renamedRelativePath(const QString &normalized, quint32 index)
{
  const qsizetype slash = normalized.lastIndexOf(QLatin1Char('/'));
  const QString parent = slash < 0 ? QString{} : normalized.first(slash + 1);
  const QString fileName = slash < 0 ? normalized : normalized.sliced(slash + 1);
  return parent + renamedFileName(fileName, index);
}

QString filesystemCollision(const QString &absolutePath)
{
  if (QFileInfo::exists(absolutePath)) {
    return absolutePath;
  }
  const QFileInfo candidate(absolutePath);
  const auto candidateName = PathPolicy::validateRelative(candidate.fileName());
  if (!candidateName.ok) {
    return absolutePath;
  }
  const QDir parent(candidate.absolutePath());
  if (!parent.exists()) {
    return {};
  }
  const QFileInfoList siblings =
      parent.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
  for (const QFileInfo &sibling : siblings) {
    const auto siblingPath = PathPolicy::validateRelative(sibling.fileName());
    if (siblingPath.ok && siblingPath.collisionKey == candidateName.collisionKey) {
      return sibling.absoluteFilePath();
    }
  }
  return {};
}

} // namespace

ConflictDecision ConflictResolver::resolve(const ConflictResolveRequest &request)
{
  const QMutexLocker locker(&m_mutex);
  return resolveLocked(request);
}

ConflictDecision ConflictResolver::retry(const ConflictResolveRequest &request, const QUuid &reservationId)
{
  const QMutexLocker locker(&m_mutex);
  if (reservationId.isNull() || m_reservations.remove(reservationId) != 1) {
    return fail(ConflictResolverError::InvalidReservation, QStringLiteral("conflict reservation is unknown"));
  }
  return resolveLocked(request);
}

bool ConflictResolver::release(const QUuid &reservationId)
{
  const QMutexLocker locker(&m_mutex);
  return !reservationId.isNull() && m_reservations.remove(reservationId) == 1;
}

qsizetype ConflictResolver::reservationCount() const
{
  const QMutexLocker locker(&m_mutex);
  return m_reservations.size();
}

ConflictDecision ConflictResolver::resolveLocked(const ConflictResolveRequest &request)
{
  QString requestedAbsolute;
  const auto requested = PathPolicy::joinLexicallyUnderRoot(
      request.targetRoot, request.relativeProtocolPath, requestedAbsolute, request.pathLimits
  );
  if (!requested.ok) {
    return fail(ConflictResolverError::UnsafePath, requested.diagnostic, requested.error);
  }
  if (request.maximumRenameAttempts == 0) {
    return fail(ConflictResolverError::CandidateExhausted, QStringLiteral("no auto-rename attempts were allowed"));
  }

  const quint32 attempts = request.policy == ConflictPolicy::AutoRename ? request.maximumRenameAttempts : 1;
  for (quint32 index = 0; index < attempts; ++index) {
    const QString relativeCandidate = renamedRelativePath(requested.normalized, index);
    QString absoluteCandidate;
    const auto candidate = PathPolicy::joinLexicallyUnderRoot(
        request.targetRoot, relativeCandidate, absoluteCandidate, request.pathLimits
    );
    if (!candidate.ok) {
      if (candidate.error == PathError::ComponentTooLong || candidate.error == PathError::PathTooLong) {
        continue;
      }
      return fail(ConflictResolverError::UnsafePath, candidate.diagnostic, candidate.error);
    }
    const QString reservationKey =
        QDir::cleanPath(request.targetRoot).normalized(QString::NormalizationForm_C).toCaseFolded() + QLatin1Char('/') +
        candidate.collisionKey;
    const QString existingPath = filesystemCollision(absoluteCandidate);
    const bool reserved = m_reservations.values().contains(reservationKey);
    if (request.policy == ConflictPolicy::Skip && (!existingPath.isEmpty() || reserved)) {
      return SkipTarget{existingPath.isEmpty() ? absoluteCandidate : existingPath};
    }
    if (request.policy == ConflictPolicy::Ask && (!existingPath.isEmpty() || reserved)) {
      return AskTarget{QUuid::createUuid(), existingPath.isEmpty() ? absoluteCandidate : existingPath};
    }
    if (request.policy == ConflictPolicy::Overwrite && reserved) {
      return fail(
          ConflictResolverError::TargetReserved,
          QStringLiteral("another in-process transfer already reserved the overwrite target")
      );
    }
    if (request.policy == ConflictPolicy::Overwrite && !existingPath.isEmpty() && QFileInfo(existingPath).isDir()) {
      return fail(
          ConflictResolverError::UnsupportedPolicy,
          QStringLiteral("CONFLICT-002 does not overwrite an existing directory")
      );
    }
    if (request.policy == ConflictPolicy::AutoRename && (!existingPath.isEmpty() || reserved)) {
      continue;
    }
    const QUuid reservationId = QUuid::createUuid();
    m_reservations.insert(reservationId, reservationKey);
    return UseTarget{
        request.policy == ConflictPolicy::Overwrite && !existingPath.isEmpty() ? existingPath : absoluteCandidate,
        candidate.normalized,
        reservationId,
        request.policy == ConflictPolicy::Overwrite && !existingPath.isEmpty(),
    };
  }
  return fail(
      ConflictResolverError::CandidateExhausted, QStringLiteral("no available auto-rename target could be reserved")
  );
}

} // namespace relaydesk::transfer
