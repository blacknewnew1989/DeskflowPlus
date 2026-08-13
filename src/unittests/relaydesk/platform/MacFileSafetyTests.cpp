/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/MacFileSafety.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

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

void createSymlink(const QString &target, const QString &linkPath)
{
  const QByteArray encodedTarget = QFile::encodeName(target);
  const QByteArray encodedLink = QFile::encodeName(linkPath);
  QCOMPARE(::symlink(encodedTarget.constData(), encodedLink.constData()), 0);
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

class MacFileSafetyTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void rejectsInvalidRequestsAndUnavailableRoots();
  void validatesRealReceiveRootWithoutFollowingFinalSymlink();
  void detectsSymlinkTraversalAndLexicalEscape();
  void rejectsEmbeddedNullWithoutTruncatingFilesystemPaths();
  void commitsWithoutReplacingAnExistingDestination();
  void atomicallyReplacesARegularDestination();
  void rejectsSymlinkedStagingAndDestinationPaths();
  void rejectsSymlinkedCommitParents();
};

void MacFileSafetyTests::rejectsInvalidRequestsAndUnavailableRoots()
{
  MacFileSafety safety;
  QCOMPARE(
      safety.verifyReceiveRoot({.receiveRoot = QStringLiteral("relative")}).error,
      FileSafetyError::InvalidRequest
  );

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString missing = QDir(temporary.path()).absoluteFilePath(QStringLiteral("missing"));
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = missing}).error, FileSafetyError::ReceiveRootUnavailable);

  const QString filePath = QDir(temporary.path()).absoluteFilePath(QStringLiteral("plain-file"));
  writeFile(filePath, QByteArrayLiteral("file"));
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = filePath}).error, FileSafetyError::ReceiveRootNotDirectory);
}

void MacFileSafetyTests::validatesRealReceiveRootWithoutFollowingFinalSymlink()
{
  MacFileSafety safety;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(safety.verifyReceiveRoot({.receiveRoot = temporary.path()}).ok());

  const QString linkPath = temporary.path() + QStringLiteral("-link");
  createSymlink(temporary.path(), linkPath);
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = linkPath}).error, FileSafetyError::LinkTraversalDetected);
  QVERIFY(QFile::remove(linkPath));
}

void MacFileSafetyTests::detectsSymlinkTraversalAndLexicalEscape()
{
  MacFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());

  const QString safeDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("safe"));
  createDirectory(safeDirectory);
  const QString missingLeaf = QDir(safeDirectory).absoluteFilePath(QStringLiteral("new-file"));
  QVERIFY(safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = missingLeaf}).ok());

  const QString linkPath = QDir(root.path()).absoluteFilePath(QStringLiteral("linked"));
  createSymlink(outside.path(), linkPath);
  const QString linkedCandidate = QDir(linkPath).absoluteFilePath(QStringLiteral("file"));
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = linkedCandidate}).error,
      FileSafetyError::LinkTraversalDetected
  );
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = outside.path()}).error,
      FileSafetyError::DestinationInvalid
  );
}

void MacFileSafetyTests::rejectsEmbeddedNullWithoutTruncatingFilesystemPaths()
{
  MacFileSafety safety;
  QTemporaryDir root;
  QVERIFY(root.isValid());

  const QString nulSuffix = QString(QChar::Null) + QStringLiteral("ignored");
  QCOMPARE(safety.verifyReceiveRoot({.receiveRoot = root.path() + nulSuffix}).error, FileSafetyError::InvalidRequest);

  const QString safeDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("safe"));
  createDirectory(safeDirectory);
  const QString truncatedCandidate = QDir(safeDirectory).absoluteFilePath(QStringLiteral("candidate"));
  QCOMPARE(
      safety.verifyNoLinkTraversal({.receiveRoot = root.path(), .candidatePath = truncatedCandidate + nulSuffix}).error,
      FileSafetyError::DestinationInvalid
  );

  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  createDirectory(stagingDirectory);
  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(safeDirectory).absoluteFilePath(QStringLiteral("file.txt"));
  writeFile(staging, QByteArrayLiteral("staged"));

  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging + nulSuffix, destination)).error,
      FileSafetyError::DestinationInvalid
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(!QFile::exists(destination));

  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, destination + nulSuffix)).error,
      FileSafetyError::DestinationInvalid
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(!QFile::exists(destination));
}

