// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/PartialCleanupPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

constexpr qint64 kNowMs = 1'800'000'000'000LL;

bool writeBytes(const QString &path, qsizetype count)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(QByteArray(count, '\x2a')) == count;
}

ResumeState stateAt(const QString &partRelativePath, quint64 offset, QDateTime updatedUtc)
{
  return {
      .transferId = QUuid::createUuid(),
      .peerDeviceId = deskflow::relaydesk::DeviceId::generate(),
      .manifestSha256 = QByteArray(32, '\x3b'),
      .direction = ResumeDirection::Receiving,
      .files =
          {
              {
                  .fileId = QUuid::createUuid(),
                  .relativeProtocolPath = QStringLiteral("资料/a.bin"),
                  .durableOffset = offset,
                  .totalBytes = 1024,
                  .partRelativePath = partRelativePath,
              },
          },
      .updatedUtc = updatedUtc,
  };
}

} // namespace

class PartialCleanupPolicyTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void listsOnlyExpiredValidKnownPartials();
  void defaultRetentionIsSevenDays();
  void reportsCorruptMissingUnsafeAndMismatchedWithoutDeleting();
  void explicitKeepAndDeleteAreSeparatedAndIdempotent();
  void changedAfterListingIsNeverDeleted();
};

void PartialCleanupPolicyTests::listsOnlyExpiredValidKnownPartials()
{
  QTemporaryDir directory;
  const QString stagingRoot = directory.filePath(QStringLiteral("staging"));
  ResumeStore store(directory.filePath(QStringLiteral("resume")));
  auto expired =
      stateAt(QStringLiteral("expired/a.part"), 8, QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC).addDays(-8));
  auto fresh = stateAt(QStringLiteral("fresh/b.part"), 5, QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC).addDays(-6));
  QVERIFY(writeBytes(QDir(stagingRoot).filePath(expired.files[0].partRelativePath), 8));
  QVERIFY(writeBytes(QDir(stagingRoot).filePath(fresh.files[0].partRelativePath), 5));
  QVERIFY(store.save(expired).ok());
  QVERIFY(store.save(fresh).ok());
  PartialCleanupPolicy policy(store, {.stagingRoot = stagingRoot});

  const auto listed = policy.listExpired(QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC));

  QVERIFY2(listed.ok(), qPrintable(listed.diagnostic));
  QCOMPARE(listed.expired.size(), 1);
  QCOMPARE(listed.expired[0].transferId, expired.transferId);
  QCOMPARE(listed.expired[0].files[0].durableOffset, quint64{8});
  QVERIFY(QFileInfo::exists(listed.expired[0].files[0].partAbsolutePath));
  QVERIFY(QFileInfo::exists(store.statePath(expired.transferId)));
  QVERIFY(QFileInfo::exists(store.statePath(fresh.transferId)));
}

void PartialCleanupPolicyTests::defaultRetentionIsSevenDays()
{
  QCOMPARE(PartialCleanupSettings{}.retention, std::chrono::duration_cast<std::chrono::seconds>(std::chrono::days{7}));
}

void PartialCleanupPolicyTests::reportsCorruptMissingUnsafeAndMismatchedWithoutDeleting()
{
  QTemporaryDir directory;
  const QString stagingRoot = directory.filePath(QStringLiteral("staging"));
  ResumeStore store(directory.filePath(QStringLiteral("resume")));
  const QDateTime old = QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC).addDays(-9);
  auto missing = stateAt(QStringLiteral("missing.part"), 3, old);
  auto mismatched = stateAt(QStringLiteral("mismatch.part"), 4, old);
  QVERIFY(writeBytes(QDir(stagingRoot).filePath(mismatched.files[0].partRelativePath), 3));
  QVERIFY(store.save(missing).ok());
  QVERIFY(store.save(mismatched).ok());
  const QString corruptPath = QDir(directory.filePath(QStringLiteral("resume")))
                                  .filePath(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee.resume.cbor"));
  QVERIFY(writeBytes(corruptPath, 6));
  const QString unknownPart = QDir(stagingRoot).filePath(QStringLiteral("orphan.part"));
  QVERIFY(writeBytes(unknownPart, 7));
  PartialCleanupPolicy policy(store, {.stagingRoot = stagingRoot});

  const auto listed = policy.listExpired(QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC));

  QVERIFY(listed.ok());
  QVERIFY(listed.expired.isEmpty());
  QCOMPARE(listed.issues.size(), 3);
  QVERIFY(QFileInfo::exists(store.statePath(missing.transferId)));
  QVERIFY(QFileInfo::exists(store.statePath(mismatched.transferId)));
  QVERIFY(QFileInfo::exists(corruptPath));
  QVERIFY(QFileInfo::exists(unknownPart));
}

