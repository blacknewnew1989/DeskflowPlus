// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ManifestBuilder.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId(QStringLiteral("01234567-89ab-cdef-8123-456789abcdef"));
const FileId kFileId(QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"));
const qint64 kFixedModifiedAtMs = 1'730'000'000'000LL;

QString createFile(
    const QTemporaryDir &directory, const QString &name, const QByteArray &contents,
    qint64 modifiedAtMs = kFixedModifiedAtMs
)
{
  const QString path = directory.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  if (file.write(contents) != contents.size()) {
    return {};
  }
  if (!file.flush()) {
    return {};
  }
  file.close();

  // Set the timestamp after the writer is closed. On APFS, closing a writer
  // after setFileTime can update mtime again when buffered data is committed.
  QFile timestampFile(path);
  // Windows requires a writable handle for SetFileTime, while APFS requires
  // the original writer to be closed before the final timestamp is applied.
  if (!timestampFile.open(QIODevice::ReadWrite) ||
      !timestampFile.setFileTime(
          QDateTime::fromMSecsSinceEpoch(modifiedAtMs, QTimeZone::UTC), QFileDevice::FileModificationTime
      )) {
    return {};
  }
  timestampFile.close();
  return path;
}

SingleFileManifestRequest requestFor(const QString &sourcePath)
{
  return {
      .sourcePath = sourcePath,
      .relativeProtocolPath = {},
      .transferId = kTransferId,
      .fileId = kFileId,
  };
}

TransferManifestRequest
transferRequestFor(const QList<ManifestSourceRequest> &sources, const QString &displayName = QString())
{
  return {.sources = sources, .transferId = kTransferId, .displayName = displayName};
}

const PreparedManifestEntry *findEntry(const TransferManifest &manifest, const QString &protocolPath)
{
  for (const PreparedManifestEntry &prepared : manifest.entries) {
    if (prepared.entry.relativeProtocolPath == protocolPath) {
      return &prepared;
    }
  }
  return nullptr;
}

} // namespace

class ManifestBuilderTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void buildsEmptyFileManifest();
  void buildsUnicodeFileWithMetadataAndHash();
  void normalizesLogicalProtocolPath();
  void usesDefaultOneMiBChunks();
  void hashesInBoundedChunks();
  void rejectsInvalidChunkOptions();
  void rejectsUnsafeLogicalPath();
  void rejectsNullIdentifiers();
  void rejectsMissingSourceAndDirectory();
  void skipsSymbolicLinkWhenSupported();
  void detectsSourceSizeChangeDuringHash();
  void detectsSourceMtimeChangeDuringHash();
  void canonicalDigestExcludesLocalPathAndTransferId();
  void canonicalDigestMatchesFrozenVector();
  void buildsNestedUnicodeFolderAndPreservesEmptyDirectories();
  void buildsMultipleSourcesWithStableOrderAndDigest();
  void rejectsPortableCollisionKeys();
  void enforcesDepthEntryAndMetadataLimits();
  void rejectsInvalidResourceOptions();
  void detectsFileChangeAfterDirectoryScan();
  void detectsDirectoryChangeDuringBuild();
  void skipsNestedSymbolicLinkWhenSupported();
  void buildsThousandsOfFilesWithinDefaultLimits();
};

void ManifestBuilderTests::buildsEmptyFileManifest()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("empty.bin"), {});
  QVERIFY(!path.isEmpty());

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->entry.size, 0);
  QCOMPARE(
      result.manifest->entry.sha256.toHex(),
      QByteArrayLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
  );
  QCOMPARE(result.manifest->summary.totalBytes, 0);
  QCOMPARE(result.manifest->summary.fileCount, 1);
  QCOMPARE(result.manifest->summary.directoryCount, 0);
}

void ManifestBuilderTests::buildsUnicodeFileWithMetadataAndHash()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("数据 😀.txt"), QByteArrayLiteral("abc"));
  QVERIFY(!path.isEmpty());

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->entry.id, kFileId);
  QCOMPARE(result.manifest->entry.relativeProtocolPath, QStringLiteral("数据 😀.txt"));
  QCOMPARE(result.manifest->entry.type, ManifestEntryType::File);
  QCOMPARE(result.manifest->entry.size, 3);
  QCOMPARE(result.manifest->entry.modifiedUtc.toMSecsSinceEpoch(), kFixedModifiedAtMs);
  QCOMPARE(
      result.manifest->entry.sha256.toHex(),
      QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
  );
  QCOMPARE(result.manifest->summary.id, kTransferId);
  QCOMPARE(result.manifest->summary.displayName, QStringLiteral("数据 😀.txt"));
  QCOMPARE(result.manifest->summary.canonicalSha256.size(), kSha256Bytes);
  QCOMPARE(result.manifest->canonicalSourcePath, QFileInfo(path).canonicalFilePath());
}

