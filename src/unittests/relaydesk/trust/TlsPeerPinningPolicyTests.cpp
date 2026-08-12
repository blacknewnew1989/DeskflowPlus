/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TlsPeerPinningPolicy.h"

#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace deskflow::relaydesk;

namespace {

TrustedDevice trustedDevice(const DeviceId &deviceId, QByteArray fingerprint)
{
  return {
      .deviceId = deviceId,
      .alias = QStringLiteral("Peer"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = std::move(fingerprint),
  };
}

} // namespace

class TlsPeerPinningPolicyTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void exactFingerprintPasses();
  void unknownChangedAndRevokedPeersFailClosed();
};

void TlsPeerPinningPolicyTests::exactFingerprintPasses()
{
  QTemporaryDir directory;
  const DeviceId peerId = DeviceId::generate();
  const QByteArray fingerprint(32, '\x2a');
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.upsert(trustedDevice(peerId, fingerprint)));

  const PeerPinningResult result = TlsPeerPinningPolicy::verify(store, peerId, fingerprint);

  QVERIFY2(result.ok(), qPrintable(result.diagnostic));
  QCOMPARE(result.error, PeerPinningError::None);
}

void TlsPeerPinningPolicyTests::unknownChangedAndRevokedPeersFailClosed()
{
  QTemporaryDir directory;
  const DeviceId peerId = DeviceId::generate();
  const QByteArray fingerprint(32, '\x2a');
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  QVERIFY(store.upsert(trustedDevice(peerId, fingerprint)));

  QCOMPARE(TlsPeerPinningPolicy::verify(store, DeviceId::generate(), fingerprint).error, PeerPinningError::UnknownPeer);
  QCOMPARE(
      TlsPeerPinningPolicy::verify(store, peerId, QByteArray(32, '\x2b')).error, PeerPinningError::FingerprintChanged
  );
  QCOMPARE(
      TlsPeerPinningPolicy::verify(store, peerId, QByteArray(31, '\x2a')).error, PeerPinningError::FingerprintChanged
  );
  QVERIFY(store.revoke(peerId));
  QCOMPARE(TlsPeerPinningPolicy::verify(store, peerId, fingerprint).error, PeerPinningError::RevokedPeer);
}

QTEST_MAIN(TlsPeerPinningPolicyTests)

#include "TlsPeerPinningPolicyTests.moc"
