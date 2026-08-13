/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDir>
#include <QMetaType>
#include <QString>

namespace deskflow::relaydesk {

enum class FileSafetyError : int
{
  None = 0,
  InvalidRequest = 1,
  ReceiveRootUnavailable = 2,
  ReceiveRootNotDirectory = 3,
  LinkTraversalDetected = 4,
  StagingFileUnavailable = 5,
  DestinationInvalid = 6,
  DestinationExists = 7,
  CommitFailed = 8,
};

enum class CommitDisposition : int
{
  FailIfExists = 0,
  ReplaceExisting = 1,
};

struct VerifyReceiveRootRequest
{
  QString receiveRoot;

  // Shape validation only. The adapter must still inspect the real filesystem.
  [[nodiscard]] bool isStructurallyValid() const
  {
    return !receiveRoot.isEmpty() && QDir::isAbsolutePath(receiveRoot);
  }

  bool operator==(const VerifyReceiveRootRequest &) const = default;
};

struct VerifyNoLinkTraversalRequest
{
  QString receiveRoot;
  QString candidatePath;

  // PathPolicy owns lexical containment. This helper only validates the
  // platform adapter's absolute-path precondition.
  [[nodiscard]] bool isStructurallyValid() const
  {
    return !receiveRoot.isEmpty() && QDir::isAbsolutePath(receiveRoot) && !candidatePath.isEmpty() &&
           QDir::isAbsolutePath(candidatePath);
  }

  bool operator==(const VerifyNoLinkTraversalRequest &) const = default;
};

struct CommitStagedFileRequest
{
  QString receiveRoot;
  QString stagingPath;
  QString destinationPath;
  CommitDisposition disposition = CommitDisposition::FailIfExists;

  // Shape validation only. Link/reparse checks and commit atomicity remain the
  // responsibility of the platform implementation.
  [[nodiscard]] bool isStructurallyValid() const
  {
    const bool knownDisposition = disposition == CommitDisposition::FailIfExists ||
                                  disposition == CommitDisposition::ReplaceExisting;
    return knownDisposition && !receiveRoot.isEmpty() && QDir::isAbsolutePath(receiveRoot) &&
           !stagingPath.isEmpty() && QDir::isAbsolutePath(stagingPath) && !destinationPath.isEmpty() &&
           QDir::isAbsolutePath(destinationPath) && stagingPath != destinationPath;
  }

  bool operator==(const CommitStagedFileRequest &) const = default;
};

struct FileSafetyResult
{
  FileSafetyError error = FileSafetyError::None;
  // Platform-private diagnostic for logs and tests; never user-visible text.
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == FileSafetyError::None;
  }

  bool operator==(const FileSafetyResult &) const = default;
};

/**
 * Worker-side filesystem boundary for Windows/macOS adapters.
 *
 * Shared PathPolicy must validate protocol paths and lexical containment before
 * these calls. Implementations perform real filesystem inspection and the
 * final platform-appropriate atomic move/replace on a disk worker, never in a
 * network callback. FileReceiver is intentionally NOT_WIRED to this contract.
 */
class IPlatformFileSafety
{
public:
  virtual ~IPlatformFileSafety() = default;

  [[nodiscard]] virtual FileSafetyResult verifyReceiveRoot(const VerifyReceiveRootRequest &request) const = 0;
  [[nodiscard]] virtual FileSafetyResult
  verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &request) const = 0;
  [[nodiscard]] virtual FileSafetyResult commitStagedFile(const CommitStagedFileRequest &request) = 0;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::FileSafetyError)
Q_DECLARE_METATYPE(deskflow::relaydesk::CommitDisposition)
Q_DECLARE_METATYPE(deskflow::relaydesk::VerifyReceiveRootRequest)
Q_DECLARE_METATYPE(deskflow::relaydesk::VerifyNoLinkTraversalRequest)
Q_DECLARE_METATYPE(deskflow::relaydesk::CommitStagedFileRequest)
Q_DECLARE_METATYPE(deskflow::relaydesk::FileSafetyResult)
