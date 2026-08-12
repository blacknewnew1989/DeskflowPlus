// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace relaydesk::transfer;

namespace {

const auto kNow = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);

TransferHistoryRecord record(const QString &id, int secondsAgo, HistoryStatus status = HistoryStatus::Completed)
{
  const bool failed = status == HistoryStatus::Failed;
  return {
      .transferId = QUuid(id),
      .peerDeviceId =
          *deskflow::relaydesk::DeviceId::fromString(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
      .peerDisplayName = QStringLiteral("Peer"),
      .displayName = QStringLiteral("资料包"),
      .direction = HistoryDirection::Sending,
      .fileCount = 2,
      .totalBytes = 9'007'199'254'740'999ULL,
      .startedUtc = kNow.addSecs(-secondsAgo - 10),
      .finishedUtc = kNow.addSecs(-secondsAgo),
      .status = status,
      .errorCode = failed ? 4008 : 0,
      .errorMessageKey = failed ? QStringLiteral("relaydesk.transfer.io_error") : QString{},
  };
}

void appendBytes(const QString &path, QByteArrayView bytes)
{
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Append), qPrintable(file.errorString()));
  QCOMPARE(file.write(bytes.data(), bytes.size()), bytes.size());
}

} // namespace

class TransferHistoryStoreTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTripsAndPagesNewestFirst();
  void replacesDuplicateTransferAtomically();
  void prunesByAgeAndCount();
  void isolatesCorruptAndOversizedRows();
  void rejectsInvalidRecordsAndLimits();
  void boundsWholeFileAndClearsIdempotently();
};

void TransferHistoryStoreTests::roundTripsAndPagesNewestFirst()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString path = temporary.filePath(QStringLiteral("transfers/history.jsonl"));
  TransferHistoryStore store(path, {}, [] { return kNow; });
  const auto older = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 20);
  const auto newer = record(QStringLiteral("20000000-0000-4000-8000-000000000002"), 10, HistoryStatus::Failed);
  const auto savedOlder = store.append(older);
  QVERIFY2(savedOlder.ok(), qPrintable(savedOlder.diagnostic));
  QVERIFY(store.append(newer).ok());

  const auto firstPage = store.page(0, 1);
  QVERIFY2(firstPage.ok(), qPrintable(firstPage.diagnostic));
  QCOMPARE(firstPage.page.totalValidEntries, 2);
  QCOMPARE(firstPage.page.records, QList<TransferHistoryRecord>{newer});
  QCOMPARE(firstPage.page.issues.size(), 0);

  const auto secondPage = store.page(1, 1);
  QVERIFY(secondPage.ok());
  QCOMPARE(secondPage.page.records, QList<TransferHistoryRecord>{older});
  QCOMPARE(secondPage.page.records.constFirst().totalBytes, older.totalBytes);

  QFile raw(path);
  QVERIFY(raw.open(QIODevice::ReadOnly));
  const QByteArray contents = raw.readAll();
  QVERIFY(!contents.contains("sourcePath"));
  QVERIFY(!contents.contains("C:\\"));
  QVERIFY(contents.endsWith('\n'));
}

void TransferHistoryStoreTests::replacesDuplicateTransferAtomically()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferHistoryStore store(temporary.filePath(QStringLiteral("history.jsonl")), {}, [] { return kNow; });
  auto original = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 20);
  QVERIFY(store.append(original).ok());
  auto replacement = original;
  replacement.status = HistoryStatus::Failed;
  replacement.errorCode = 4008;
  replacement.errorMessageKey = QStringLiteral("relaydesk.transfer.io_error");
  replacement.finishedUtc = kNow;
  QVERIFY(store.append(replacement).ok());

  const auto page = store.page();
  QVERIFY(page.ok());
  QCOMPARE(page.page.totalValidEntries, 1);
  QCOMPARE(page.page.records, QList<TransferHistoryRecord>{replacement});

  QDir directory(temporary.path());
  QCOMPARE(directory.entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("history.jsonl")});
}

