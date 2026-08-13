/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsFileSafety.h"

#include <QDir>
#include <QStringList>

#include <windows.h>

#include <cstring>
#include <utility>
#include <vector>

namespace deskflow::relaydesk {
namespace {

class NativeHandle final
{
public:
  NativeHandle() noexcept = default;
  explicit NativeHandle(HANDLE value) noexcept : m_value(value)
  {
  }

  ~NativeHandle()
  {
    if (m_value != INVALID_HANDLE_VALUE)
      ::CloseHandle(m_value);
  }

  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;

  NativeHandle(NativeHandle &&other) noexcept : m_value(std::exchange(other.m_value, INVALID_HANDLE_VALUE))
  {
  }

  NativeHandle &operator=(NativeHandle &&other) noexcept
  {
    if (this == &other)
      return *this;
    if (m_value != INVALID_HANDLE_VALUE)
      ::CloseHandle(m_value);
    m_value = std::exchange(other.m_value, INVALID_HANDLE_VALUE);
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept
  {
    return m_value;
  }

  [[nodiscard]] bool valid() const noexcept
  {
    return m_value != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE m_value = INVALID_HANDLE_VALUE;
};

struct OpenEntry
{
  NativeHandle handle;
  DWORD attributes = INVALID_FILE_ATTRIBUTES;
  DWORD nativeError = ERROR_SUCCESS;
};

FileSafetyResult failure(FileSafetyError error, const QString &diagnostic)
{
  return {.error = error, .diagnostic = diagnostic};
}

QString nativeErrorText(DWORD nativeError)
{
  wchar_t *message = nullptr;
  const DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      nativeError, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr
  );
  QString text;
  if (length != 0 && message != nullptr)
    text = QString::fromWCharArray(message, static_cast<qsizetype>(length)).trimmed();
  if (message != nullptr)
    ::LocalFree(message);
  return text;
}

FileSafetyResult nativeFailure(FileSafetyError error, const QString &operation, DWORD nativeError)
{
  return failure(
      error,
      QStringLiteral("%1 failed with Win32 error %2: %3")
          .arg(operation)
          .arg(nativeError)
          .arg(nativeErrorText(nativeError))
  );
}

bool containsEmbeddedNull(const QString &path)
{
  return path.contains(QChar::Null);
}

bool isDeviceNamespace(const QString &path)
{
  return path.startsWith(QStringLiteral("\\\\?\\")) || path.startsWith(QStringLiteral("\\\\.\\"));
}

std::pair<QString, FileSafetyResult>
normalizeAbsolutePath(const QString &path, FileSafetyError invalidError, const QString &role)
{
  if (path.isEmpty() || containsEmbeddedNull(path) || !QDir::isAbsolutePath(path) || isDeviceNamespace(path)) {
    return {
        {}, failure(invalidError, QStringLiteral("%1 is not an accepted absolute Windows path").arg(role))};
  }

  const wchar_t *input = reinterpret_cast<const wchar_t *>(path.utf16());
  const DWORD required = ::GetFullPathNameW(input, 0, nullptr, nullptr);
  if (required == 0)
    return {{}, nativeFailure(invalidError, QStringLiteral("normalize %1").arg(role), ::GetLastError())};

  std::vector<wchar_t> buffer(static_cast<size_t>(required));
  const DWORD written = ::GetFullPathNameW(input, required, buffer.data(), nullptr);
  if (written == 0 || written >= required)
    return {{}, nativeFailure(invalidError, QStringLiteral("normalize %1").arg(role), ::GetLastError())};

  QString normalized = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
  normalized.replace(QLatin1Char('/'), QLatin1Char('\\'));
  while (normalized.size() > 3 && normalized.endsWith(QLatin1Char('\\')))
    normalized.chop(1);
  return {normalized, {}};
}

QString stripExtendedPrefix(QString path)
{
  if (path.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive))
    path = QStringLiteral("\\\\") + path.sliced(8);
  else if (path.startsWith(QStringLiteral("\\\\?\\"), Qt::CaseInsensitive))
    path = path.sliced(4);
  path.replace(QLatin1Char('/'), QLatin1Char('\\'));
  while (path.size() > 3 && path.endsWith(QLatin1Char('\\')))
    path.chop(1);
  return path;
}

std::pair<QString, FileSafetyResult> finalPathForHandle(HANDLE handle, const QString &role)
{
  const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const DWORD required = ::GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
  if (required == 0)
    return {{}, nativeFailure(FileSafetyError::LinkTraversalDetected, role, ::GetLastError())};

  std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1);
  const DWORD written = ::GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
  if (written == 0 || written >= buffer.size())
    return {{}, nativeFailure(FileSafetyError::LinkTraversalDetected, role, ::GetLastError())};
  return {stripExtendedPrefix(QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written))), {}};
}

