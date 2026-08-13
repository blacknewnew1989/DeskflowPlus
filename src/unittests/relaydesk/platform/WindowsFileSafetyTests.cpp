/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsFileSafety.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <windows.h>
#include <winioctl.h>

#include <cstring>

using namespace deskflow::relaydesk;

namespace {

void createDirectory(const QString &path)
{
  QVERIFY2(QDir().mkpath(path), qPrintable(path));
}

void writeFile(const QString &path, const QByteArray &contents)
{
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
  QCOMPARE(file.write(contents), contents.size());
  file.close();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

#pragma pack(push, 1)
struct MountPointReparseData
{
  DWORD tag;
  WORD dataLength;
  WORD reserved;
  WORD substituteNameOffset;
  WORD substituteNameLength;
  WORD printNameOffset;
  WORD printNameLength;
  wchar_t pathBuffer[1];
};
#pragma pack(pop)

void createDirectoryJunction(const QString &target, const QString &linkPath)
{
  createDirectory(linkPath);
  const QString nativeTarget = QStringLiteral("\\??\\") + QDir::toNativeSeparators(target);
  const QString printTarget = QDir::toNativeSeparators(target);
  const DWORD substituteBytes = static_cast<DWORD>(nativeTarget.size() * sizeof(wchar_t));
  const DWORD printBytes = static_cast<DWORD>(printTarget.size() * sizeof(wchar_t));
  const DWORD pathBytes = substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
  const DWORD totalBytes = static_cast<DWORD>(offsetof(MountPointReparseData, pathBuffer)) + pathBytes;
  QByteArray storage(static_cast<qsizetype>(totalBytes), Qt::Uninitialized);
  auto *data = reinterpret_cast<MountPointReparseData *>(storage.data());
  data->tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->dataLength = static_cast<WORD>(8 + pathBytes);
  data->reserved = 0;
  data->substituteNameOffset = 0;
  data->substituteNameLength = static_cast<WORD>(substituteBytes);
  data->printNameOffset = static_cast<WORD>(substituteBytes + sizeof(wchar_t));
  data->printNameLength = static_cast<WORD>(printBytes);
  std::memcpy(data->pathBuffer, nativeTarget.utf16(), substituteBytes);
  data->pathBuffer[nativeTarget.size()] = L'\0';
  std::memcpy(
      reinterpret_cast<char *>(data->pathBuffer) + data->printNameOffset, printTarget.utf16(), printBytes
  );
  *reinterpret_cast<wchar_t *>(
      reinterpret_cast<char *>(data->pathBuffer) + data->printNameOffset + printBytes
  ) = L'\0';

  const HANDLE handle = ::CreateFileW(
      reinterpret_cast<const wchar_t *>(linkPath.utf16()), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr
  );
  QVERIFY2(handle != INVALID_HANDLE_VALUE, qPrintable(QString::number(::GetLastError())));
  DWORD returned = 0;
  const BOOL configured = ::DeviceIoControl(
      handle, FSCTL_SET_REPARSE_POINT, data, totalBytes, nullptr, 0, &returned, nullptr
  );
  const DWORD nativeError = configured ? ERROR_SUCCESS : ::GetLastError();
  ::CloseHandle(handle);
  QVERIFY2(configured, qPrintable(QString::number(nativeError)));
}

void removeDirectoryJunction(const QString &linkPath)
{
  QVERIFY2(
      ::RemoveDirectoryW(reinterpret_cast<const wchar_t *>(linkPath.utf16())) != FALSE,
      qPrintable(QString::number(::GetLastError()))
  );
}

CommitStagedFileRequest commitRequest(
    const QString &root, const QString &staging, const QString &destination,
    CommitDisposition disposition = CommitDisposition::FailIfExists
)
{
  return {
      .receiveRoot = root,
      .stagingPath = staging,
      .destinationPath = destination,
      .disposition = disposition,
  };
}

} // namespace

class WindowsFileSafetyTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void rejectsInvalidRequestsAndUnavailableRoots();
  void validatesReceiveRootAndRejectsFinalReparsePoint();
  void detectsReparseTraversalAndLexicalEscape();
  void rejectsEmbeddedNullAndDeviceNamespaces();
  void rejectsCommitPathsOutsideReceiveRoot();
  void commitsWithoutReplacingAnExistingDestination();
  void atomicallyReplacesARegularDestination();
  void rejectsHardLinkedDestinationIdentity();
  void rejectsReparseAndNonRegularCommitPaths();
};

void WindowsFileSafetyTests::rejectsInvalidRequestsAndUnavailableRoots()
{
  WindowsFileSafety safety;
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = QStringLiteral("relative")}).error, FileSafetyError::InvalidRequest);

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString missing = QDir(temporary.path()).absoluteFilePath(QStringLiteral("missing"));
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = missing}).error, FileSafetyError::ReceiveRootUnavailable);

  const QString filePath = QDir(temporary.path()).absoluteFilePath(QStringLiteral("plain-file"));
  writeFile(filePath, QByteArrayLiteral("file"));
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = filePath}).error, FileSafetyError::ReceiveRootNotDirectory);
}

void WindowsFileSafetyTests::validatesReceiveRootAndRejectsFinalReparsePoint()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir holder;
  QVERIFY(root.isValid());
  QVERIFY(holder.isValid());
  QVERIFY(safety.verifyReceiveRoot({.receiveRoot = root.path()}).ok());

  const QString linkPath = QDir(holder.path()).absoluteFilePath(QStringLiteral("root-link"));
  createDirectoryJunction(root.path(), linkPath);
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = linkPath}).error, FileSafetyError::LinkTraversalDetected);
  removeDirectoryJunction(linkPath);
}

