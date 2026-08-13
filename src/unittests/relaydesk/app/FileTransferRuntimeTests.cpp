/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/trust/TrustedDeviceStore.h"
#include "../TestTlsIdentity.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

class FileTransferRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void listenerLifecycleIsOwnedAndRestartable();
  void invalidIdentityFailsWithoutPublishingAListener();
};

void FileTransferRuntimeTests::listenerLifecycleIsOwnedAndRestartable()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto identityPath = ::relaydesk::test::writeTlsIdentity(directory);
  QVERIFY(!identityPath.isEmpty());

  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  DiscoveryRegistry registry(localId);
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(localId, trust, registry, identityPath, options);
  QSignalSpy started(&runtime, &FileTransferRuntime::started);
  QSignalSpy stopped(&runtime, &FileTransferRuntime::stopped);
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  const quint16 firstPort = runtime.listeningPort();
  QVERIFY(firstPort != 0);
  QCOMPARE(started.count(), 1);
  QVERIFY(runtime.start(&diagnostic));
  QCOMPARE(runtime.listeningPort(), firstPort);
  QCOMPARE(started.count(), 1);

  runtime.stop();
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(stopped.count(), 1);
  QTcpServer releasedPortProbe;
  QVERIFY(releasedPortProbe.listen(QHostAddress::LocalHost, firstPort));
  releasedPortProbe.close();

  QVERIFY2(runtime.start(&diagnostic), qPrintable(diagnostic));
  QVERIFY(runtime.isRunning());
  QCOMPARE(started.count(), 2);
  QCOMPARE(errors.count(), 0);
}

void FileTransferRuntimeTests::invalidIdentityFailsWithoutPublishingAListener()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto localId = DeviceId::generate();
  TrustedDeviceStore trust(directory.filePath(QStringLiteral("trusted-devices.json")));
  DiscoveryRegistry registry(localId);
  FileTransferRuntimeOptions options;
  options.listenAddress = QHostAddress::LocalHost;
  FileTransferRuntime runtime(
      localId, trust, registry, directory.filePath(QStringLiteral("missing.pem")), options
  );
  QSignalSpy errors(&runtime, &FileTransferRuntime::errorOccurred);

  QString diagnostic;
  QVERIFY(!runtime.start(&diagnostic));
  QVERIFY(!diagnostic.isEmpty());
  QVERIFY(!runtime.isRunning());
  QCOMPARE(runtime.listeningPort(), quint16{0});
  QCOMPARE(errors.count(), 1);
  QCOMPARE(
      qvariant_cast<FileTransferRuntimeError>(errors.first().at(0)),
      FileTransferRuntimeError::ListenerFailed
  );
}

QTEST_MAIN(FileTransferRuntimeTests)

#include "FileTransferRuntimeTests.moc"
