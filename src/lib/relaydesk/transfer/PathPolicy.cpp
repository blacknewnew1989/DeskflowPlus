// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/PathPolicy.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace relaydesk::transfer {
namespace {

PathValidationResult fail(PathError error, QString diagnostic)
{
  return {
      .ok = false,
      .error = error,
      .normalized = {},
      .collisionKey = {},
      .diagnostic = std::move(diagnostic),
  };
}

bool hasInvalidUnicodeOrControl(const QString &value)
{
  for (qsizetype index = 0; index < value.size(); ++index) {
    const QChar character = value.at(index);
    if (character.isHighSurrogate()) {
      if (index + 1 >= value.size() || !value.at(index + 1).isLowSurrogate()) {
        return true;
      }
      ++index;
      continue;
    }
    if (character.isLowSurrogate() || character.category() == QChar::Other_Control) {
      return true;
    }
  }
  return false;
}

bool hasDrivePrefix(const QString &path)
{
  static const QRegularExpression drivePrefix(QStringLiteral("^[A-Za-z]:"));
  return drivePrefix.match(path).hasMatch();
}

bool isWindowsReservedName(const QString &component)
{
  QString stem = component.section(u'.', 0, 0);
  while (stem.endsWith(u'.') || stem.endsWith(u' ')) {
    stem.chop(1);
  }
  stem = stem.toUpper();
  if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN") || stem == QStringLiteral("AUX") ||
      stem == QStringLiteral("NUL")) {
    return true;
  }

  static const QRegularExpression numberedDevice(
      QStringLiteral("^(COM|LPT)([1-9]|[\u00b9\u00b2\u00b3])$"), QRegularExpression::CaseInsensitiveOption
  );
  return numberedDevice.match(stem).hasMatch();
}

QString collisionKeyForProtocolPath(const QString &normalized)
{
  return normalized.toCaseFolded().normalized(QString::NormalizationForm_C);
}

QString comparableLocalPath(QString path)
{
  path = QDir::fromNativeSeparators(QDir::cleanPath(std::move(path)));
  return collisionKeyForProtocolPath(path);
}

} // namespace

PathValidationResult PathPolicy::validateRelative(const QString &path, const PathLimits &limits)
{
  if (path.isEmpty()) {
    return fail(PathError::Empty, QStringLiteral("protocol path is empty"));
  }
  if (path.startsWith(QStringLiteral("//")) || path.startsWith(QStringLiteral("\\\\")) || path.startsWith(u'/') ||
      QDir::isAbsolutePath(path) || hasDrivePrefix(path)) {
    return fail(PathError::Absolute, QStringLiteral("absolute, UNC, and drive paths are not allowed"));
  }
  if (path.contains(u'\\')) {
    return fail(PathError::InvalidSeparator, QStringLiteral("protocol paths must use '/' separators"));
  }
  if (hasInvalidUnicodeOrControl(path)) {
    return fail(
        PathError::InvalidUnicodeOrControl, QStringLiteral("path contains invalid Unicode, NUL, or control characters")
    );
  }

  const QString normalized = path.normalized(QString::NormalizationForm_C);
  const QByteArray fullUtf8 = normalized.toUtf8();
  if (static_cast<quint64>(fullUtf8.size()) > limits.maxUtf8Bytes) {
    return fail(PathError::PathTooLong, QStringLiteral("UTF-8 protocol path exceeds the configured limit"));
  }

  const QStringList components = normalized.split(u'/', Qt::KeepEmptyParts);
  if (static_cast<quint64>(components.size()) > limits.maxDepth) {
    return fail(PathError::TooDeep, QStringLiteral("protocol path depth exceeds the configured limit"));
  }

  static const QString windowsInvalidCharacters = QStringLiteral("<>:\"|?*");
  for (const QString &component : components) {
    if (component.isEmpty()) {
      return fail(PathError::EmptyComponent, QStringLiteral("protocol path contains an empty component"));
    }
    if (component == QStringLiteral(".")) {
      return fail(PathError::DotComponent, QStringLiteral("'.' path components are not allowed"));
    }
    if (component == QStringLiteral("..")) {
      return fail(PathError::ParentTraversal, QStringLiteral("'..' path components are not allowed"));
    }
    if (static_cast<quint64>(component.toUtf8().size()) > limits.maxComponentUtf8Bytes) {
      return fail(PathError::ComponentTooLong, QStringLiteral("UTF-8 path component exceeds the configured limit"));
    }
    if (hasInvalidUnicodeOrControl(component)) {
      return fail(
          PathError::InvalidUnicodeOrControl,
          QStringLiteral("path component contains invalid Unicode or control characters")
      );
    }
    for (const QChar character : component) {
      if (windowsInvalidCharacters.contains(character)) {
        return fail(PathError::InvalidCharacter, QStringLiteral("path contains a cross-platform invalid character"));
      }
    }
    if (component.endsWith(u'.') || component.endsWith(u' ')) {
      return fail(
          PathError::WindowsTrailingDotOrSpace, QStringLiteral("portable path components cannot end in dot or space")
      );
    }
    if (isWindowsReservedName(component)) {
      return fail(PathError::WindowsReservedName, QStringLiteral("path contains a Windows reserved device name"));
    }
  }

  return {
      .ok = true,
      .error = PathError::None,
      .normalized = normalized,
      .collisionKey = collisionKeyForProtocolPath(normalized),
      .diagnostic = {},
  };
}

PathValidationResult PathPolicy::joinLexicallyUnderRoot(
    const QString &absoluteRoot, const QString &relativePath, QString &outputAbsolutePath, const PathLimits &limits
)
{
  outputAbsolutePath.clear();
  if (!QDir::isAbsolutePath(absoluteRoot)) {
    return fail(PathError::RootNotAbsolute, QStringLiteral("receive root must be absolute"));
  }

  PathValidationResult validated = validateRelative(relativePath, limits);
  if (!validated.ok) {
    return validated;
  }

  const QString cleanedRoot = QDir::cleanPath(absoluteRoot);
  const QString candidate = QDir::cleanPath(QDir(cleanedRoot).absoluteFilePath(validated.normalized));
  const QString comparableRoot = comparableLocalPath(cleanedRoot);
  const QString comparableCandidate = comparableLocalPath(candidate);
  QString rootWithSeparator = comparableRoot;
  if (!rootWithSeparator.endsWith(u'/')) {
    rootWithSeparator += u'/';
  }

  if (comparableCandidate != comparableRoot && !comparableCandidate.startsWith(rootWithSeparator)) {
    return fail(PathError::EscapesRoot, QStringLiteral("resolved path escapes the receive root"));
  }

  outputAbsolutePath = candidate;
  return validated;
}

EntryPolicyResult PathPolicy::entryPolicy(ManifestEntryKind kind)
{
  switch (kind) {
  case ManifestEntryKind::RegularFile:
  case ManifestEntryKind::Directory:
    return {.disposition = EntryDisposition::Include, .diagnostic = {}};
  case ManifestEntryKind::SymbolicLink:
    return {
        .disposition = EntryDisposition::Skip,
        .diagnostic = QStringLiteral("symbolic links are skipped by the P0 sender policy"),
    };
  case ManifestEntryKind::Special:
    return {
        .disposition = EntryDisposition::Skip,
        .diagnostic = QStringLiteral("special filesystem entries are skipped by the P0 sender policy"),
    };
  }
  return {
      .disposition = EntryDisposition::Skip,
      .diagnostic = QStringLiteral("unknown filesystem entry kind is skipped"),
  };
}

} // namespace relaydesk::transfer