std::pair<OpenEntry, FileSafetyResult>
openEntry(const QString &path, FileSafetyError unavailableError, const QString &role)
{
  const HANDLE value = ::CreateFileW(
      reinterpret_cast<const wchar_t *>(path.utf16()), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr
  );
  if (value == INVALID_HANDLE_VALUE) {
    const DWORD nativeError = ::GetLastError();
    return {
        OpenEntry{NativeHandle{}, INVALID_FILE_ATTRIBUTES, nativeError},
        nativeFailure(unavailableError, QStringLiteral("open %1").arg(role), nativeError),
    };
  }

  BY_HANDLE_FILE_INFORMATION information{};
  if (!::GetFileInformationByHandle(value, &information)) {
    const DWORD nativeError = ::GetLastError();
    ::CloseHandle(value);
    return {
        OpenEntry{NativeHandle{}, INVALID_FILE_ATTRIBUTES, nativeError},
        nativeFailure(unavailableError, QStringLiteral("inspect %1").arg(role), nativeError),
    };
  }
  return std::make_pair(
      OpenEntry{NativeHandle(value), information.dwFileAttributes, ERROR_SUCCESS}, FileSafetyResult{}
  );
}

FileSafetyError rootOpenError(DWORD nativeError)
{
  switch (nativeError) {
  case ERROR_DIRECTORY:
    return FileSafetyError::ReceiveRootNotDirectory;
  case ERROR_CANT_ACCESS_FILE:
  case ERROR_REPARSE_TAG_INVALID:
  case ERROR_REPARSE_TAG_MISMATCH:
    return FileSafetyError::LinkTraversalDetected;
  default:
    return FileSafetyError::ReceiveRootUnavailable;
  }
}

std::pair<OpenEntry, FileSafetyResult> openRoot(const QString &normalizedRoot)
{
  auto [entry, result] = openEntry(normalizedRoot, FileSafetyError::ReceiveRootUnavailable, QStringLiteral("receive root"));
  if (!result.ok()) {
    const DWORD nativeError = entry.nativeError;
    return {OpenEntry{}, nativeFailure(rootOpenError(nativeError), QStringLiteral("open receive root"), nativeError)};
  }
  if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return {
        OpenEntry{},
        failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("receive root is a reparse point")),
    };
  }
  if ((entry.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return {
        OpenEntry{},
        failure(FileSafetyError::ReceiveRootNotDirectory, QStringLiteral("receive root is not a directory")),
    };
  }

  auto [finalPath, finalResult] = finalPathForHandle(entry.handle.get(), QStringLiteral("resolve receive root"));
  if (!finalResult.ok())
    return {OpenEntry{}, finalResult};
  if (finalPath.compare(normalizedRoot, Qt::CaseInsensitive) != 0) {
    return {
        OpenEntry{}, failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("receive root resolves through a reparse point"))};
  }
  return std::make_pair(std::move(entry), FileSafetyResult{});
}

