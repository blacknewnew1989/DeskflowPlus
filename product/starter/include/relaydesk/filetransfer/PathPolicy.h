// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QString>
#include <QStringList>

namespace relaydesk::filetransfer {

enum class TargetPlatform {
    Portable,
    Windows,
    MacOS
};

enum class PathError {
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
    EscapesRoot
};

struct PathLimits {
    qsizetype maxUtf8Bytes = 4096;
    qsizetype maxComponentUtf8Bytes = 255;
    qsizetype maxDepth = 128;
};

struct PathValidationResult {
    bool ok = false;
    PathError error = PathError::None;
    QString normalized;
    QString diagnostic;
};

class PathPolicy final {
public:
    [[nodiscard]] static PathValidationResult validateRelative(
        const QString& path,
        TargetPlatform platform,
        const PathLimits& limits = {});

    // P0 lexical containment for the app-managed receive root. Sender-side
    // link entries are skipped; deeper race-hardening is intentionally out of scope.
    [[nodiscard]] static PathValidationResult joinLexicallyUnderRoot(
        const QString& absoluteRoot,
        const QString& relativePath,
        TargetPlatform platform,
        QString& outputAbsolutePath,
        const PathLimits& limits = {});
};

} // namespace relaydesk::filetransfer
