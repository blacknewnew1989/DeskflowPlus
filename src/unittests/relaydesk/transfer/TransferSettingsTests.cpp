// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::transfer;

class TransferSettingsTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void defaultsToDownloadsRelayDeskAndAsk();
  void roundTripsNormalizedSettings();
  void rejectsUnsafeReceiveRootsAndInvalidPersistedValues();
  void rejectsPartialSettingsWithoutSchemaAndPreservesThem();
  void saveRefusesToOverwriteUnreadableSettings();
  void syncFailureRestoresPreviousSettings();
};

void TransferSettingsTests::defaultsToDownloadsRelayDeskAndAsk()
{
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  TransferSettingsStore store(settings);
  const auto loaded = store.load();
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostic));
  QCOMPARE(loaded.settings.receiveRoot, defaultReceiveRoot());
  QVERIFY(loaded.settings.receiveRoot.endsWith(QStringLiteral("/RelayDesk")));
  QCOMPARE(loaded.settings.incomingPolicy, IncomingTransferPolicy::Ask);
  QVERIFY(!loaded.settings.autoAcceptTrusted());
  QCOMPARE(loaded.settings.defaultConflictPolicy, ConflictPolicy::AutoRename);
}

void TransferSettingsTests::roundTripsNormalizedSettings()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  TransferSettingsStore store(settings);
  TransferSettings expected{
      .receiveRoot = temporary.filePath(QStringLiteral("receive/../receive")),
      .incomingPolicy = IncomingTransferPolicy::AutoAcceptTrusted,
      .defaultConflictPolicy = ConflictPolicy::Ask,
  };
  QString diagnostic;
  QVERIFY2(store.save(expected, &diagnostic), qPrintable(diagnostic));
  const auto loaded = store.load();
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostic));
  QCOMPARE(loaded.settings.receiveRoot, QDir::cleanPath(temporary.filePath(QStringLiteral("receive"))));
  QCOMPARE(loaded.settings.incomingPolicy, IncomingTransferPolicy::AutoAcceptTrusted);
  QVERIFY(loaded.settings.autoAcceptTrusted());
  QCOMPARE(loaded.settings.defaultConflictPolicy, ConflictPolicy::Ask);
  QCOMPARE(settings.value(TransferSettingsStore::schemaVersionKey()).toInt(), kTransferSettingsSchemaVersion);
}

void TransferSettingsTests::rejectsUnsafeReceiveRootsAndInvalidPersistedValues()
{
  QString diagnostic;
  for (QString root : {QString(), QStringLiteral("relative"), QDir::rootPath()}) {
    QVERIFY(!validateReceiveRoot(root, &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
  }
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(TransferSettingsStore::schemaVersionKey(), kTransferSettingsSchemaVersion);
  settings.setValue(TransferSettingsStore::receiveRootKey(), QStringLiteral("relative"));
  settings.setValue(TransferSettingsStore::incomingPolicyKey(), QStringLiteral("autoAcceptTrusted"));
  settings.setValue(TransferSettingsStore::defaultConflictPolicyKey(), QStringLiteral("autoRename"));
  TransferSettingsStore store(settings);
  QVERIFY(!store.load().ok);
  settings.setValue(TransferSettingsStore::receiveRootKey(), temporary.path());
  settings.setValue(TransferSettingsStore::incomingPolicyKey(), QStringLiteral("always"));
  QVERIFY(!store.load().ok);
}

void TransferSettingsTests::rejectsPartialSettingsWithoutSchemaAndPreservesThem()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(TransferSettingsStore::receiveRootKey(), temporary.filePath(QStringLiteral("partial")));
  TransferSettingsStore store(settings);
  const auto loaded = store.load();
  QVERIFY(!loaded.ok);
  QString diagnostic;
  QVERIFY(!store.save({.receiveRoot = temporary.path()}, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QVERIFY(!settings.contains(TransferSettingsStore::schemaVersionKey()));
  QCOMPARE(
      settings.value(TransferSettingsStore::receiveRootKey()).toString(),
      temporary.filePath(QStringLiteral("partial"))
  );
}

void TransferSettingsTests::saveRefusesToOverwriteUnreadableSettings()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
  settings.setValue(TransferSettingsStore::schemaVersionKey(), kTransferSettingsSchemaVersion + 1);
  TransferSettingsStore store(settings);
  QString diagnostic;
  QVERIFY(!store.save({.receiveRoot = temporary.path()}, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QCOMPARE(settings.value(TransferSettingsStore::schemaVersionKey()).toInt(), kTransferSettingsSchemaVersion + 1);
}

void TransferSettingsTests::syncFailureRestoresPreviousSettings()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QSettings settings(temporary.path(), QSettings::IniFormat);
  TransferSettingsStore store(settings);
  QString diagnostic;
  QVERIFY(!store.save({.receiveRoot = temporary.filePath(QStringLiteral("receive"))}, &diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QVERIFY(!settings.contains(TransferSettingsStore::schemaVersionKey()));
  QVERIFY(!settings.contains(TransferSettingsStore::receiveRootKey()));
  QVERIFY(!settings.contains(TransferSettingsStore::incomingPolicyKey()));
  QVERIFY(!settings.contains(TransferSettingsStore::defaultConflictPolicyKey()));
}

QTEST_MAIN(TransferSettingsTests)

#include "TransferSettingsTests.moc"