std::pair<QStringList, FileSafetyResult>
relativeComponents(const QString &normalizedRoot, const QString &normalizedCandidate)
{
  if (normalizedCandidate.compare(normalizedRoot, Qt::CaseInsensitive) == 0)
    return {QStringList{}, {}};

  QString prefix = normalizedRoot;
  if (!prefix.endsWith(QLatin1Char('\\')))
    prefix += QLatin1Char('\\');
  if (!normalizedCandidate.startsWith(prefix, Qt::CaseInsensitive)) {
    return {
        {}, failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate is outside receive root"))};
  }

  const QStringList components = normalizedCandidate.sliced(prefix.size()).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
  if (components.isEmpty())
    return {{}, failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate path is empty"))};
  for (const QString &component : components) {
    if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
      return {{}, failure(FileSafetyError::DestinationInvalid, QStringLiteral("candidate contains dot traversal"))};
    }
  }
  return {components, {}};
}

FileSafetyResult inspectComponents(
    const QString &normalizedRoot, const QStringList &components, bool allowMissingLeaf,
    FileSafetyError unavailableError, const QString &role
)
{
  QString current = normalizedRoot;
  for (qsizetype index = 0; index < components.size(); ++index) {
    if (!current.endsWith(QLatin1Char('\\')))
      current += QLatin1Char('\\');
    current += components.at(index);

    auto [entry, result] = openEntry(current, unavailableError, role);
    if (!result.ok()) {
      const DWORD nativeError = entry.nativeError;
      const bool missing = nativeError == ERROR_FILE_NOT_FOUND || nativeError == ERROR_PATH_NOT_FOUND;
      if (missing && allowMissingLeaf && index + 1 == components.size())
        return {};
      return result;
    }
    if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("%1 traverses a reparse point").arg(role));
    if (index + 1 < components.size() && (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
      return failure(FileSafetyError::DestinationInvalid, QStringLiteral("%1 parent is not a directory").arg(role));
  }
  return {};
}

std::pair<QString, FileSafetyResult> parentPath(const QString &path, const QString &role)
{
  const qsizetype separator = path.lastIndexOf(QLatin1Char('\\'));
  if (separator <= 0)
    return {{}, failure(FileSafetyError::DestinationInvalid, QStringLiteral("%1 has no parent directory").arg(role))};
  QString parent = path.left(separator);
  if (parent.size() == 2 && parent.at(1) == QLatin1Char(':'))
    parent += QLatin1Char('\\');
  return {parent, {}};
}

std::pair<ULONGLONG, FileSafetyResult> volumeSerial(HANDLE handle, FileSafetyError error, const QString &role)
{
  FILE_ID_INFO identity{};
  if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
    return {0, nativeFailure(error, QStringLiteral("read %1 volume").arg(role), ::GetLastError())};
  return {identity.VolumeSerialNumber, {}};
}

bool sameFile(HANDLE first, HANDLE second)
{
  FILE_ID_INFO firstInfo{};
  FILE_ID_INFO secondInfo{};
  if (!::GetFileInformationByHandleEx(first, FileIdInfo, &firstInfo, sizeof(firstInfo)) ||
      !::GetFileInformationByHandleEx(second, FileIdInfo, &secondInfo, sizeof(secondInfo))) {
    return false;
  }
  return firstInfo.VolumeSerialNumber == secondInfo.VolumeSerialNumber &&
         std::memcmp(firstInfo.FileId.Identifier, secondInfo.FileId.Identifier, sizeof(firstInfo.FileId.Identifier)) == 0;
}

FileSafetyResult commitNativeFailure(DWORD nativeError)
{
  switch (nativeError) {
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    return nativeFailure(FileSafetyError::DestinationExists, QStringLiteral("commit staged file"), nativeError);
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return nativeFailure(FileSafetyError::StagingFileUnavailable, QStringLiteral("commit staged file"), nativeError);
  case ERROR_CANT_ACCESS_FILE:
  case ERROR_REPARSE_TAG_INVALID:
  case ERROR_REPARSE_TAG_MISMATCH:
    return nativeFailure(FileSafetyError::LinkTraversalDetected, QStringLiteral("commit staged file"), nativeError);
  default:
    return nativeFailure(FileSafetyError::CommitFailed, QStringLiteral("commit staged file"), nativeError);
  }
}

} // namespace

FileSafetyResult WindowsFileSafety::verifyReceiveRoot(const VerifyReceiveRootRequest &request) const
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("receive root request is structurally invalid"));
  auto [rootPath, pathResult] =
      normalizeAbsolutePath(request.receiveRoot, FileSafetyError::InvalidRequest, QStringLiteral("receive root"));
  if (!pathResult.ok())
    return pathResult;
  const auto [root, rootResult] = openRoot(rootPath);
  return rootResult;
}

FileSafetyResult WindowsFileSafety::verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &request) const
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("link traversal request is structurally invalid"));

  auto [rootPath, rootPathResult] =
      normalizeAbsolutePath(request.receiveRoot, FileSafetyError::InvalidRequest, QStringLiteral("receive root"));
  if (!rootPathResult.ok())
    return rootPathResult;
  auto [candidatePath, candidatePathResult] =
      normalizeAbsolutePath(request.candidatePath, FileSafetyError::DestinationInvalid, QStringLiteral("candidate"));
  if (!candidatePathResult.ok())
    return candidatePathResult;
  auto [root, rootResult] = openRoot(rootPath);
  if (!rootResult.ok())
    return rootResult;
  auto [components, componentsResult] = relativeComponents(rootPath, candidatePath);
  if (!componentsResult.ok())
    return componentsResult;
  return inspectComponents(
      rootPath, components, true, FileSafetyError::DestinationInvalid, QStringLiteral("candidate path")
  );
}

