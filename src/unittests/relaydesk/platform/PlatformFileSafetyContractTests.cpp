/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/IPlatformFileSafety.h"

#include <QDir>
#include <QMetaType>
#include <QTest>

#include <type_traits>

using namespace deskflow::relaydesk;

namespace {

using VerifyReceiveRootMethod = FileSafetyResult (IPlatformFileSafety::*)(const VerifyReceiveRootRequest &) const;
using VerifyNoLinkTraversalMethod =
    FileSafetyResult (IPlatformFileSafety::*)(const VerifyNoLinkTraversalRequest &) const;
using CommitStagedFileMethod = FileSafetyResult (IPlatformFileSafety::*)(const CommitStagedFileRequest &);

static_assert(std::is_abstract_v<IPlatformFileSafety>);
static_assert(std::is_same_v<decltype(&IPlatformFileSafety::verifyReceiveRoot), VerifyReceiveRootMethod>);
static_assert(std::is_same_v<decltype(&IPlatformFileSafety::verifyNoLinkTraversal), VerifyNoLinkTraversalMethod>);
static_assert(std::is_same_v<decltype(&IPlatformFileSafety::commitStagedFile), CommitStagedFileMethod>);
static_assert(std::is_copy_constructible_v<VerifyReceiveRootRequest>);
static_assert(std::is_copy_constructible_v<VerifyNoLinkTraversalRequest>);
static_assert(std::is_copy_constructible_v<CommitStagedFileRequest>);
static_assert(std::is_copy_constructible_v<FileSafetyResult>);
static_assert(std::is_same_v<decltype(VerifyReceiveRootRequest::receiveRoot), QString>);
static_assert(std::is_same_v<decltype(VerifyNoLinkTraversalRequest::receiveRoot), QString>);
static_assert(std::is_same_v<decltype(VerifyNoLinkTraversalRequest::candidatePath), QString>);
static_assert(std::is_same_v<decltype(CommitStagedFileRequest::receiveRoot), QString>);
static_assert(std::is_same_v<decltype(CommitStagedFileRequest::stagingPath), QString>);
static_assert(std::is_same_v<decltype(CommitStagedFileRequest::destinationPath), QString>);
static_assert(std::is_same_v<decltype(CommitStagedFileRequest::disposition), CommitDisposition>);
static_assert(std::is_same_v<decltype(FileSafetyResult::error), FileSafetyError>);
static_assert(std::is_same_v<decltype(FileSafetyResult::diagnostic), QString>);
static_assert(static_cast<int>(FileSafetyError::None) == 0);
static_assert(static_cast<int>(FileSafetyError::InvalidRequest) == 1);
static_assert(static_cast<int>(FileSafetyError::ReceiveRootUnavailable) == 2);
static_assert(static_cast<int>(FileSafetyError::ReceiveRootNotDirectory) == 3);
static_assert(static_cast<int>(FileSafetyError::LinkTraversalDetected) == 4);
static_assert(static_cast<int>(FileSafetyError::StagingFileUnavailable) == 5);
static_assert(static_cast<int>(FileSafetyError::DestinationInvalid) == 6);
static_assert(static_cast<int>(FileSafetyError::DestinationExists) == 7);
static_assert(static_cast<int>(FileSafetyError::CommitFailed) == 8);
static_assert(static_cast<int>(CommitDisposition::FailIfExists) == 0);
static_assert(static_cast<int>(CommitDisposition::ReplaceExisting) == 1);

QString absolutePath(const QString &relative)
{
  return QDir(QDir::tempPath()).absoluteFilePath(relative);
}

} // namespace

class PlatformFileSafetyContractTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void validatesRequestShapeWithoutFilesystemIo();
  void keepsCommitRolesNamedAndCopyable();
  void exposesStableTypedResult();
  void registersQueuedValueTypes();
};

void PlatformFileSafetyContractTests::validatesRequestShapeWithoutFilesystemIo()
{
  const QString root = absolutePath(QStringLiteral("RelayDesk-receive-root"));
  QVERIFY(VerifyReceiveRootRequest{.receiveRoot = root}.isStructurallyValid());
  QVERIFY(!VerifyReceiveRootRequest{.receiveRoot = QStringLiteral("relative/root")}.isStructurallyValid());

  QVERIFY((VerifyNoLinkTraversalRequest{
               .receiveRoot = root,
               .candidatePath = QDir(root).absoluteFilePath(QStringLiteral("folder/file.txt")),
           }
               .isStructurallyValid()));
  QVERIFY(!(VerifyNoLinkTraversalRequest{
                .receiveRoot = root,
                .candidatePath = QStringLiteral("folder/file.txt"),
            }
                .isStructurallyValid()));
}

void PlatformFileSafetyContractTests::keepsCommitRolesNamedAndCopyable()
{
  const QString root = absolutePath(QStringLiteral("RelayDesk-receive-root"));
  const CommitStagedFileRequest request{
      .receiveRoot = root,
      .stagingPath = QDir(root).absoluteFilePath(QStringLiteral(".incoming/transfer/file.part")),
      .destinationPath = QDir(root).absoluteFilePath(QStringLiteral("folder/file.txt")),
      .disposition = CommitDisposition::ReplaceExisting,
  };

  QVERIFY(request.isStructurallyValid());
  QCOMPARE(CommitStagedFileRequest(request), request);

  auto samePath = request;
  samePath.destinationPath = samePath.stagingPath;
  QVERIFY(!samePath.isStructurallyValid());

  auto unknownDisposition = request;
  unknownDisposition.disposition = static_cast<CommitDisposition>(99);
  QVERIFY(!unknownDisposition.isStructurallyValid());
}

void PlatformFileSafetyContractTests::exposesStableTypedResult()
{
  const FileSafetyResult success;
  QVERIFY(success.ok());

  const FileSafetyResult failed{
      .error = FileSafetyError::LinkTraversalDetected,
      .diagnostic = QStringLiteral("platform-private link diagnostic"),
  };
  QVERIFY(!failed.ok());
  QCOMPARE(FileSafetyResult(failed), failed);
}

void PlatformFileSafetyContractTests::registersQueuedValueTypes()
{
  QVERIFY(QMetaType::fromType<FileSafetyError>().isValid());
  QVERIFY(QMetaType::fromType<CommitDisposition>().isValid());
  QVERIFY(QMetaType::fromType<VerifyReceiveRootRequest>().isValid());
  QVERIFY(QMetaType::fromType<VerifyNoLinkTraversalRequest>().isValid());
  QVERIFY(QMetaType::fromType<CommitStagedFileRequest>().isValid());
  QVERIFY(QMetaType::fromType<FileSafetyResult>().isValid());
}

QTEST_GUILESS_MAIN(PlatformFileSafetyContractTests)

#include "PlatformFileSafetyContractTests.moc"