void ManifestBuilderTests::normalizesLogicalProtocolPath()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("source.txt"), QByteArrayLiteral("data"));
  QVERIFY(!path.isEmpty());
  auto request = requestFor(path);
  request.relativeProtocolPath = QStringLiteral("Cafe\u0301/Report.TXT");

  const auto result = ManifestBuilder::buildSingleFile(request);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->entry.relativeProtocolPath, QStringLiteral("Café/Report.TXT"));
  QCOMPARE(result.manifest->protocolCollisionKey, QStringLiteral("café/report.txt"));
}

void ManifestBuilderTests::hashesInBoundedChunks()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("chunks.bin"), QByteArrayLiteral("0123456789"));
  QVERIFY(!path.isEmpty());
  QList<quint64> progressValues;
  ManifestBuildOptions options;
  options.hashChunkBytes = 4;
  options.progress = [&progressValues](const ManifestBuildProgress &progress) {
    progressValues.append(progress.bytesHashed);
    QCOMPARE(progress.totalBytes, 10);
  };

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path), options);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(progressValues, QList<quint64>({4, 8, 10}));
}

void ManifestBuilderTests::usesDefaultOneMiBChunks()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray contents(static_cast<qsizetype>(kDefaultManifestHashChunkBytes) + 1, 'x');
  const QString path = createFile(directory, QStringLiteral("default-chunks.bin"), contents);
  QVERIFY(!path.isEmpty());
  QList<quint64> progressValues;
  ManifestBuildOptions options;
  options.progress = [&progressValues](const ManifestBuildProgress &progress) {
    progressValues.append(progress.bytesHashed);
  };

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path), options);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(
      progressValues,
      QList<quint64>({kDefaultManifestHashChunkBytes, static_cast<quint64>(kDefaultManifestHashChunkBytes) + 1})
  );
}

void ManifestBuilderTests::rejectsInvalidChunkOptions()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("source.bin"), QByteArrayLiteral("data"));
  QVERIFY(!path.isEmpty());

  ManifestBuildOptions options;
  options.hashChunkBytes = 0;
  QCOMPARE(ManifestBuilder::buildSingleFile(requestFor(path), options).error, ManifestBuildError::InvalidOptions);
  options.hashChunkBytes = kMaxManifestHashChunkBytes + 1;
  QCOMPARE(ManifestBuilder::buildSingleFile(requestFor(path), options).error, ManifestBuildError::InvalidOptions);
}

void ManifestBuilderTests::rejectsUnsafeLogicalPath()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("source.txt"), QByteArrayLiteral("data"));
  auto request = requestFor(path);
  request.relativeProtocolPath = QStringLiteral("../escape.txt");

  const auto result = ManifestBuilder::buildSingleFile(request);

  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::UnsafeProtocolPath);
  QCOMPARE(result.pathError, PathError::ParentTraversal);
}

void ManifestBuilderTests::rejectsNullIdentifiers()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("source.txt"), QByteArrayLiteral("data"));
  auto request = requestFor(path);
  request.transferId = QUuid();
  QCOMPARE(ManifestBuilder::buildSingleFile(request).error, ManifestBuildError::InvalidTransferId);

  request = requestFor(path);
  request.fileId = QUuid();
  QCOMPARE(ManifestBuilder::buildSingleFile(request).error, ManifestBuildError::InvalidFileId);
}

void ManifestBuilderTests::rejectsMissingSourceAndDirectory()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QCOMPARE(
      ManifestBuilder::buildSingleFile(requestFor(directory.filePath(QStringLiteral("missing.bin")))).error,
      ManifestBuildError::SourceNotFound
  );
  QCOMPARE(
      ManifestBuilder::buildSingleFile(requestFor(directory.path())).error, ManifestBuildError::DirectoryNotSupported
  );
}

void ManifestBuilderTests::skipsSymbolicLinkWhenSupported()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString sourcePath = createFile(directory, QStringLiteral("target.bin"), QByteArrayLiteral("data"));
  QVERIFY(!sourcePath.isEmpty());
  const QString linkPath = directory.filePath(QStringLiteral("source-link"));
  if (!QFile::link(sourcePath, linkPath) || !QFileInfo(linkPath).isSymLink()) {
    QSKIP("This Windows environment cannot create a QFileInfo-visible symbolic link");
  }

  const auto result = ManifestBuilder::buildSingleFile(requestFor(linkPath));

  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::EntrySkipped);
}