FileSafetyResult WindowsFileSafety::commitStagedFile(const CommitStagedFileRequest &request)
{
  if (!request.isStructurallyValid())
    return failure(FileSafetyError::InvalidRequest, QStringLiteral("commit request is structurally invalid"));

  auto [rootPath, rootPathResult] =
      normalizeAbsolutePath(request.receiveRoot, FileSafetyError::InvalidRequest, QStringLiteral("receive root"));
  if (!rootPathResult.ok())
    return rootPathResult;
  auto [stagingPath, stagingPathResult] =
      normalizeAbsolutePath(request.stagingPath, FileSafetyError::DestinationInvalid, QStringLiteral("staging path"));
  if (!stagingPathResult.ok())
    return stagingPathResult;
  auto [destinationPath, destinationPathResult] = normalizeAbsolutePath(
      request.destinationPath, FileSafetyError::DestinationInvalid, QStringLiteral("destination path")
  );
  if (!destinationPathResult.ok())
    return destinationPathResult;
  if (stagingPath.compare(destinationPath, Qt::CaseInsensitive) == 0)
    return failure(FileSafetyError::DestinationInvalid, QStringLiteral("staging and destination paths are equal"));

  auto [root, rootResult] = openRoot(rootPath);
  if (!rootResult.ok())
    return rootResult;
  auto [stagingComponents, stagingComponentsResult] = relativeComponents(rootPath, stagingPath);
  if (!stagingComponentsResult.ok())
    return stagingComponentsResult;
  auto [destinationComponents, destinationComponentsResult] = relativeComponents(rootPath, destinationPath);
  if (!destinationComponentsResult.ok())
    return destinationComponentsResult;

  if (const auto result = inspectComponents(
          rootPath, stagingComponents, false, FileSafetyError::StagingFileUnavailable, QStringLiteral("staging path")
      );
      !result.ok()) {
    return result;
  }
  if (const auto result = inspectComponents(
          rootPath, destinationComponents, true, FileSafetyError::DestinationInvalid,
          QStringLiteral("destination path")
      );
      !result.ok()) {
    return result;
  }

  auto [staging, stagingOpenResult] =
      openEntry(stagingPath, FileSafetyError::StagingFileUnavailable, QStringLiteral("staging file"));
  if (!stagingOpenResult.ok())
    return stagingOpenResult;
  if ((staging.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("staging file is a reparse point"));
  if ((staging.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    return failure(FileSafetyError::StagingFileUnavailable, QStringLiteral("staging path is not a regular file"));

  auto [destinationParentPath, destinationParentPathResult] = parentPath(destinationPath, QStringLiteral("destination"));
  if (!destinationParentPathResult.ok())
    return destinationParentPathResult;
  auto [destinationParent, destinationParentOpenResult] =
      openEntry(destinationParentPath, FileSafetyError::DestinationInvalid, QStringLiteral("destination parent"));
  if (!destinationParentOpenResult.ok())
    return destinationParentOpenResult;
  if ((destinationParent.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("destination parent is a reparse point"));
  if ((destinationParent.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    return failure(FileSafetyError::DestinationInvalid, QStringLiteral("destination parent is not a directory"));

  auto [stagingVolume, stagingVolumeResult] =
      volumeSerial(staging.handle.get(), FileSafetyError::CommitFailed, QStringLiteral("staging"));
  if (!stagingVolumeResult.ok())
    return stagingVolumeResult;
  auto [destinationVolume, destinationVolumeResult] =
      volumeSerial(destinationParent.handle.get(), FileSafetyError::CommitFailed, QStringLiteral("destination"));
  if (!destinationVolumeResult.ok())
    return destinationVolumeResult;
  if (stagingVolume != destinationVolume)
    return failure(FileSafetyError::CommitFailed, QStringLiteral("staging and destination are on different volumes"));

  auto [destination, destinationOpenResult] =
      openEntry(destinationPath, FileSafetyError::DestinationInvalid, QStringLiteral("destination file"));
  bool destinationExists = destinationOpenResult.ok();
  if (!destinationExists) {
    const DWORD nativeError = destination.nativeError;
    if (nativeError != ERROR_FILE_NOT_FOUND && nativeError != ERROR_PATH_NOT_FOUND)
      return destinationOpenResult;
  } else {
    if ((destination.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      return failure(FileSafetyError::LinkTraversalDetected, QStringLiteral("destination is a reparse point"));
    if ((destination.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      return failure(FileSafetyError::DestinationInvalid, QStringLiteral("destination is not a regular file"));
    if (sameFile(staging.handle.get(), destination.handle.get()))
      return failure(FileSafetyError::DestinationInvalid, QStringLiteral("staging and destination are the same file"));
    if (request.disposition == CommitDisposition::FailIfExists)
      return failure(FileSafetyError::DestinationExists, QStringLiteral("destination already exists"));
  }

  staging.handle = NativeHandle{};
  destination.handle = NativeHandle{};
  destinationParent.handle = NativeHandle{};
  root.handle = NativeHandle{};

  BOOL committed = FALSE;
  if (request.disposition == CommitDisposition::ReplaceExisting && destinationExists) {
    committed = ::ReplaceFileW(
        reinterpret_cast<const wchar_t *>(destinationPath.utf16()),
        reinterpret_cast<const wchar_t *>(stagingPath.utf16()), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr
    );
  } else {
    committed = ::MoveFileExW(
        reinterpret_cast<const wchar_t *>(stagingPath.utf16()),
        reinterpret_cast<const wchar_t *>(destinationPath.utf16()), MOVEFILE_WRITE_THROUGH
    );
  }
  if (committed)
    return {};
  return commitNativeFailure(::GetLastError());
}

} // namespace deskflow::relaydesk
