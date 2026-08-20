// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSettings.h"

#include <QDataStream>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace relaydesk::transfer;

namespace {

struct FailingSettingsFormat
{
  static inline bool failWrite = false;
  static inline QSettings::SettingsMap attempted;

  static bool read(QIODevice &device, QSettings::SettingsMap &map)
  {
    if (device.atEnd())
      return true;
    QDataStream stream(&device);
    stream >> map;
    return stream.status() == QDataStream::Ok;
  }

  static bool write(QIODevice &device, const QSettings::SettingsMap &map)
  {
    attempted = map;
    if (failWrite)
      return false;
    QDataStream stream(&device);
    stream << map;
    return stream.status() == QDataStream::Ok;
  }

  static QSettings::Format format()
  {
    static const auto value = QSettings::registerFormat(QStringLiteral("relaydesk-transfer-test"), read, write);
    return value;
  }
};

} // namespace

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
  void syncFailureKeepsPreviouslyPersistedSettings();
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
      settings.value(TransferSettingsStore::receiveRootKey()).toString(), temporary.filePath(QStringLiteral("partial"))
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

void TransferSettingsTests::syncFailureKeepsPreviouslyPersistedSettings()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto path = temporary.filePath(QStringLiteral("settings.relaydesk-transfer-test"));
  const TransferSettings previous{
      .receiveRoot = temporary.filePath(QStringLiteral("previous")),
      .incomingPolicy = IncomingTransferPolicy::Ask,
      .defaultConflictPolicy = ConflictPolicy::AutoRename,
  };
  const TransferSettings replacement{
      .receiveRoot = temporary.filePath(QStringLiteral("replacement")),
      .incomingPolicy = IncomingTransferPolicy::AutoAcceptTrusted,
      .defaultConflictPolicy = ConflictPolicy::Overwrite,
  };

  {
    QSettings settings(path, FailingSettingsFormat::format());
    TransferSettingsStore store(settings);
    QString diagnostic;
    QVERIFY2(store.save(previous, &diagnostic), qPrintable(diagnostic));

    FailingSettingsFormat::attempted.clear();
    FailingSettingsFormat::failWrite = true;
    QVERIFY(!store.save(replacement, &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
    QCOMPARE(
        FailingSettingsFormat::attempted.value(TransferSettingsStore::receiveRootKey()).toString(),
        QDir::cleanPath(replacement.receiveRoot)
    );
    const auto restored = store.load();
    QVERIFY2(restored.ok, qPrintable(restored.diagnostic));
    QCOMPARE(restored.settings.receiveRoot, QDir::cleanPath(previous.receiveRoot));
    QCOMPARE(restored.settings.incomingPolicy, previous.incomingPolicy);
    QCOMPARE(restored.settings.defaultConflictPolicy, previous.defaultConflictPolicy);
    FailingSettingsFormat::failWrite = false;
  }

  QSettings reopened(path, FailingSettingsFormat::format());
  TransferSettingsStore reopenedStore(reopened);
  const auto persisted = reopenedStore.load();
  QVERIFY2(persisted.ok, qPrintable(persisted.diagnostic));
  QCOMPARE(persisted.settings.receiveRoot, QDir::cleanPath(previous.receiveRoot));
  QCOMPARE(persisted.settings.incomingPolicy, previous.incomingPolicy);
  QCOMPARE(persisted.settings.defaultConflictPolicy, previous.defaultConflictPolicy);
}

QTEST_MAIN(TransferSettingsTests)

#include "TransferSettingsTests.moc"