void PartialCleanupPolicyTests::explicitKeepAndDeleteAreSeparatedAndIdempotent()
{
  QTemporaryDir directory;
  const QString stagingRoot = directory.filePath(QStringLiteral("staging"));
  ResumeStore store(directory.filePath(QStringLiteral("resume")));
  auto state = stateAt(
      QStringLiteral("selected/keep-or-delete.part"), 9, QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC).addDays(-8)
  );
  const QString partPath = QDir(stagingRoot).filePath(state.files[0].partRelativePath);
  QVERIFY(writeBytes(partPath, 9));
  QVERIFY(store.save(state).ok());
  PartialCleanupPolicy policy(store, {.stagingRoot = stagingRoot});
  const auto listed = policy.listExpired(QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC));
  QCOMPARE(listed.expired.size(), 1);

  const auto kept = policy.apply(listed.expired[0], PartialCleanupChoice::Keep);
  QVERIFY(kept.ok());
  QCOMPARE(kept.removedPartFiles, 0);
  QVERIFY(QFileInfo::exists(partPath));
  QVERIFY(QFileInfo::exists(store.statePath(state.transferId)));

  const auto removed = policy.apply(listed.expired[0], PartialCleanupChoice::Delete);
  QVERIFY2(removed.ok(), qPrintable(removed.diagnostic));
  QCOMPARE(removed.removedPartFiles, 1);
  QVERIFY(removed.stateRemoved);
  QVERIFY(!QFileInfo::exists(partPath));
  QVERIFY(!QFileInfo::exists(store.statePath(state.transferId)));

  const auto repeated = policy.apply(listed.expired[0], PartialCleanupChoice::Delete);
  QVERIFY(repeated.ok());
  QCOMPARE(repeated.removedPartFiles, 0);
}

void PartialCleanupPolicyTests::changedAfterListingIsNeverDeleted()
{
  QTemporaryDir directory;
  const QString stagingRoot = directory.filePath(QStringLiteral("staging"));
  ResumeStore store(directory.filePath(QStringLiteral("resume")));
  auto state = stateAt(QStringLiteral("changed.part"), 4, QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC).addDays(-8));
  const QString partPath = QDir(stagingRoot).filePath(state.files[0].partRelativePath);
  QVERIFY(writeBytes(partPath, 4));
  QVERIFY(store.save(state).ok());
  PartialCleanupPolicy policy(store, {.stagingRoot = stagingRoot});
  const auto listed = policy.listExpired(QDateTime::fromMSecsSinceEpoch(kNowMs, Qt::UTC));
  QCOMPARE(listed.expired.size(), 1);
  QFile changed(partPath);
  QVERIFY(changed.open(QIODevice::Append));
  QCOMPARE(changed.write("x", 1), qint64{1});
  changed.close();

  const auto result = policy.apply(listed.expired[0], PartialCleanupChoice::Delete);

  QCOMPARE(result.error, PartialCleanupApplyError::ChangedSinceListing);
  QVERIFY(QFileInfo::exists(partPath));
  QVERIFY(QFileInfo::exists(store.statePath(state.transferId)));
}

QTEST_MAIN(PartialCleanupPolicyTests)

#include "PartialCleanupPolicyTests.moc"
