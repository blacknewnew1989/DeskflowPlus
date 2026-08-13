// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ConflictResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <future>
#include <vector>

using namespace relaydesk::transfer;

namespace {

bool createFile(const QString &path)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
}

UseTarget use(const ConflictDecision &decision)
{
  return std::get<UseTarget>(decision);
}

} // namespace

class ConflictResolverTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void autoRenamesFilesExtensionsAndDirectories_data();
  void autoRenamesFilesExtensionsAndDirectories();
  void rejectsUnsafePaths();
  void reservesConcurrentNamesConservatively();
  void retriesWhenReservedCandidateAppears();
  void treatsCaseAndUnicodeNormalizationAsConflicts();
  void boundsRenameAttempts();
  void resolvesSkipAskAndOverwriteWithoutMutatingTarget();
  void nonAutoPoliciesUseUnoccupiedTarget();
  void refusesDirectoryOverwriteAndReservedOverwrite();
};

void ConflictResolverTests::autoRenamesFilesExtensionsAndDirectories_data()
{
  QTest::addColumn<QString>("relativePath");
  QTest::addColumn<QString>("expectedRenamed");
  QTest::newRow("extension") << QStringLiteral("docs/report.txt") << QStringLiteral("docs/report (1).txt");
  QTest::newRow("multi-extension") << QStringLiteral("archive.tar.gz") << QStringLiteral("archive.tar (1).gz");
  QTest::newRow("hidden-file") << QStringLiteral(".env") << QStringLiteral(".env (1)");
  QTest::newRow("directory") << QStringLiteral("资料") << QStringLiteral("资料 (1)");
}

void ConflictResolverTests::autoRenamesFilesExtensionsAndDirectories()
{
  QFETCH(QString, relativePath);
  QFETCH(QString, expectedRenamed);
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString existing = QDir(root.path()).filePath(relativePath);
  if (relativePath == QStringLiteral("资料")) {
    QVERIFY(QDir().mkpath(existing));
  } else {
    QVERIFY(createFile(existing));
  }
  ConflictResolver resolver;

  const auto decision = resolver.resolve({.targetRoot = root.path(), .relativeProtocolPath = relativePath});

  QVERIFY(std::holds_alternative<UseTarget>(decision));
  QCOMPARE(use(decision).relativeProtocolPath, expectedRenamed);
  QVERIFY(!QFileInfo::exists(use(decision).absolutePath));
  QVERIFY(resolver.release(use(decision).reservationId));
}

void ConflictResolverTests::rejectsUnsafePaths()
{
  QTemporaryDir root;
  ConflictResolver resolver;
  const auto unsafe = resolver.resolve({.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("../x")});
  QVERIFY(std::holds_alternative<ConflictFailure>(unsafe));
  QCOMPARE(std::get<ConflictFailure>(unsafe).error, ConflictResolverError::UnsafePath);
}

void ConflictResolverTests::reservesConcurrentNamesConservatively()
{
  QTemporaryDir root;
  ConflictResolver resolver;
  const ConflictResolveRequest request{.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("same.txt")};
  std::vector<std::future<ConflictDecision>> pending;
  for (int index = 0; index < 8; ++index) {
    pending.emplace_back(std::async(std::launch::async, [&resolver, request]() { return resolver.resolve(request); }));
  }
  QSet<QString> names;
  QList<QUuid> reservations;
  for (auto &future : pending) {
    const auto decision = future.get();
    QVERIFY(std::holds_alternative<UseTarget>(decision));
    names.insert(use(decision).relativeProtocolPath);
    reservations.append(use(decision).reservationId);
  }
  QCOMPARE(names.size(), 8);
  QCOMPARE(resolver.reservationCount(), 8);
  for (const QUuid &reservation : reservations) {
    QVERIFY(resolver.release(reservation));
  }
}

void ConflictResolverTests::retriesWhenReservedCandidateAppears()
{
  QTemporaryDir root;
  ConflictResolver resolver;
  const ConflictResolveRequest request{.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("race.txt")};
  const auto first = resolver.resolve(request);
  QCOMPARE(use(first).relativeProtocolPath, QStringLiteral("race.txt"));
  QVERIFY(createFile(use(first).absolutePath));

  const auto retried = resolver.retry(request, use(first).reservationId);

  QCOMPARE(use(retried).relativeProtocolPath, QStringLiteral("race (1).txt"));
  QCOMPARE(resolver.reservationCount(), 1);
}