void TransferHistoryStoreTests::prunesByAgeAndCount()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferHistoryLimits limits;
  limits.maximumEntries = 3;
  limits.maximumAge = std::chrono::days{2};
  TransferHistoryStore store(temporary.filePath(QStringLiteral("history.jsonl")), limits, [] { return kNow; });

  const auto expired = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 3 * 24 * 60 * 60);
  const auto first = record(QStringLiteral("20000000-0000-4000-8000-000000000002"), 40);
  const auto second = record(QStringLiteral("30000000-0000-4000-8000-000000000003"), 30);
  const auto third = record(QStringLiteral("40000000-0000-4000-8000-000000000004"), 20);
  const auto fourth = record(QStringLiteral("50000000-0000-4000-8000-000000000005"), 10);
  QVERIFY(store.append(expired).ok());
  QVERIFY(store.append(first).ok());
  QVERIFY(store.append(second).ok());
  QVERIFY(store.append(third).ok());
  QVERIFY(store.append(fourth).ok());

  const auto page = store.page(0, 3);
  QVERIFY(page.ok());
  QCOMPARE(page.page.totalValidEntries, 3);
  QCOMPARE(page.page.records, QList<TransferHistoryRecord>({fourth, third, second}));
}

void TransferHistoryStoreTests::isolatesCorruptAndOversizedRows()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferHistoryLimits limits;
  limits.maximumLineBytes = 2'048;
  TransferHistoryStore store(temporary.filePath(QStringLiteral("history.jsonl")), limits, [] { return kNow; });
  const auto valid = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 10);
  QVERIFY(store.append(valid).ok());
  appendBytes(store.historyPath(), QByteArrayLiteral("not-json\n"));
  appendBytes(store.historyPath(), QByteArray(2'049, 'x') + '\n');

  const auto page = store.page();
  QVERIFY(page.ok());
  QCOMPARE(page.page.records, QList<TransferHistoryRecord>{valid});
  QCOMPARE(page.page.issues.size(), 2);
  QCOMPARE(page.page.issues[0].line, 2);
  QCOMPARE(page.page.issues[1].line, 3);

  const auto second = record(QStringLiteral("20000000-0000-4000-8000-000000000002"), 5);
  QVERIFY(store.append(second).ok());
  const auto healed = store.page();
  QVERIFY(healed.ok());
  QCOMPARE(healed.page.issues.size(), 0);
  QCOMPARE(healed.page.totalValidEntries, 2);
}

void TransferHistoryStoreTests::rejectsInvalidRecordsAndLimits()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  TransferHistoryStore store(temporary.filePath(QStringLiteral("history.jsonl")), {}, [] { return kNow; });
  auto invalid = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 10);
  invalid.transferId = QUuid{};
  QCOMPARE(store.append(invalid).error, TransferHistoryError::InvalidRecord);
  invalid = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 10);
  invalid.finishedUtc = invalid.startedUtc.addSecs(-1);
  QCOMPARE(store.append(invalid).error, TransferHistoryError::InvalidRecord);
  invalid = record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 10);
  invalid.status = HistoryStatus::Completed;
  invalid.errorCode = 1;
  QCOMPARE(store.append(invalid).error, TransferHistoryError::InvalidRecord);

  TransferHistoryLimits invalidLimits;
  invalidLimits.maximumEntries = 0;
  TransferHistoryStore limited(temporary.filePath(QStringLiteral("limited.jsonl")), invalidLimits, [] { return kNow; });
  QCOMPARE(
      limited.append(record(QStringLiteral("10000000-0000-4000-8000-000000000001"), 10)).error,
      TransferHistoryError::InvalidLimits
  );
  QCOMPARE(store.page(-1, 10).error, TransferHistoryError::InvalidLimits);
  QCOMPARE(store.page(0, 0).error, TransferHistoryError::InvalidLimits);

  TransferHistoryStore relative(QStringLiteral("history.jsonl"));
  QCOMPARE(relative.page().error, TransferHistoryError::InvalidStorePath);
}

void TransferHistoryStoreTests::boundsWholeFileAndClearsIdempotently()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QString path = temporary.filePath(QStringLiteral("history.jsonl"));
  TransferHistoryLimits limits;
  limits.maximumFileBytes = 512;
  limits.maximumLineBytes = 512;
  TransferHistoryStore store(path, limits, [] { return kNow; });
  appendBytes(path, QByteArray(513, 'x'));
  QCOMPARE(store.page().error, TransferHistoryError::FileTooLarge);

  QVERIFY(store.clear().ok());
  QVERIFY(!QFileInfo::exists(path));
  QVERIFY(store.clear().ok());
  QCOMPARE(store.page().page.totalValidEntries, 0);
}

QTEST_GUILESS_MAIN(TransferHistoryStoreTests)
#include "TransferHistoryStoreTests.moc"
