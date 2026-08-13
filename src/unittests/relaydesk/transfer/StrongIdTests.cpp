// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileId.h"
#include "relaydesk/transfer/TransferId.h"

#include <QHash>
#include <QMetaType>
#include <QtTest>

#include <type_traits>

using namespace relaydesk::transfer;

static_assert(!std::is_same_v<TransferId, FileId>);
static_assert(!std::is_same_v<TransferId, QUuid>);
static_assert(!std::is_same_v<FileId, QUuid>);
static_assert(!std::is_default_constructible_v<TransferId>);
static_assert(!std::is_default_constructible_v<FileId>);
static_assert(!std::is_constructible_v<TransferId, QUuid>);
static_assert(!std::is_constructible_v<FileId, QUuid>);
static_assert(!std::is_convertible_v<QUuid, TransferId>);
static_assert(!std::is_convertible_v<QUuid, FileId>);

class StrongIdTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void transferIdRoundTripsCanonicalRepresentations();
  void fileIdRoundTripsCanonicalRepresentations();
  void factoriesRejectInvalidAndNullValues();
  void valuesAreHashableAndRegisteredWithQt();
};

void StrongIdTests::transferIdRoundTripsCanonicalRepresentations()
{
  const auto id = TransferId::generate();
  QCOMPARE(id.toBytes().size(), 16);
  QCOMPARE(TransferId::fromBytes(id.toBytes()), std::optional<TransferId>{id});
  QCOMPARE(TransferId::fromString(id.toString()), std::optional<TransferId>{id});
  QCOMPARE(id.toString().size(), 36);
}

void StrongIdTests::fileIdRoundTripsCanonicalRepresentations()
{
  const auto id = FileId::generate();
  QCOMPARE(id.toBytes().size(), 16);
  QCOMPARE(FileId::fromBytes(id.toBytes()), std::optional<FileId>{id});
  QCOMPARE(FileId::fromString(id.toString()), std::optional<FileId>{id});
  QCOMPARE(id.toString().size(), 36);
}

void StrongIdTests::factoriesRejectInvalidAndNullValues()
{
  const QByteArray nullBytes(16, '\0');
  QVERIFY(!TransferId::fromBytes({}).has_value());
  QVERIFY(!TransferId::fromBytes(QByteArray(15, '\x01')).has_value());
  QVERIFY(!TransferId::fromBytes(nullBytes).has_value());
  QVERIFY(!TransferId::fromString({}).has_value());
  QVERIFY(!TransferId::fromString(QStringLiteral("{4daeb3d0-e2ee-4f92-a976-70d7cc620d80}")).has_value());
  QVERIFY(!FileId::fromBytes({}).has_value());
  QVERIFY(!FileId::fromBytes(QByteArray(17, '\x01')).has_value());
  QVERIFY(!FileId::fromBytes(nullBytes).has_value());
  QVERIFY(!FileId::fromString(QStringLiteral("not-a-uuid")).has_value());
  QVERIFY(!FileId::fromString(QStringLiteral("00000000-0000-0000-0000-000000000000")).has_value());
}

void StrongIdTests::valuesAreHashableAndRegisteredWithQt()
{
  const auto transferId = TransferId::generate();
  const auto fileId = FileId::generate();
  QHash<TransferId, QString> transfers;
  QHash<FileId, QString> files;
  transfers.insert(transferId, QStringLiteral("transfer"));
  files.insert(fileId, QStringLiteral("file"));
  QCOMPARE(transfers.value(transferId), QStringLiteral("transfer"));
  QCOMPARE(files.value(fileId), QStringLiteral("file"));
  QVERIFY(QMetaType::fromType<TransferId>().isValid());
  QVERIFY(QMetaType::fromType<FileId>().isValid());
}

QTEST_GUILESS_MAIN(StrongIdTests)
#include "StrongIdTests.moc"
