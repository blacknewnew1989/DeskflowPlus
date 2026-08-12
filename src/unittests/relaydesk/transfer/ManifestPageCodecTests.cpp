// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ManifestPageCodec.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDateTime>
#include <QTest>
#include <QTimeZone>

#include <utility>

using namespace relaydesk::transfer;

namespace {

const TransferId kTransferId(QStringLiteral("01234567-89ab-cdef-8123-456789abcdef"));
const qint64 kModifiedAtMs = 1'730'000'000'000LL;

ManifestEntry makeEntry(
    const QString &path, const QString &uuid, ManifestEntryType type = ManifestEntryType::File, quint64 size = 3,
    const QByteArray &sha256 = QByteArray(32, '\x42')
)
{
  ManifestEntry entry;
  entry.id = QUuid(uuid);
  entry.relativeProtocolPath = path;
  entry.type = type;
  entry.size = type == ManifestEntryType::Directory ? 0 : size;
  entry.modifiedUtc = QDateTime::fromMSecsSinceEpoch(kModifiedAtMs, QTimeZone::UTC);
  entry.sha256 = type == ManifestEntryType::Directory ? QByteArray() : sha256;
  return entry;
}

TransferManifest makeManifest()
{
  TransferManifest manifest;
  manifest.summary.id = kTransferId;
  manifest.summary.displayName = QStringLiteral("Root");
  const QList<ManifestEntry> entries{
      makeEntry(
          QStringLiteral("Root"), QStringLiteral("10000000-0000-8000-8000-000000000001"), ManifestEntryType::Directory
      ),
      makeEntry(QStringLiteral("Root/a.txt"), QStringLiteral("20000000-0000-8000-8000-000000000002")),
      makeEntry(
          QStringLiteral("Root/数据 😀.bin"), QStringLiteral("30000000-0000-8000-8000-000000000003"),
          ManifestEntryType::File, 4, QByteArray(32, '\x24')
      ),
  };
  for (const ManifestEntry &entry : entries) {
    manifest.entries.append({.entry = entry});
  }
  manifest.summary.fileCount = 2;
  manifest.summary.directoryCount = 1;
  manifest.summary.totalBytes = 7;
  manifest.summary.canonicalSha256 = ManifestPageCodec::canonicalSha256(entries);
  return manifest;
}

ManifestPagingLimits twoEntryPages()
{
  ManifestPagingLimits limits;
  limits.maxEntriesPerPage = 2;
  return limits;
}

QList<QByteArray>
encodePlannedPages(const TransferManifest &manifest, const ManifestPagePlan &plan, const ManifestPagingLimits &limits)
{
  QList<QByteArray> pages;
  for (quint64 index = 0; index < plan.pageCount(); ++index) {
    QString error;
    const QByteArray encoded = ManifestPageCodec::encodePage(manifest, plan, index, limits, &error);
    if (encoded.isEmpty()) {
      return {};
    }
    pages.append(encoded);
  }
  return pages;
}

QByteArray replacePageField(const QByteArray &encoded, const QCborValue &key, const QCborValue &value)
{
  QCborMap map = QCborValue::fromCbor(encoded).toMap();
  map.insert(key, value);
  return QCborValue(map).toCbor(QCborValue::SortKeysInMaps);
}

} // namespace

class ManifestPageCodecTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void canonicalDigestMatchesFILE015Vector();
  void plansDeterministicBoundedPagesAndReassembles();
  void encodedPageMatchesFrozenVector();
  void plannerEnforcesEntryPageAndMetadataLimits();
  void encoderRejectsForgedPagePlan();
  void codecRoundTripsAndIgnoresUnknownIntegerKeys();
  void manifestCompleteRoundTripsAndFinalizes();
  void decoderRejectsMalformedShapeAndEntryLimits();
  void reassemblerRejectsOutOfOrderDuplicateAndMissingPages();
  void reassemblerRejectsIdentityCountDigestAndTotalMetadataMismatch();
  void reassemblerRejectsPathCollisionsAndDuplicateFileIds();
  void pagesThousandsOfEntriesWithinBounds();
};

