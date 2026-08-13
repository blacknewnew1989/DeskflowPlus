/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacFileSafety.h"

#include <QDir>
#include <QFile>
#include <QStringList>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/stdio.h>
#include <unistd.h>

#include <utility>

namespace deskflow::relaydesk {
namespace {

class FileDescriptor final
{
public:
  FileDescriptor() noexcept = default;

  explicit FileDescriptor(int value) noexcept : m_value(value)
  {
  }

  ~FileDescriptor()
  {
    if (m_value >= 0)
      ::close(m_value);
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept : m_value(std::exchange(other.m_value, -1))
  {
  }

  FileDescriptor &operator=(FileDescriptor &&other) noexcept
  {
    if (this == &other)
      return *this;
    if (m_value >= 0)
      ::close(m_value);
    m_value = std::exchange(other.m_value, -1);
    return *this;
  }

  [[nodiscard]] int get() const noexcept
  {
    return m_value;
  }

private:
  int m_value = -1;
};

struct RootHandle
{
  QString cleanPath;
  FileDescriptor descriptor;
};

struct RelativePath
{
  QStringList components;
};

struct ParentHandle
{
  FileDescriptor descriptor;
  QByteArray leafName;
};

FileSafetyResult failure(FileSafetyError error, const QString &diagnostic)
{
  return {.error = error, .diagnostic = diagnostic};
}

FileSafetyResult errnoFailure(FileSafetyError error, const QString &operation, int nativeError)
{
  return failure(
      error,
      QStringLiteral("%1 failed with errno %2: %3")
          .arg(operation)
          .arg(nativeError)
          .arg(QString::fromLocal8Bit(std::strerror(nativeError)))
  );
}

FileSafetyError rootOpenError(int nativeError)
{
  switch (nativeError) {
  case ELOOP:
    return FileSafetyError::LinkTraversalDetected;
  case ENOTDIR:
    return FileSafetyError::ReceiveRootNotDirectory;
  default:
    return FileSafetyError::ReceiveRootUnavailable;
  }
}

bool containsEmbeddedNull(const QString &path)
{
  return path.contains(QChar::Null);
}

std::pair<RootHandle, FileSafetyResult> openRoot(const QString &path)
{
  if (containsEmbeddedNull(path)) {
    return {
        RootHandle{},
        failure(FileSafetyError::InvalidRequest, QStringLiteral("receive root contains an embedded NUL")),
    };
  }
  const QString cleanPath = QDir::cleanPath(path);
  const QByteArray encodedPath = QFile::encodeName(cleanPath);
  struct stat entry{};
  if (::lstat(encodedPath.constData(), &entry) != 0) {
    const int nativeError = errno;
    return {RootHandle{}, errnoFailure(rootOpenError(nativeError), QStringLiteral("lstat receive root"), nativeError)};
  }
  if (S_ISLNK(entry.st_mode)) {
    return {
        RootHandle{},
        failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("receive root is a symbolic link")),
    };
  }
  if (!S_ISDIR(entry.st_mode)) {
    return {
        RootHandle{},
        failure(FileSafetyError::ReceiveRootNotDirectory, QStringLiteral("receive root is not a directory")),
    };
  }

  const int descriptor = ::open(encodedPath.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) {
    const int nativeError = errno;
    return {RootHandle{}, errnoFailure(rootOpenError(nativeError), QStringLiteral("open receive root"), nativeError)};
  }
  return {RootHandle{cleanPath, FileDescriptor(descriptor)}, FileSafetyResult{}};
}

std::pair<RelativePath, FileSafetyResult> relativePath(const QString &root, const QString &candidate)
{
  if (containsEmbeddedNull(root) || containsEmbeddedNull(candidate)) {
    return {
        RelativePath{},
        failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate path contains an embedded NUL")),
    };
  }
  const QString relative = QDir(root).relativeFilePath(QDir::cleanPath(candidate));
  if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") ||
      relative.startsWith(QStringLiteral("../"))) {
    return {
        RelativePath{},
        failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate is outside receive root")),
    };
  }

  if (relative == QStringLiteral("."))
    return {RelativePath{}, FileSafetyResult{}};

  const QStringList components = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const QString &component : components) {
    if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
      return {
          RelativePath{},
          failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate contains dot traversal")),
      };
    }
  }
  if (components.isEmpty()) {
    return {
        RelativePath{}, failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate path is empty"))};
  }
  return {RelativePath{components}, FileSafetyResult{}};
}