void ManifestBuilderTests::detectsSourceSizeChangeDuringHash()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("changes.bin"), QByteArrayLiteral("0123456789"));
  QVERIFY(!path.isEmpty());
  bool changed = false;
  ManifestBuildOptions options;
  options.hashChunkBytes = 4;
  options.progress = [&changed, &path](const ManifestBuildProgress &) {
    if (changed) {
      return;
    }
    QFile mutator(path);
    QVERIFY(mutator.open(QIODevice::Append));
    QCOMPARE(mutator.write("x", 1), 1);
    mutator.close();
    changed = true;
  };

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path), options);

  QVERIFY(changed);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::SourceChanged);
}

void ManifestBuilderTests::detectsSourceMtimeChangeDuringHash()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("mtime.bin"), QByteArrayLiteral("0123456789"));
  QVERIFY(!path.isEmpty());
  bool changed = false;
  ManifestBuildOptions options;
  options.hashChunkBytes = 4;
  options.progress = [&changed, &path](const ManifestBuildProgress &) {
    if (changed) {
      return;
    }
    QFile mutator(path);
    QVERIFY(mutator.open(QIODevice::ReadWrite));
    QVERIFY(mutator.setFileTime(
        QDateTime::fromMSecsSinceEpoch(kFixedModifiedAtMs + 10'000, QTimeZone::UTC), QFileDevice::FileModificationTime
    ));
    mutator.close();
    changed = true;
  };

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path), options);

  QVERIFY(changed);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::SourceChanged);
}

void ManifestBuilderTests::canonicalDigestExcludesLocalPathAndTransferId()
{
  QTemporaryDir firstDirectory;
  QTemporaryDir secondDirectory;
  QVERIFY(firstDirectory.isValid());
  QVERIFY(secondDirectory.isValid());
  const QString firstPath = createFile(firstDirectory, QStringLiteral("same.bin"), QByteArrayLiteral("same"));
  const QString secondPath = createFile(secondDirectory, QStringLiteral("same.bin"), QByteArrayLiteral("same"));
  QVERIFY(!firstPath.isEmpty());
  QVERIFY(!secondPath.isEmpty());

  auto firstRequest = requestFor(firstPath);
  auto secondRequest = requestFor(secondPath);
  secondRequest.transferId = QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));
  const auto first = ManifestBuilder::buildSingleFile(firstRequest);
  const auto second = ManifestBuilder::buildSingleFile(secondRequest);

  QVERIFY(first.ok());
  QVERIFY(second.ok());
  QCOMPARE(first.manifest->summary.canonicalSha256, second.manifest->summary.canonicalSha256);
}

void ManifestBuilderTests::canonicalDigestMatchesFrozenVector()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = createFile(directory, QStringLiteral("vector.txt"), QByteArrayLiteral("abc"));
  QVERIFY(!path.isEmpty());

  const auto result = ManifestBuilder::buildSingleFile(requestFor(path));

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  // Frozen after the canonical form above was defined. Changing this digest
  // requires a protocol compatibility decision, not a platform-specific fix.
  QCOMPARE(
      result.manifest->summary.canonicalSha256.toHex(),
      QByteArrayLiteral("7fdd103e0b3fd12dd6762a609fe21b1d7cdb97962096922fa379a667283ec518")
  );
}

void ManifestBuilderTests::buildsNestedUnicodeFolderAndPreservesEmptyDirectories()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QDir root(directory.path());
  QVERIFY(root.mkpath(QStringLiteral("Café/空目录")));
  QVERIFY(root.mkpath(QStringLiteral("数据")));
  const QString filePath = createFile(directory, QStringLiteral("数据/报告 😀.txt"), QByteArrayLiteral("abc"));
  QVERIFY(!filePath.isEmpty());

  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("根")},
  });
  const auto result = ManifestBuilder::buildTransfer(request);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->summary.totalBytes, 3);
  QCOMPARE(result.manifest->summary.fileCount, 1);
  QCOMPARE(result.manifest->summary.directoryCount, 4);
  QCOMPARE(result.manifest->entries.size(), 5);
  const PreparedManifestEntry *file = findEntry(*result.manifest, QStringLiteral("根/数据/报告 😀.txt"));
  QVERIFY(file != nullptr);
  QCOMPARE(
      file->entry.sha256.toHex(), QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
  );
  QCOMPARE(file->protocolCollisionKey, QStringLiteral("根/数据/报告 😀.txt").toCaseFolded());
  const PreparedManifestEntry *empty = findEntry(*result.manifest, QStringLiteral("根/Café/空目录"));
  QVERIFY(empty != nullptr);
  QCOMPARE(empty->entry.type, ManifestEntryType::Directory);
  QCOMPARE(empty->entry.size, 0);
  QVERIFY(empty->entry.sha256.isEmpty());
}