void ManifestPageCodecTests::canonicalDigestMatchesFILE015Vector()
{
  const ManifestEntry entry = makeEntry(
      QStringLiteral("vector.txt"), QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"), ManifestEntryType::File, 3,
      QByteArray::fromHex(QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
  );

  QString error;
  const QByteArray digest = ManifestPageCodec::canonicalSha256({entry}, &error);

  QVERIFY2(!digest.isEmpty(), qPrintable(error));
  QCOMPARE(digest.toHex(), QByteArrayLiteral("7fdd103e0b3fd12dd6762a609fe21b1d7cdb97962096922fa379a667283ec518"));
}

void ManifestPageCodecTests::plansDeterministicBoundedPagesAndReassembles()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();

  const auto firstPlan = ManifestPageCodec::plan(manifest, limits);
  const auto secondPlan = ManifestPageCodec::plan(manifest, limits);

  QVERIFY2(firstPlan.ok(), qPrintable(firstPlan.diagnostic));
  QVERIFY2(secondPlan.ok(), qPrintable(secondPlan.diagnostic));
  QCOMPARE(firstPlan.plan->ranges, secondPlan.plan->ranges);
  QCOMPARE(firstPlan.plan->pageCount(), 2);
  QCOMPARE(firstPlan.plan->entryCount, 3);
  const QList<QByteArray> pages = encodePlannedPages(manifest, *firstPlan.plan, limits);
  QCOMPARE(pages.size(), 2);
  QCOMPARE(static_cast<quint64>(pages.at(0).size() + pages.at(1).size()), firstPlan.plan->totalMetadataBytes);
  for (const QByteArray &page : pages) {
    QVERIFY(static_cast<quint64>(page.size()) <= limits.maxPageMetadataBytes);
  }

  ManifestPageReassembler reassembler(
      kTransferId, firstPlan.plan->pageCount(), manifest.entries.size(), manifest.summary.canonicalSha256, limits
  );
  for (const QByteArray &page : pages) {
    QString diagnostic;
    QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, page, &diagnostic), ManifestPageError::None);
    QVERIFY2(diagnostic.isEmpty(), qPrintable(diagnostic));
  }
  auto result = reassembler.finish();
  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.entries->size(), manifest.entries.size());
  for (qsizetype index = 0; index < result.entries->size(); ++index) {
    QCOMPARE(result.entries->at(index), manifest.entries.at(index).entry);
  }
}