FileSafetyResult inspectExistingPath(int rootDescriptor, const QStringList &components)
{
  const int duplicate = ::fcntl(rootDescriptor, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) {
    const int nativeError = errno;
    return errnoFailure(FileSafetyError::DestinationInvalid, QStringLiteral("duplicate receive root"), nativeError);
  }
  FileDescriptor current(duplicate);

  for (qsizetype index = 0; index < components.size(); ++index) {
    const QByteArray name = QFile::encodeName(components.at(index));
    struct stat entry{};
    if (::fstatat(current.get(), name.constData(), &entry, AT_SYMLINK_NOFOLLOW) != 0) {
      const int nativeError = errno;
      if (nativeError == ENOENT)
        return {};
      if (nativeError == ELOOP)
        return errnoFailure(FileSafetyError::LinkTraversalDetected, QStringLiteral("inspect candidate"), nativeError);
      return errnoFailure(FileSafetyError::DestinationInvalid, QStringLiteral("inspect candidate"), nativeError);
    }
    if (S_ISLNK(entry.st_mode)) {
      return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("candidate traverses a symbolic link"));
    }
    if (index + 1 == components.size())
      return {};
    if (!S_ISDIR(entry.st_mode)) {
      return failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate parent is not a directory"));
    }

    const int next = ::openat(current.get(), name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0) {
      const int nativeError = errno;
      const FileSafetyError error = nativeError == ELOOP ? FileSafetyError::LinkTraversalDetected
                                                         : FileSafetyError::DestinationInvalid;
      return errnoFailure(error, QStringLiteral("open candidate parent"), nativeError);
    }
    current = FileDescriptor(next);
  }
  return {};
}

std::pair<ParentHandle, FileSafetyResult>
openParent(int rootDescriptor, const QStringList &components, FileSafetyError unavailableError)
{
  if (components.isEmpty()) {
    return {
        ParentHandle{},
        failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate cannot equal receive root")),
    };
  }

  const int duplicate = ::fcntl(rootDescriptor, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) {
    const int nativeError = errno;
    return {ParentHandle{}, errnoFailure(unavailableError, QStringLiteral("duplicate receive root"), nativeError)};
  }
  FileDescriptor current(duplicate);

  for (qsizetype index = 0; index + 1 < components.size(); ++index) {
    const QByteArray name = QFile::encodeName(components.at(index));
    struct stat entry{};
    if (::fstatat(current.get(), name.constData(), &entry, AT_SYMLINK_NOFOLLOW) != 0) {
      const int nativeError = errno;
      const FileSafetyError error = nativeError == ELOOP ? FileSafetyError::LinkTraversalDetected : unavailableError;
      return {ParentHandle{}, errnoFailure(error, QStringLiteral("inspect commit parent"), nativeError)};
    }
    if (S_ISLNK(entry.st_mode)) {
      return {
          ParentHandle{},
          failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("commit parent is a symbolic link")),
      };
    }
    if (!S_ISDIR(entry.st_mode)) {
      return {ParentHandle{}, failure(unavailableError, QStringLiteral("commit parent is not a directory"))};
    }

    const int next = ::openat(current.get(), name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0) {
      const int nativeError = errno;
      const FileSafetyError error = nativeError == ELOOP ? FileSafetyError::LinkTraversalDetected : unavailableError;
      return {ParentHandle{}, errnoFailure(error, QStringLiteral("open commit parent"), nativeError)};
    }
    current = FileDescriptor(next);
  }

  return {
      ParentHandle{std::move(current), QFile::encodeName(components.constLast())}, FileSafetyResult{}};
}

FileSafetyResult inspectStaging(const ParentHandle &parent, struct stat &entry)
{
  if (::fstatat(parent.descriptor.get(), parent.leafName.constData(), &entry, AT_SYMLINK_NOFOLLOW) != 0) {
    const int nativeError = errno;
    const FileSafetyError error = nativeError == ELOOP ? FileSafetyError::LinkTraversalDetected
                                                       : FileSafetyError::StagingFileUnavailable;
    return errnoFailure(error, QStringLiteral("inspect staging file"), nativeError);
  }
  if (S_ISLNK(entry.st_mode)) {
    return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("staging file is a symbolic link"));
  }
  if (!S_ISREG(entry.st_mode)) {
    return failure(FileSafetyError::StagingFileUnavailable, QStringLiteral("staging path is not a regular file"));
  }
  return {};
}

