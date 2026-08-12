// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/filetransfer/PathPolicy.h"

#include <QDir>
#include <QRegularExpression>

#include <utility>

namespace relaydesk::filetransfer {
namespace {

PathValidationResult fail(const PathError error, QString diagnostic)
{
    return {
        .ok = false,
        .error = error,
        .normalized = {},
        .diagnostic = std::move(diagnostic),
    };
}

bool hasControlCharacter(const QString& value)
{
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (code < 0x20U || code == 0x7FU || code == 0U) {
            return true;
        }
    }
    return false;
}

bool isWindowsReservedName(const QString& component)
{
    const QString stem = component.section(u'.', 0, 0).toUpper();
    if (stem == QStringLiteral("CON") ||
        stem == QStringLiteral("PRN") ||
        stem == QStringLiteral("AUX") ||
        stem == QStringLiteral("NUL")) {
        return true;
    }

    static const QRegularExpression numberedDevice(
        QStringLiteral("^(COM|LPT)[1-9]$"),
        QRegularExpression::CaseInsensitiveOption);
    return numberedDevice.match(stem).hasMatch();
}

QString comparablePath(QString path, const TargetPlatform platform)
{
    path = QDir::fromNativeSeparators(QDir::cleanPath(std::move(path)));
    if (platform == TargetPlatform::Windows) {
        path = path.toCaseFolded();
    }
    return path;
}

} // namespace

PathValidationResult PathPolicy::validateRelative(
    const QString& path,
    const TargetPlatform platform,
    const PathLimits& limits)
{
    if (path.isEmpty()) {
        return fail(PathError::Empty, QStringLiteral("Path is empty"));
    }

    if (path.contains(u'\\')) {
        return fail(
            PathError::InvalidSeparator,
            QStringLiteral("Protocol paths must use '/' separators"));
    }

    static const QRegularExpression windowsDriveAbsolute(
        QStringLiteral("^[A-Za-z]:/"));
    if (path.startsWith(u'/') || QDir::isAbsolutePath(path) ||
        windowsDriveAbsolute.match(path).hasMatch()) {
        return fail(
            PathError::Absolute,
            QStringLiteral("Absolute paths are not allowed"));
    }

    const QString normalized =
        path.normalized(QString::NormalizationForm_C);

    if (hasControlCharacter(normalized)) {
        return fail(
            PathError::InvalidUnicodeOrControl,
            QStringLiteral("Path contains NUL or control characters"));
    }

    const QByteArray fullUtf8 = normalized.toUtf8();
    if (fullUtf8.size() > limits.maxUtf8Bytes) {
        return fail(
            PathError::PathTooLong,
            QStringLiteral("UTF-8 path exceeds configured limit"));
    }

    const QStringList components =
        normalized.split(u'/', Qt::KeepEmptyParts);

    if (components.size() > limits.maxDepth) {
        return fail(
            PathError::TooDeep,
            QStringLiteral("Path depth exceeds configured limit"));
    }

    static const QString windowsInvalid = QStringLiteral("<>:\"|?*");

    for (const QString& component : components) {
        if (component.isEmpty()) {
            return fail(
                PathError::EmptyComponent,
                QStringLiteral("Path has an empty component"));
        }
        if (component == QStringLiteral(".")) {
            return fail(
                PathError::DotComponent,
                QStringLiteral("'.' path components are not allowed"));
        }
        if (component == QStringLiteral("..")) {
            return fail(
                PathError::ParentTraversal,
                QStringLiteral("'..' path components are not allowed"));
        }
        if (component.toUtf8().size() > limits.maxComponentUtf8Bytes) {
            return fail(
                PathError::ComponentTooLong,
                QStringLiteral("Path component exceeds configured limit"));
        }
        if (hasControlCharacter(component)) {
            return fail(
                PathError::InvalidUnicodeOrControl,
                QStringLiteral("Path component contains control characters"));
        }

        const bool windowsRules =
            platform == TargetPlatform::Windows ||
            platform == TargetPlatform::Portable;

        if (windowsRules) {
            for (const QChar ch : component) {
                if (windowsInvalid.contains(ch)) {
                    return fail(
                        PathError::InvalidCharacter,
                        QStringLiteral("Path contains a Windows-invalid character"));
                }
            }
            if (component.endsWith(u'.') || component.endsWith(u' ')) {
                return fail(
                    PathError::WindowsTrailingDotOrSpace,
                    QStringLiteral("Windows names cannot end in dot or space"));
            }
            if (isWindowsReservedName(component)) {
                return fail(
                    PathError::WindowsReservedName,
                    QStringLiteral("Windows reserved device name"));
            }
        }

        if (platform == TargetPlatform::MacOS && component.contains(u':')) {
            // Keep Finder-facing behavior predictable even though POSIX APIs can
            // represent more names than Finder exposes consistently.
            return fail(
                PathError::InvalidCharacter,
                QStringLiteral("':' is not accepted by the macOS product policy"));
        }
    }

    return {
        .ok = true,
        .error = PathError::None,
        .normalized = normalized,
        .diagnostic = {},
    };
}

PathValidationResult PathPolicy::joinLexicallyUnderRoot(
    const QString& absoluteRoot,
    const QString& relativePath,
    const TargetPlatform platform,
    QString& outputAbsolutePath,
    const PathLimits& limits)
{
    outputAbsolutePath.clear();

    if (!QDir::isAbsolutePath(absoluteRoot)) {
        return fail(
            PathError::RootNotAbsolute,
            QStringLiteral("Receive root must be absolute"));
    }

    PathValidationResult validated =
        validateRelative(relativePath, platform, limits);
    if (!validated.ok) {
        return validated;
    }

    const QString cleanedRoot = QDir::cleanPath(absoluteRoot);
    const QString candidate =
        QDir::cleanPath(QDir(cleanedRoot).absoluteFilePath(validated.normalized));

    const QString rootComparable = comparablePath(cleanedRoot, platform);
    const QString candidateComparable = comparablePath(candidate, platform);

    QString rootWithSeparator = rootComparable;
    if (!rootWithSeparator.endsWith(u'/')) {
        rootWithSeparator += u'/';
    }

    if (candidateComparable != rootComparable &&
        !candidateComparable.startsWith(rootWithSeparator)) {
        return fail(
            PathError::EscapesRoot,
            QStringLiteral("Path escapes receive root"));
    }

    outputAbsolutePath = candidate;
    return validated;
}

} // namespace relaydesk::filetransfer