void ManifestPageCodecTests::encodedPageMatchesFrozenVector()
{
  TransferManifest manifest;
  manifest.summary.id = kTransferId;
  manifest.summary.displayName = QStringLiteral("vector.txt");
  const ManifestEntry entry = makeEntry(
      QStringLiteral("vector.txt"), QStringLiteral("fedcba98-7654-4321-9234-56789abcdef0"), ManifestEntryType::File, 3,
      QByteArray::fromHex(QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
  );
  manifest.entries.append({.entry = entry});
  manifest.summary.fileCount = 1;
  manifest.summary.totalBytes = 3;
  manifest.summary.canonicalSha256 = ManifestPageCodec::canonicalSha256({entry});
  const auto plan = ManifestPageCodec::plan(manifest);
  QVERIFY2(plan.ok(), qPrintable(plan.diagnostic));

  QString error;
  const QByteArray encoded = ManifestPageCodec::encodePage(manifest, *plan.plan, 0, {}, &error);

  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  QCOMPARE(
      encoded.toHex(),
      QByteArrayLiteral(
          "a401500123456789abcdef8123456789abcdef020003010481a70150fedcba9876544321923456789abcdef0026a7665"
          "63746f722e74787403000403051b00000192cc091400065820ba7816bf8f01cfea414140de5dae2223b00361a396177"
          "a9cb410ff61f20015ad0700"
      )
  );
}

void ManifestPageCodecTests::plannerEnforcesEntryPageAndMetadataLimits()
{
  const TransferManifest manifest = makeManifest();
  ManifestPagingLimits limits;
  limits.maxEntries = 2;
  QCOMPARE(ManifestPageCodec::plan(manifest, limits).error, ManifestPageError::TooManyEntries);

  limits = {};
  limits.maxEntriesPerPage = 1;
  limits.maxPages = 2;
  QCOMPARE(ManifestPageCodec::plan(manifest, limits).error, ManifestPageError::TooManyPages);

  limits = {};
  limits.maxManifestMetadataBytes = 64;
  QCOMPARE(ManifestPageCodec::plan(manifest, limits).error, ManifestPageError::ManifestMetadataTooLarge);

  limits = {};
  limits.maxPageMetadataBytes = 64;
  QCOMPARE(ManifestPageCodec::plan(manifest, limits).error, ManifestPageError::EntryTooLarge);

  limits = {};
  limits.maxEntries = kMaxPagedManifestEntries + 1;
  QCOMPARE(ManifestPageCodec::plan(manifest, limits).error, ManifestPageError::InvalidLimits);

  TransferManifest invalidManifest = manifest;
  invalidManifest.summary.canonicalSha256 = QByteArray(32, '\x11');
  QCOMPARE(ManifestPageCodec::plan(invalidManifest).error, ManifestPageError::DigestMismatch);

  invalidManifest = manifest;
  invalidManifest.summary.fileCount += 1;
  QCOMPARE(ManifestPageCodec::plan(invalidManifest).error, ManifestPageError::InvalidManifestEntry);

  invalidManifest = manifest;
  std::swap(invalidManifest.entries[0], invalidManifest.entries[1]);
  QCOMPARE(ManifestPageCodec::plan(invalidManifest).error, ManifestPageError::InvalidManifestOrder);
}

void ManifestPageCodecTests::encoderRejectsForgedPagePlan()
{
  const TransferManifest manifest = makeManifest();
  ManifestPagePlan forged;
  forged.transferId = kTransferId;
  forged.entryCount = manifest.entries.size();
  forged.ranges.append({.firstEntry = 2, .entryCount = 2});
  QString error;

  QVERIFY(ManifestPageCodec::encodePage(manifest, forged, 0, {}, &error).isEmpty());
  QVERIFY(!error.isEmpty());
}

void ManifestPageCodecTests::codecRoundTripsAndIgnoresUnknownIntegerKeys()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY(plan.ok());
  const QByteArray encoded = ManifestPageCodec::encodePage(manifest, *plan.plan, 0, limits);
  QVERIFY(!encoded.isEmpty());

  const auto decoded = ManifestPageCodec::decode(kProtocolMajorVersion, encoded, limits);
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(decoded.page->transferId, kTransferId);
  QCOMPARE(decoded.page->pageIndex, 0);
  QCOMPARE(decoded.page->pageCount, 2);
  QCOMPARE(decoded.page->entries.size(), 2);

  const QByteArray withOptional = replacePageField(encoded, QCborValue(99), QCborValue(QStringLiteral("future")));
  const auto optionalDecoded = ManifestPageCodec::decode(kProtocolMajorVersion, withOptional, limits);
  QVERIFY2(optionalDecoded.ok(), qPrintable(optionalDecoded.diagnostic));
  QCOMPARE(optionalDecoded.page->entries, decoded.page->entries);
}