FileSafetyResult inspectDestination(const ParentHandle &parent, bool &exists, struct stat &entry)
{
  if (::fstatat(parent.descriptor.get(), parent.leafName.constData(), &entry, AT_SYMLINK_NOFOLLOW) != 0) {
    const int nativeError = errno;
    if (nativeError == ENOENT) {
      exists = false;
      return {};
    }
    const FileSafetyError error = nativeError == ELOOP ? FileSafetyError::LinkTraversalDetected
                                                       : FileSafetyError::DestinationInvalid;
    return errnoFailure(error, QStringLiteral("inspect destination"), nativeError);
  }
  exists = true;
  if (S_ISLNK(entry.st_mode)) {
    return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("destination is a symbolic link"));
  }
  if (!S_ISREG(entry.st_mode)) {
    return failure(FileSafetyError::DestinationInvalid, QStringLiteral("destination is not a regular file"));
  }
  return {};
}

} // namespace

FileSafetyResult MacFileSafety::verifyReceiveRoot(const VerifyReceiveRootRequest &request) const
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("receive root request is structurally invalid"));
  const auto [root, result] = openRoot(request.receiveRoot);
  return result;
}

FileSafetyResult MacFileSafety::verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &request) const
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("link traversal request is structurally invalid"));

  auto [root, rootResult] = openRoot(request.receiveRoot);
  if (!rootResult.ok())
    return rootResult;
  auto [candidate, candidateResult] = relativePath(root.cleanPath, request.candidatePath);
  if (!candidateResult.ok())
    return candidateResult;
  return inspectExistingPath(root.descriptor.get(), candidate.components);
}

FileSafetyResult MacFileSafety::commitStagedFile(const CommitStagedFileRequest &request)
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("commit request is structurally invalid"));

  auto [root, rootResult] = openRoot(request.receiveRoot);
  if (!rootResult.ok())
    return rootResult;
  auto [staging, stagingPathResult] = relativePath(root.cleanPath, request.stagingPath);
  if (!stagingPathResult.ok())
    return stagingPathResult;
  auto [destination, destinationPathResult] = relativePath(root.cleanPath, request.destinationPath);
  if (!destinationPathResult.ok())
    return destinationPathResult;

  auto [stagingParent, stagingParentResult] =
      openParent(root.descriptor.get(), staging.components, FileSafetyError::StagingFileUnavailable);
  if (!stagingParentResult.ok())
    return stagingParentResult;
  auto [destinationParent, destinationParentResult] =
      openParent(root.descriptor.get(), destination.components, FileSafetyError::DestinationInvalid);
  if (!destinationParentResult.ok())
    return destinationParentResult;

  struct stat stagingEntry{};
  if (const FileSafetyResult result = inspectStaging(stagingParent, stagingEntry); !result.ok())
    return result;

  bool destinationExists = false;
  struct stat destinationEntry{};
  if (const FileSafetyResult result = inspectDestination(destinationParent, destinationExists, destinationEntry);
      !result.ok()) {
    return result;
  }
  if (destinationExists && stagingEntry.st_dev == destinationEntry.st_dev &&
      stagingEntry.st_ino == destinationEntry.st_ino) {
    return failure(FileSafetyError::DestinationInvalid, QStringLiteral("staging and destination are the same file"));
  }
  if (destinationExists && request.disposition == CommitDisposition::FailIfExists) {
    return failure(FileSafetyError::DestinationExists, QStringLiteral("destination already exists"));
  }

  unsigned int renameFlags = RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH;
  if (request.disposition == CommitDisposition::FailIfExists)
    renameFlags |= RENAME_EXCL;
  const int renameResult = ::renameatx_np(
      stagingParent.descriptor.get(), stagingParent.leafName.constData(), destinationParent.descriptor.get(),
      destinationParent.leafName.constData(), renameFlags
  );
  if (renameResult == 0)
    return {};

  const int nativeError = errno;
  if (nativeError == EEXIST)
    return errnoFailure(FileSafetyError::DestinationExists, QStringLiteral("commit staged file"), nativeError);
  if (nativeError == ENOENT)
    return errnoFailure(FileSafetyError::StagingFileUnavailable, QStringLiteral("commit staged file"), nativeError);
  if (nativeError == ELOOP)
    return errnoFailure(FileSafetyError::LinkTraversalDetected, QStringLiteral("commit staged file"), nativeError);
#ifdef ENOTCAPABLE
  if (nativeError == ENOTCAPABLE)
    return errnoFailure(FileSafetyError::LinkTraversalDetected, QStringLiteral("commit staged file"), nativeError);
#endif
  return errnoFailure(FileSafetyError::CommitFailed, QStringLiteral("commit staged file"), nativeError);
}

} // namespace deskflow::relaydesk
