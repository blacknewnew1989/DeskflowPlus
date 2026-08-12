// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QString>
#include <QtGlobal>

namespace relaydesk::transfer {

enum class PathError
{
  None,
  Empty,
  InvalidUnicodeOrControl,
  Absolute,
  InvalidSeparator,
  EmptyComponent,
  DotComponent,
  ParentTraversal,
  ComponentTooLong,
  PathTooLong,
  TooDeep,
  InvalidCharacter,
  WindowsReservedName,
  WindowsTrailingDotOrSpace,
  RootNotAbsolute,
  EscapesRoot,
};

struct PathLimits
{
  quint32 maxUtf8Bytes = 4096;
  quint32 maxComponentUtf8Bytes = 255;
  quint32 maxDepth = 128;
};

struct PathValidationResult
{
  bool ok = false;
  PathError error = PathError::None;
  QString normalized;
  // NFC + Unicode case folding. ConflictResolver uses this conservative,
  // platform-neutral key while preserving normalized for display and wire use.
  QString collisionKey;
  QString diagnostic;
};

enum class ManifestEntryKind
{
  RegularFile,
  Directory,
  SymbolicLink,
  Special,
};

enum class EntryDisposition
{
  Include,
  Skip,
};

struct EntryPolicyResult
{
  EntryDisposition disposition = EntryDisposition::Skip;
  QString diagnostic;
};

class PathPolicy final
{
public:
  // Protocol paths are one portable format: relative, '/' separated, NFC,
  // and valid on both Windows and macOS. Platform adapters may impose fewer
  // rules locally, but must not weaken this shared wire policy.
  [[nodiscard]] static PathValidationResult validateRelative(const QString &path, const PathLimits &limits = {});

  // P0 lexical containment for an app-managed receive root. This deliberately
  // does not attempt handle-by-handle anti-TOCTOU hardening.
  [[nodiscard]] static PathValidationResult joinLexicallyUnderRoot(
      const QString &absoluteRoot, const QString &relativePath, QString &outputAbsolutePath,
      const PathLimits &limits = {}
  );

  // Sender-side links and special files are reported and skipped in P0.
  [[nodiscard]] static EntryPolicyResult entryPolicy(ManifestEntryKind kind);
};

} // namespace relaydesk::transfer