void ManifestPageCodecTests::manifestCompleteRoundTripsAndFinalizes()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY(plan.ok());
  const QList<QByteArray> pages = encodePlannedPages(manifest, *plan.plan, limits);

  const ManifestComplete complete{
      .transferId = kTransferId,
      .canonicalSha256 = manifest.summary.canonicalSha256,
  };
  QString error;
  const QByteArray encoded = ManifestPageCodec::encodeComplete(complete, &error);
  QVERIFY2(!encoded.isEmpty(), qPrintable(error));
  const auto decoded = ManifestPageCodec::decodeComplete(kProtocolMajorVersion, encoded);
  QVERIFY2(decoded.ok(), qPrintable(decoded.diagnostic));
  QCOMPARE(*decoded.message, complete);

  ManifestPageReassembler reassembler(
      kTransferId, plan.plan->pageCount(), manifest.entries.size(), manifest.summary.canonicalSha256, limits
  );
  for (const QByteArray &page : pages) {
    QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, page), ManifestPageError::None);
  }
  QVERIFY(reassembler.finish(*decoded.message).ok());

  QCOMPARE(
      ManifestPageCodec::decodeComplete(kProtocolMajorVersion + 1, encoded).error, ManifestPageError::UnsupportedVersion
  );
  QCOMPARE(
      ManifestPageCodec::decodeComplete(kProtocolMajorVersion, encoded + '\0').error, ManifestPageError::MalformedCbor
  );
  QCborMap invalidDigest = QCborValue::fromCbor(encoded).toMap();
  invalidDigest.insert(QCborValue(2), QCborValue(QByteArray(31, '\x11')));
  QCOMPARE(
      ManifestPageCodec::decodeComplete(
          kProtocolMajorVersion, QCborValue(invalidDigest).toCbor(QCborValue::SortKeysInMaps)
      )
          .error,
      ManifestPageError::InvalidFieldValue
  );
  invalidDigest.remove(QCborValue(2));
  QCOMPARE(
      ManifestPageCodec::decodeComplete(
          kProtocolMajorVersion, QCborValue(invalidDigest).toCbor(QCborValue::SortKeysInMaps)
      )
          .error,
      ManifestPageError::MissingField
  );

  ManifestPageReassembler wrongComplete(
      kTransferId, plan.plan->pageCount(), manifest.entries.size(), manifest.summary.canonicalSha256, limits
  );
  ManifestComplete wrongTransfer = complete;
  wrongTransfer.transferId = QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));
  QCOMPARE(wrongComplete.finish(wrongTransfer).error, ManifestPageError::TransferMismatch);
  ManifestComplete wrongDigest = complete;
  wrongDigest.canonicalSha256 = QByteArray(32, '\x11');
  QCOMPARE(wrongComplete.finish(wrongDigest).error, ManifestPageError::DigestMismatch);
}

void ManifestPageCodecTests::decoderRejectsMalformedShapeAndEntryLimits()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY(plan.ok());
  const QByteArray encoded = ManifestPageCodec::encodePage(manifest, *plan.plan, 0, limits);

  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion + 1, encoded, limits).error, ManifestPageError::UnsupportedVersion
  );
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, encoded + '\0', limits).error, ManifestPageError::MalformedCbor
  );
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, QCborValue(7).toCbor(), limits).error,
      ManifestPageError::MetadataNotMap
  );
  QCOMPARE(
      ManifestPageCodec::decode(
          kProtocolMajorVersion, replacePageField(encoded, QCborValue(QStringLiteral("bad")), QCborValue(1)), limits
      )
          .error,
      ManifestPageError::NonIntegerKey
  );

  QCborMap missing = QCborValue::fromCbor(encoded).toMap();
  missing.remove(QCborValue(1));
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, QCborValue(missing).toCbor(QCborValue::SortKeysInMaps), limits)
          .error,
      ManifestPageError::MissingField
  );
  QCOMPARE(
      ManifestPageCodec::decode(
          kProtocolMajorVersion, replacePageField(encoded, QCborValue(2), QCborValue(QStringLiteral("zero"))), limits
      )
          .error,
      ManifestPageError::InvalidFieldType
  );
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, replacePageField(encoded, QCborValue(2), QCborValue(2)), limits)
          .error,
      ManifestPageError::InvalidFieldValue
  );

  ManifestPagingLimits oneEntry;
  oneEntry.maxEntriesPerPage = 1;
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, encoded, oneEntry).error, ManifestPageError::InvalidFieldValue
  );

  QCborMap unsafe = QCborValue::fromCbor(encoded).toMap();
  QCborArray entries = unsafe.value(QCborValue(4)).toArray();
  QCborMap unsafeEntry = entries.at(0).toMap();
  unsafeEntry.insert(QCborValue(2), QCborValue(QStringLiteral("../escape")));
  entries[0] = unsafeEntry;
  unsafe.insert(QCborValue(4), entries);
  QCOMPARE(
      ManifestPageCodec::decode(kProtocolMajorVersion, QCborValue(unsafe).toCbor(QCborValue::SortKeysInMaps), limits)
          .error,
      ManifestPageError::InvalidManifestEntry
  );
}