void MacFileSafetyTests::commitsWithoutReplacingAnExistingDestination()
{
  MacFileSafety safety;
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
  QFile committed(destination);
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), QByteArrayLiteral("first"));
  committed.close();

  writeFile(staging, QByteArrayLiteral("second"));
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, destination)).error,
      FileSafetyError::DestinationExists
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), QByteArrayLiteral("first"));
}

void MacFileSafetyTests::atomicallyReplacesARegularDestination()
{
  MacFileSafety safety;
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
      safety
          .commitStagedFile(commitRequest(root.path(), staging, destination, CommitDisposition::ReplaceExisting))
          .ok()
  );
  QVERIFY(!QFile::exists(staging));
  QFile committed(destination);
  QVERIFY(committed.open(QIODevice::ReadOnly));
  QCOMPARE(committed.readAll(), QByteArrayLiteral("replacement"));
}

void MacFileSafetyTests::rejectsSymlinkedStagingAndDestinationPaths()
{
  MacFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString destinationDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral("folder"));
  createDirectory(stagingDirectory);
  createDirectory(destinationDirectory);
  const QString outsideFile = QDir(outside.path()).absoluteFilePath(QStringLiteral("outside"));
  writeFile(outsideFile, QByteArrayLiteral("outside"));

  const QString stagingLink = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  const QString destination = QDir(destinationDirectory).absoluteFilePath(QStringLiteral("file.txt"));
  createSymlink(outsideFile, stagingLink);
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), stagingLink, destination)).error,
      FileSafetyError::LinkTraversalDetected
  );
  QVERIFY(QFile::remove(stagingLink));

  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  writeFile(staging, QByteArrayLiteral("staged"));
  createSymlink(outsideFile, destination);
  QCOMPARE(
      safety
          .commitStagedFile(commitRequest(root.path(), staging, destination, CommitDisposition::ReplaceExisting))
          .error,
      FileSafetyError::LinkTraversalDetected
  );
  QVERIFY(QFile::exists(staging));
}

void MacFileSafetyTests::rejectsSymlinkedCommitParents()
{
  MacFileSafety safety;
  QTemporaryDir root;
  QTemporaryDir outside;
  QVERIFY(root.isValid());
  QVERIFY(outside.isValid());
  const QString stagingDirectory = QDir(root.path()).absoluteFilePath(QStringLiteral(".incoming"));
  const QString outsideDirectory = QDir(outside.path()).absoluteFilePath(QStringLiteral("outside"));
  createDirectory(stagingDirectory);
  createDirectory(outsideDirectory);

  const QString stagingParentLink = QDir(root.path()).absoluteFilePath(QStringLiteral("staging-link"));
  const QString destinationParentLink = QDir(root.path()).absoluteFilePath(QStringLiteral("destination-link"));
  createSymlink(stagingDirectory, stagingParentLink);
  createSymlink(outsideDirectory, destinationParentLink);

  const QString staging = QDir(stagingDirectory).absoluteFilePath(QStringLiteral("file.part"));
  writeFile(staging, QByteArrayLiteral("staged"));
  const QString destination = QDir(destinationParentLink).absoluteFilePath(QStringLiteral("file.txt"));
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), staging, destination)).error,
      FileSafetyError::LinkTraversalDetected
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(!QFile::exists(QDir(outsideDirectory).absoluteFilePath(QStringLiteral("file.txt"))));

  const QString linkedStaging = QDir(stagingParentLink).absoluteFilePath(QStringLiteral("file.part"));
  const QString safeDestination = QDir(root.path()).absoluteFilePath(QStringLiteral("file.txt"));
  QCOMPARE(
      safety.commitStagedFile(commitRequest(root.path(), linkedStaging, safeDestination)).error,
      FileSafetyError::LinkTraversalDetected
  );
  QVERIFY(QFile::exists(staging));
  QVERIFY(!QFile::exists(safeDestination));
}

QTEST_GUILESS_MAIN(MacFileSafetyTests)

#include "MacFileSafetyTests.moc"