void WindowsFileSafetyTests::detectsReparseTraversalAndLexicalEscape()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());

  const QString safeDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("safe"));
  createDirectory(safeDirectory);
  const QString missingLeaf = QDir(safeDirectory).absoluteFilePath(QStringLiteral("new-file"));
  QVERIFY(safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = missingLeaf}).ok());

  const QString linkPath = QDir(root.path()).absoluteFilePath(QStringLiteral("linked"));
  createDirectoryJunction(outside.path(), linkPath);
  const QString linkedCandidate = QDir(linkPath).absoluteFilePath(QStringLiteral("file"));
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = linkedCandidate}).error,
      FileSafetyError::LinkTraversalDetected
  );
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = outside.path()}).error,
      FileSafetyError::DestinationInvalid
  );
  removeDirectoryJunction(linkPath);
}

void WindowsFileSafetyTests::rejectsEmbeddedNullAndDeviceNamespaces()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString nulSuffix = QString(QChar::Null) + QStringLiteral("ignored");
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = root.path() + nulSuffix}).error, FileSafetyError::InvalidRequest);

  const QString candidate = QDir(root.path()).absoluteFilePath(QStringLiteral("candidate"));
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = candidate + nulSuffix}).error,
      FileSafetyError::DestinationInvalid
  );
  QCOMPARE(
      safety.verifyReceiveRoot({.receiveRoot = QStringLiteral("\\\\?\\C:\\RelayDesk")}).error,
      FileSafetyError::InvalidRequest
  );
}

void WindowsFileSafetyTests::rejectsCommitPathsOutsideReceiveRoot()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("file.txt"));
  const QString outsideStaging = QDir(outside.path()).absoluteFilePath(QStringLiteral("outside.part"));
  const QString outsideDestination = QDir(outside.path()).absoluteFilePath(QStringLiteral("outside.txt"));
  writeFile(staging, QByteArrayLiteral("inside"));
  writeFile(outsideStaging, QByteArrayLiteral("outside"));

  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), outsideStaging, destination)).error,
      FileSafetyError::DestinationInvalid
  );
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, outsideDestination)).error,
      FileSafetyError::DestinationInvalid
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(QFile::exists(outsideStaging));
  QVERIFY(!QFile::exists(destination));
  QVERIFY(!QFile::exists(outsideDestination));
}

void WindowsFileSafetyTests::commitsWithoutReplacingAnExistingDestination()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("file.txt"));

  writeFile(staging, QByteArrayLiteral("first"));
  QVERIFY(safety.commitStagedFile(commitRequest(root.path(), staging, destination)).ok());
  QVERIFY(!QFile::exists(staging));
  QCOMPARE(readFile(destination), QByteArrayLiteral("first"));

  writeFile(staging, QByteArrayLiteral("second"));
  QCOMPARE(safety.commitStagedFile(commitRequest(root.path(), staging, destination)).error, FileSafetyError::DestinationExists);
  QVERIFY(QFile::exists(staging));
  QCOMPARE(readFile(destination), QByteArrayLiteral("first"));
}

void WindowsFileSafetyTests::atomicallyReplacesARegularDestination()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("file.txt"));
  writeFile(staging, QByteArrayLiteral("replacement"));
  writeFile(destination, QByteArrayLiteral("old"));

  QVERIFY(
      safety.commitStagedFile(commitRequest(root.path(), staging, destination, CommitDisposition::ReplaceExisting)).ok()
  );
  QVERIFY(!QFile::exists(staging));
  QCOMPARE(readFile(destination), QByteArrayLiteral("replacement"));
}

void WindowsFileSafetyTests::rejectsHardLinkedDestinationIdentity()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("file.txt"));
  writeFile(staging, QByteArrayLiteral("same inode"));
  QVERIFY2(
      ::CreateHardLinkW(
          reinterpret_cast<const wchar_t *>(destination.utf16()),
          reinterpret_cast<const wchar_t *>(staging.utf16()), nullptr
      ) != FALSE,
      qPrintable(QString::number(::GetLastError()))
  );

  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, destination, CommitDisposition::ReplaceExisting)).error,
      FileSafetyError::DestinationInvalid
  );
  QCOMPARE(readFile(staging), QByteArrayLiteral("same inode"));
  QCOMPARE(readFile(destination), QByteArrayLiteral("same inode"));
}

void WindowsFileSafetyTests::rejectsReparseAndNonRegularCommitPaths()
{
  WindowsFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);

  const QString linkedParent = QDir(root.path()).absoluteFilePath(QStringLiteral("linked"));
  createDirectoryJunction(outside.path(), linkedParent);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  writeFile(staging, QByteArrayLiteral("staged"));
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, QDir(linkedParent).filePath(QStringLiteral("file.txt")))).error,
      FileSafetyError::LinkTraversalDetected
  );
  QVERIFY(QFile::exists(staging));

  const QString destinationDirectoryPath = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("directory-target"));
  createDirectory(destinationDirectoryPath);
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, destinationDirectoryPath, CommitDisposition::ReplaceExisting)).error,
      FileSafetyError::DestinationInvalid
  );
  QVERIFY(QFile::exists(staging));
  removeDirectoryJunction(linkedParent);
}

QTEST_GUILESS_MAIN(WindowsFileSafetyTests)

#include "WindowsFileSafetyTests.moc"