void ManifestPageCodecTests::reassemblerRejectsOutOfOrderDuplicateAndMissingPages()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY(plan.ok());
  const QList<QByteArray> pages = encodePlannedPages(manifest, *plan.plan, limits);
  ManifestPageReassembler reassembler(
      kTransferId, plan.plan->pageCount(), manifest.entries.size(), manifest.summary.canonicalSha256, limits
  );

  QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, pages.at(1)), ManifestPageError::OutOfOrderPage);
  QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, pages.at(0)), ManifestPageError::None);
  QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, pages.at(0)), ManifestPageError::DuplicatePage);
  QCOMPARE(reassembler.finish().error, ManifestPageError::MissingPage);
  QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, pages.at(1)), ManifestPageError::None);
  QVERIFY(reassembler.finish().ok());
  QCOMPARE(reassembler.finish().error, ManifestPageError::AlreadyComplete);
}

void ManifestPageCodecTests::reassemblerRejectsIdentityCountDigestAndTotalMetadataMismatch()
{
  const TransferManifest manifest = makeManifest();
  const ManifestPagingLimits limits = twoEntryPages();
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY(plan.ok());
  const QList<QByteArray> pages = encodePlannedPages(manifest, *plan.plan, limits);

  ManifestPageReassembler wrongTransfer(
      QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")), plan.plan->pageCount(), manifest.entries.size(),
      manifest.summary.canonicalSha256, limits
  );
  QCOMPARE(wrongTransfer.addEncodedPage(kProtocolMajorVersion, pages.at(0)), ManifestPageError::TransferMismatch);

  ManifestPageReassembler wrongPages(
      kTransferId, plan.plan->pageCount() + 1, manifest.entries.size(), manifest.summary.canonicalSha256, limits
  );
  QCOMPARE(wrongPages.addEncodedPage(kProtocolMajorVersion, pages.at(0)), ManifestPageError::PageCountMismatch);

  ManifestPageReassembler wrongCount(
      kTransferId, plan.plan->pageCount(), manifest.entries.size() + 1, manifest.summary.canonicalSha256, limits
  );
  for (const QByteArray &page : pages) {
    QCOMPARE(wrongCount.addEncodedPage(kProtocolMajorVersion, page), ManifestPageError::None);
  }
  QCOMPARE(wrongCount.finish().error, ManifestPageError::EntryCountMismatch);

  ManifestPageReassembler wrongDigest(
      kTransferId, plan.plan->pageCount(), manifest.entries.size(), QByteArray(32, '\x11'), limits
  );
  for (const QByteArray &page : pages) {
    QCOMPARE(wrongDigest.addEncodedPage(kProtocolMajorVersion, page), ManifestPageError::None);
  }
  QCOMPARE(wrongDigest.finish().error, ManifestPageError::DigestMismatch);

  ManifestPagingLimits totalLimit = limits;
  totalLimit.maxManifestMetadataBytes = static_cast<quint64>(pages.at(0).size() + pages.at(1).size() - 1);
  ManifestPageReassembler metadataLimit(
      kTransferId, plan.plan->pageCount(), manifest.entries.size(), manifest.summary.canonicalSha256, totalLimit
  );
  QCOMPARE(metadataLimit.addEncodedPage(kProtocolMajorVersion, pages.at(0)), ManifestPageError::None);
  QCOMPARE(
      metadataLimit.addEncodedPage(kProtocolMajorVersion, pages.at(1)), ManifestPageError::ManifestMetadataTooLarge
  );
}