void ManifestBuilderTests::buildsMultipleSourcesWithStableOrderAndDigest()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString alpha = createFile(directory, QStringLiteral("local-a.bin"), QByteArrayLiteral("alpha"));
  const QString beta = createFile(directory, QStringLiteral("local-b.bin"), QByteArrayLiteral("beta"));
  QVERIFY(!alpha.isEmpty());
  QVERIFY(!beta.isEmpty());
  const ManifestSourceRequest alphaSource{.sourcePath = alpha, .relativeProtocolPath = QStringLiteral("Batch/a.bin")};
  const ManifestSourceRequest betaSource{.sourcePath = beta, .relativeProtocolPath = QStringLiteral("Batch/b.bin")};

  const auto forward =
      ManifestBuilder::buildTransfer(transferRequestFor({betaSource, alphaSource}, QStringLiteral("Batch")));
  auto reverseRequest = transferRequestFor({alphaSource, betaSource}, QStringLiteral("Batch"));
  reverseRequest.transferId = QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));
  const auto reverse = ManifestBuilder::buildTransfer(reverseRequest);

  QVERIFY2(forward.ok(), qPrintable(forward.diagnostic));
  QVERIFY2(reverse.ok(), qPrintable(reverse.diagnostic));
  QCOMPARE(forward.manifest->entries.at(0).entry.relativeProtocolPath, QStringLiteral("Batch/a.bin"));
  QCOMPARE(forward.manifest->entries.at(1).entry.relativeProtocolPath, QStringLiteral("Batch/b.bin"));
  QCOMPARE(forward.manifest->summary.fileCount, 2);
  QCOMPARE(forward.manifest->summary.directoryCount, 0);
  QCOMPARE(forward.manifest->summary.totalBytes, 9);
  QCOMPARE(forward.manifest->summary.canonicalSha256, reverse.manifest->summary.canonicalSha256);
  QCOMPARE(forward.manifest->entries.at(0).entry.id, reverse.manifest->entries.at(0).entry.id);
  // Frozen RDFT/1 multi-entry vector. It also locks the deterministic UUIDv8
  // entry IDs and UTF-8 path ordering into the shared compatibility contract.
  QCOMPARE(
      forward.manifest->summary.canonicalSha256.toHex(),
      QByteArrayLiteral("88c8e7fb4629abd54fb73f89a50cb299cfde298afb7f742fc709e77eb89d9c7c")
  );
}

void ManifestBuilderTests::rejectsPortableCollisionKeys()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString first = createFile(directory, QStringLiteral("first.bin"), QByteArrayLiteral("a"));
  const QString second = createFile(directory, QStringLiteral("second.bin"), QByteArrayLiteral("b"));
  QVERIFY(!first.isEmpty());
  QVERIFY(!second.isEmpty());

  const auto result = ManifestBuilder::buildTransfer(transferRequestFor(
      {
          {.sourcePath = first, .relativeProtocolPath = QStringLiteral("Data/Readme.txt")},
          {.sourcePath = second, .relativeProtocolPath = QStringLiteral("data/README.TXT")},
      },
      QStringLiteral("Collision test")
  ));

  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::ProtocolPathCollision);
}

void ManifestBuilderTests::enforcesDepthEntryAndMetadataLimits()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QDir root(directory.path());
  QVERIFY(root.mkpath(QStringLiteral("one/two")));
  QVERIFY(!createFile(directory, QStringLiteral("one/a.bin"), {}).isEmpty());
  QVERIFY(!createFile(directory, QStringLiteral("one/two/b.bin"), {}).isEmpty());
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("root")},
  });

  ManifestBuildOptions options;
  options.pathLimits.maxDepth = 2;
  auto result = ManifestBuilder::buildTransfer(request, options);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::UnsafeProtocolPath);
  QCOMPARE(result.pathError, PathError::TooDeep);

  options = {};
  options.maxEntries = 2;
  result = ManifestBuilder::buildTransfer(request, options);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::TooManyEntries);

  options = {};
  options.maxMetadataBytes = 1;
  result = ManifestBuilder::buildTransfer(request, options);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::ManifestMetadataTooLarge);
}