void ConflictResolverTests::treatsCaseAndUnicodeNormalizationAsConflicts()
{
  QTemporaryDir root;
  ConflictResolver resolver;
  QVERIFY(createFile(root.filePath(QStringLiteral("Report.TXT"))));
  const auto caseCollision =
      resolver.resolve({.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("report.txt")});
  QCOMPARE(use(caseCollision).relativeProtocolPath, QStringLiteral("report (1).txt"));
  QVERIFY(resolver.release(use(caseCollision).reservationId));

  const QString decomposed = QStringLiteral("Cafe\u0301.txt");
  QVERIFY(createFile(root.filePath(decomposed)));
  const auto unicodeCollision =
      resolver.resolve({.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("Caf\u00e9.txt")});
  QCOMPARE(use(unicodeCollision).relativeProtocolPath, QStringLiteral("Caf\u00e9 (1).txt"));
}

void ConflictResolverTests::boundsRenameAttempts()
{
  QTemporaryDir root;
  QVERIFY(createFile(root.filePath(QStringLiteral("full.bin"))));
  QVERIFY(createFile(root.filePath(QStringLiteral("full (1).bin"))));
  ConflictResolver resolver;
  const auto decision = resolver.resolve(
      {.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("full.bin"), .maximumRenameAttempts = 2}
  );
  QVERIFY(std::holds_alternative<ConflictFailure>(decision));
  QCOMPARE(std::get<ConflictFailure>(decision).error, ConflictResolverError::CandidateExhausted);
}

void ConflictResolverTests::resolvesSkipAskAndOverwriteWithoutMutatingTarget()
{
  QTemporaryDir root;
  const QString target = root.filePath(QStringLiteral("existing.txt"));
  QVERIFY(createFile(target));
  ConflictResolver resolver;

  const auto skipped = resolver.resolve(
      {.targetRoot = root.path(),
       .relativeProtocolPath = QStringLiteral("existing.txt"),
       .policy = ConflictPolicy::Skip}
  );
  QVERIFY(std::holds_alternative<SkipTarget>(skipped));
  QCOMPARE(std::get<SkipTarget>(skipped).absolutePath, target);

  const auto asked = resolver.resolve(
      {.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("existing.txt"), .policy = ConflictPolicy::Ask}
  );
  QVERIFY(std::holds_alternative<AskTarget>(asked));
  QVERIFY(!std::get<AskTarget>(asked).conflictId.isNull());
  QCOMPARE(std::get<AskTarget>(asked).absolutePath, target);

  const auto overwrite = resolver.resolve(
      {.targetRoot = root.path(),
       .relativeProtocolPath = QStringLiteral("existing.txt"),
       .policy = ConflictPolicy::Overwrite}
  );
  QVERIFY(std::holds_alternative<UseTarget>(overwrite));
  QCOMPARE(use(overwrite).commitDisposition, TargetCommitDisposition::ReplaceExisting);
  QVERIFY(QFileInfo::exists(target));
  QVERIFY(resolver.release(use(overwrite).reservationId));
}

void ConflictResolverTests::nonAutoPoliciesUseUnoccupiedTarget()
{
  QTemporaryDir root;
  for (const ConflictPolicy policy : {ConflictPolicy::Skip, ConflictPolicy::Ask, ConflictPolicy::Overwrite}) {
    ConflictResolver resolver;
    const auto decision = resolver.resolve(
        {.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("free.txt"), .policy = policy}
    );
    QVERIFY(std::holds_alternative<UseTarget>(decision));
    QCOMPARE(use(decision).commitDisposition, TargetCommitDisposition::FailIfExists);
    QVERIFY(resolver.release(use(decision).reservationId));
  }
}

void ConflictResolverTests::refusesDirectoryOverwriteAndReservedOverwrite()
{
  QTemporaryDir root;
  QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("folder"))));
  ConflictResolver resolver;
  const auto directory = resolver.resolve(
      {.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("folder"), .policy = ConflictPolicy::Overwrite}
  );
  QVERIFY(std::holds_alternative<ConflictFailure>(directory));
  QCOMPARE(std::get<ConflictFailure>(directory).error, ConflictResolverError::UnsupportedPolicy);

  const auto reserved = resolver.resolve({.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("busy")});
  QVERIFY(std::holds_alternative<UseTarget>(reserved));
  const auto overwrite = resolver.resolve(
      {.targetRoot = root.path(), .relativeProtocolPath = QStringLiteral("busy"), .policy = ConflictPolicy::Overwrite}
  );
  QVERIFY(std::holds_alternative<ConflictFailure>(overwrite));
  QCOMPARE(std::get<ConflictFailure>(overwrite).error, ConflictResolverError::TargetReserved);
}

QTEST_MAIN(ConflictResolverTests)

#include "ConflictResolverTests.moc"