void ManifestPageCodecTests::reassemblerRejectsPathCollisionsAndDuplicateFileIds()
{
  const ManifestEntry first =
      makeEntry(QStringLiteral("Data/a.txt"), QStringLiteral("10000000-0000-8000-8000-000000000001"));
  ManifestEntry collision =
      makeEntry(QStringLiteral("data/A.TXT"), QStringLiteral("20000000-0000-8000-8000-000000000002"));
  ManifestPage collisionPage{.transferId = kTransferId, .pageIndex = 0, .pageCount = 1, .entries = {first, collision}};
  const QByteArray collisionDigest = ManifestPageCodec::canonicalSha256({first, collision});
  ManifestPageReassembler collisionAssembler(kTransferId, 1, 2, collisionDigest);
  QCOMPARE(collisionAssembler.addPage(collisionPage), ManifestPageError::ProtocolPathCollision);

  collision.id = first.id;
  collision.relativeProtocolPath = QStringLiteral("data/b.txt");
  ManifestPage duplicateIdPage{
      .transferId = kTransferId, .pageIndex = 0, .pageCount = 1, .entries = {first, collision}
  };
  const QByteArray duplicateDigest = ManifestPageCodec::canonicalSha256({first, collision});
  ManifestPageReassembler duplicateAssembler(kTransferId, 1, 2, duplicateDigest);
  QCOMPARE(duplicateAssembler.addPage(duplicateIdPage), ManifestPageError::DuplicateFileId);
}

void ManifestPageCodecTests::pagesThousandsOfEntriesWithinBounds()
{
  constexpr qsizetype kEntryCount = 5'000;
  TransferManifest manifest;
  manifest.summary.id = kTransferId;
  manifest.summary.displayName = QStringLiteral("bulk");
  QList<ManifestEntry> entries;
  entries.reserve(kEntryCount);
  manifest.entries.reserve(kEntryCount);
  for (qsizetype index = 0; index < kEntryCount; ++index) {
    const QString suffix = QStringLiteral("%1").arg(index + 1, 12, 16, QLatin1Char('0'));
    ManifestEntry entry = makeEntry(
        QStringLiteral("bulk/file-%1.bin").arg(index, 5, 10, QLatin1Char('0')),
        QStringLiteral("10000000-0000-8000-8000-%1").arg(suffix), ManifestEntryType::File, 0,
        QByteArray(32, static_cast<char>(index & 0xff))
    );
    entries.append(entry);
    manifest.entries.append({.entry = std::move(entry)});
  }
  manifest.summary.fileCount = kEntryCount;
  manifest.summary.canonicalSha256 = ManifestPageCodec::canonicalSha256(entries);

  ManifestPagingLimits limits;
  limits.maxEntriesPerPage = 128;
  const auto plan = ManifestPageCodec::plan(manifest, limits);
  QVERIFY2(plan.ok(), qPrintable(plan.diagnostic));
  QCOMPARE(plan.plan->entryCount, kEntryCount);
  QCOMPARE(plan.plan->pageCount(), 40);

  ManifestPageReassembler reassembler(
      kTransferId, plan.plan->pageCount(), kEntryCount, manifest.summary.canonicalSha256, limits
  );
  quint64 totalMetadataBytes = 0;
  for (quint64 pageIndex = 0; pageIndex < plan.plan->pageCount(); ++pageIndex) {
    QString error;
    const QByteArray encoded = ManifestPageCodec::encodePage(manifest, *plan.plan, pageIndex, limits, &error);
    QVERIFY2(!encoded.isEmpty(), qPrintable(error));
    QVERIFY(static_cast<quint64>(encoded.size()) <= limits.maxPageMetadataBytes);
    totalMetadataBytes += static_cast<quint64>(encoded.size());
    QCOMPARE(reassembler.addEncodedPage(kProtocolMajorVersion, encoded), ManifestPageError::None);
  }
  QCOMPARE(totalMetadataBytes, plan.plan->totalMetadataBytes);
  QVERIFY(reassembler.finish().ok());
}

QTEST_MAIN(ManifestPageCodecTests)

#include "ManifestPageCodecTests.moc"