void ManifestBuilderTests::rejectsInvalidResourceOptions()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("root")},
  });
  ManifestBuildOptions options;
  options.maxEntries = kMaxManifestEntries + 1;
  QCOMPARE(ManifestBuilder::buildTransfer(request, options).error, ManifestBuildError::InvalidOptions);
  options = {};
  options.maxMetadataBytes = kMaxManifestMetadataBytes + 1;
  QCOMPARE(ManifestBuilder::buildTransfer(request, options).error, ManifestBuildError::InvalidOptions);
}

void ManifestBuilderTests::detectsFileChangeAfterDirectoryScan()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString first = createFile(directory, QStringLiteral("a.bin"), QByteArrayLiteral("aaaa"));
  const QString later = createFile(directory, QStringLiteral("z.bin"), QByteArrayLiteral("zzzz"));
  QVERIFY(!first.isEmpty());
  QVERIFY(!later.isEmpty());
  bool changed = false;
  ManifestBuildOptions options;
  options.hashChunkBytes = 2;
  options.progress = [&changed, &later](const ManifestBuildProgress &) {
    if (changed) {
      return;
    }
    QFile mutator(later);
    QVERIFY(mutator.open(QIODevice::Append));
    QCOMPARE(mutator.write("x", 1), 1);
    mutator.close();
    changed = true;
  };
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("root")},
  });

  const auto result = ManifestBuilder::buildTransfer(request, options);

  QVERIFY(changed);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::SourceChanged);
}

void ManifestBuilderTests::detectsDirectoryChangeDuringBuild()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(!createFile(directory, QStringLiteral("source.bin"), QByteArrayLiteral("data")).isEmpty());
  bool changed = false;
  ManifestBuildOptions options;
  options.hashChunkBytes = 2;
  options.progress = [&changed, &directory](const ManifestBuildProgress &) {
    if (changed) {
      return;
    }
    QVERIFY(!createFile(directory, QStringLiteral("appeared.bin"), QByteArrayLiteral("new")).isEmpty());
    changed = true;
  };
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("root")},
  });

  const auto result = ManifestBuilder::buildTransfer(request, options);

  QVERIFY(changed);
  QVERIFY(!result.ok());
  QCOMPARE(result.error, ManifestBuildError::SourceChanged);
}

void ManifestBuilderTests::skipsNestedSymbolicLinkWhenSupported()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString target = createFile(directory, QStringLiteral("target.bin"), QByteArrayLiteral("data"));
  QVERIFY(!target.isEmpty());
  const QString link = directory.filePath(QStringLiteral("nested-link"));
  if (!QFile::link(target, link) || !QFileInfo(link).isSymLink()) {
    QSKIP("This Windows environment cannot create a QFileInfo-visible symbolic link");
  }
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("root")},
  });

  const auto result = ManifestBuilder::buildTransfer(request);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->summary.fileCount, 1);
  QCOMPARE(result.manifest->warnings.size(), 1);
  QCOMPARE(result.manifest->warnings.constFirst().relativeProtocolPath, QStringLiteral("root/nested-link"));
  QVERIFY(!result.manifest->warnings.constFirst().diagnostic.isEmpty());
}

void ManifestBuilderTests::buildsThousandsOfFilesWithinDefaultLimits()
{
  constexpr int kFileCount = 2'048;
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  for (int index = 0; index < kFileCount; ++index) {
    const QString name = QStringLiteral("file-%1.bin").arg(index, 4, 10, QLatin1Char('0'));
    QVERIFY2(!createFile(directory, name, {}).isEmpty(), qPrintable(name));
  }
  const auto request = transferRequestFor({
      {.sourcePath = directory.path(), .relativeProtocolPath = QStringLiteral("bulk")},
  });

  const auto result = ManifestBuilder::buildTransfer(request);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.manifest->entries.size(), kFileCount + 1);
  QCOMPARE(result.manifest->summary.fileCount, kFileCount);
  QCOMPARE(result.manifest->summary.directoryCount, 1);
  QCOMPARE(result.manifest->summary.totalBytes, 0);
  QCOMPARE(result.manifest->entries.constFirst().entry.relativeProtocolPath, QStringLiteral("bulk"));
  QCOMPARE(result.manifest->entries.constLast().entry.relativeProtocolPath, QStringLiteral("bulk/file-2047.bin"));
}

QTEST_MAIN(ManifestBuilderTests)

#include "ManifestBuilderTests.moc"
