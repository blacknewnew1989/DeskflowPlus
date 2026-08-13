/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingManager.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <utility>

using namespace deskflow::relaydesk;

namespace {

struct Packet
{
  QByteArray bytes;
  PairingEndpoint target;
};

DeviceInfo deviceInfo(const DeviceId &id, QString name, char fingerprintByte)
{
  return {
      .deviceId = id,
      .displayName = std::move(name),
      .platform = QStringLiteral("windows"),
      .architecture = QStringLiteral("x86_64"),
      .appVersion = QStringLiteral("0.1.0"),
      .inputPort = 24800,
      .filePort = 24801,
      .capabilities = {.input = true, .fileV1 = true},
      .certificateFingerprintSha256 = QByteArray(32, fingerprintByte),
  };
}

DeviceSnapshot snapshotFor(const DeviceInfo &device)
{
  return {
      .id = device.deviceId,
      .displayName = device.displayName,
      .platform = device.platform,
      .architecture = device.architecture,
      .presence = DevicePresence::Discovered,
      .capabilities = device.capabilities,
  };
}

struct Fixture
{
  QTemporaryDir directory;
  QDateTime now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  DeviceId firstId = DeviceId::generate();
  DeviceId secondId = DeviceId::generate();
  DeviceInfo firstInfo = deviceInfo(firstId, QStringLiteral("First"), '\x11');
  DeviceInfo secondInfo = deviceInfo(secondId, QStringLiteral("Second"), '\x22');
  PairingEndpoint firstEndpoint{QHostAddress(QStringLiteral("127.0.0.1")), 31001};
  PairingEndpoint secondEndpoint{QHostAddress(QStringLiteral("127.0.0.1")), 31002};
  TrustedDeviceStore firstStore{directory.filePath(QStringLiteral("first/trusted.json"))};
  TrustedDeviceStore secondStore{directory.filePath(QStringLiteral("second/trusted.json"))};
  QList<Packet> firstPackets;
  QList<Packet> secondPackets;
  PairingManager first{
      firstInfo,
      firstStore,
      [this](QByteArray bytes, PairingEndpoint target) {
        firstPackets.append({std::move(bytes), std::move(target)});
        return PairingTransportResult{.ok = true};
      },
      {},
      [this]() { return now; },
      []() { return 123456U; },
  };
  PairingManager second{
      secondInfo,
      secondStore,
      [this](QByteArray bytes, PairingEndpoint target) {
        secondPackets.append({std::move(bytes), std::move(target)});
        return PairingTransportResult{.ok = true};
      },
      {},
      [this]() { return now; },
      []() { return 654321U; },
  };

  PairingOperationResult startAndDeliverRequest()
  {
    auto started = first.startPairing(snapshotFor(secondInfo), secondInfo.certificateFingerprintSha256, secondEndpoint);
    if (!started.ok() || firstPackets.isEmpty()) {
      return started;
    }
    const auto request = firstPackets.takeFirst();
    if (request.target != secondEndpoint) {
      return {.error = PairingOperationError::InvalidEndpoint, .diagnostic = QStringLiteral("wrong target")};
    }
    return second.receiveDatagram(request.bytes, firstEndpoint);
  }
};

} // namespace

class PairingManagerTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void completesOnlyAfterBothUsersConfirm();
  void wrongCodeRejectsWithoutTrust();
  void expiresAtFiveMinutes();
  void rejectsForgedEndpointSessionAndDevice();
  void rejectsDuplicatesAndOutOfOrderMessages();
  void persistenceFailureRejectsBothSides();
  void cancelAndRevokeAreDurable();
};

void PairingManagerTests::completesOnlyAfterBothUsersConfirm()
{
  Fixture fixture;
  QVERIFY2(fixture.startAndDeliverRequest().ok(), "request should be accepted");
  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  const auto sas = fixture.first.snapshot()->sixDigitSas;
  QCOMPARE(sas, QStringLiteral("123456"));
  QCOMPARE(fixture.second.snapshot()->pairingSessionId, sessionId);
  QVERIFY(fixture.second.snapshot()->sixDigitSas.isEmpty());

  QVERIFY(fixture.first.confirmMatchingSas(sessionId).ok());
  QVERIFY(fixture.firstStore.devices().isEmpty());
  QVERIFY(fixture.second.submitDisplayedSas(sessionId, sas).ok());
  QVERIFY(fixture.secondStore.devices().isEmpty());

  const auto submission = fixture.secondPackets.takeFirst();
  QVERIFY(fixture.first.receiveDatagram(submission.bytes, fixture.secondEndpoint).ok());
  QCOMPARE(fixture.first.snapshot()->state, PairingState::Completed);
  QVERIFY(fixture.firstStore.find(fixture.secondId).has_value());
  QVERIFY(fixture.secondStore.devices().isEmpty());

  const auto accepted = fixture.firstPackets.takeFirst();
  QVERIFY(fixture.second.receiveDatagram(accepted.bytes, fixture.firstEndpoint).ok());
  QCOMPARE(fixture.second.snapshot()->state, PairingState::Completed);
  QVERIFY(fixture.secondStore.find(fixture.firstId).has_value());
}

void PairingManagerTests::wrongCodeRejectsWithoutTrust()
{
  Fixture fixture;
  QVERIFY(fixture.startAndDeliverRequest().ok());
  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  QVERIFY(fixture.first.confirmMatchingSas(sessionId).ok());
  QVERIFY(fixture.second.submitDisplayedSas(sessionId, QStringLiteral("999999")).ok());

  const auto submission = fixture.secondPackets.takeFirst();
  const auto compared = fixture.first.receiveDatagram(submission.bytes, fixture.secondEndpoint);

  QCOMPARE(compared.error, PairingOperationError::InvalidCode);
  QCOMPARE(fixture.first.snapshot()->state, PairingState::Rejected);
  QCOMPARE(fixture.first.snapshot()->failureReason, PairingFailureReason::CodeMismatch);
  QVERIFY(fixture.firstStore.devices().isEmpty());
  const auto rejected = fixture.firstPackets.takeFirst();
  QCOMPARE(
      fixture.second.receiveDatagram(rejected.bytes, fixture.firstEndpoint).error,
      PairingOperationError::InvalidCode
  );
  QCOMPARE(fixture.second.snapshot()->state, PairingState::Failed);
  QCOMPARE(fixture.second.snapshot()->failureReason, PairingFailureReason::CodeMismatch);
  QVERIFY(fixture.secondStore.devices().isEmpty());
}

void PairingManagerTests::expiresAtFiveMinutes()
{
  Fixture fixture;
  QVERIFY(fixture.startAndDeliverRequest().ok());
  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  QCOMPARE(fixture.first.snapshot()->expiresAtUtc, fixture.now.addSecs(300));
  fixture.now = fixture.now.addSecs(300);

  QVERIFY(fixture.first.expireIfNeeded());
  QVERIFY(fixture.second.expireIfNeeded());
  QCOMPARE(fixture.first.snapshot()->state, PairingState::Expired);
  QCOMPARE(fixture.second.snapshot()->state, PairingState::Expired);
  QCOMPARE(
      fixture.second.submitDisplayedSas(sessionId, QStringLiteral("123456")).error,
      PairingOperationError::Expired
  );
}

void PairingManagerTests::rejectsForgedEndpointSessionAndDevice()
{
  Fixture fixture;
  QVERIFY(fixture.startAndDeliverRequest().ok());
  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  auto submission = PairingCodeSubmission{
      .pairingSessionId = sessionId,
      .sender = fixture.secondInfo,
      .sixDigitSas = QStringLiteral("123456"),
  };
  const auto encoded = PairingMessageCodec::encode(PairingMessage(submission));

  QCOMPARE(
      fixture.first.receiveDatagram(
          encoded, {QHostAddress(QStringLiteral("127.0.0.1")), quint16(fixture.secondEndpoint.port + 1)}
      ).error,
      PairingOperationError::EndpointMismatch
  );
  submission.pairingSessionId = QUuid::createUuid();
  QCOMPARE(
      fixture.first.receiveDatagram(
          PairingMessageCodec::encode(PairingMessage(submission)), fixture.secondEndpoint
      ).error,
      PairingOperationError::SessionMismatch
  );
  submission.pairingSessionId = sessionId;
  submission.sender = deviceInfo(DeviceId::generate(), QStringLiteral("Imposter"), '\x22');
  QCOMPARE(
      fixture.first.receiveDatagram(
          PairingMessageCodec::encode(PairingMessage(submission)), fixture.secondEndpoint
      ).error,
      PairingOperationError::PeerMismatch
  );
  QCOMPARE(fixture.first.snapshot()->state, PairingState::AwaitingUserComparison);
  QVERIFY(fixture.firstStore.devices().isEmpty());
}

void PairingManagerTests::rejectsDuplicatesAndOutOfOrderMessages()
{
  Fixture fixture;
  QVERIFY(fixture.first.startPairing(
      snapshotFor(fixture.secondInfo), fixture.secondInfo.certificateFingerprintSha256, fixture.secondEndpoint
  ).ok());
  const auto requestPacket = fixture.firstPackets.first();
  QVERIFY(fixture.second.receiveDatagram(requestPacket.bytes, fixture.firstEndpoint).ok());
  QCOMPARE(
      fixture.second.receiveDatagram(requestPacket.bytes, fixture.firstEndpoint).error,
      PairingOperationError::DuplicateMessage
  );

  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  const auto prematureResult = PairingMessageCodec::encode(PairingMessage(PairingResultMessage{
      .pairingSessionId = sessionId,
      .accepted = true,
  }));
  QCOMPARE(
      fixture.first.receiveDatagram(prematureResult, fixture.secondEndpoint).error,
      PairingOperationError::UnexpectedMessage
  );

  QTemporaryDir directory;
  TrustedDeviceStore store(directory.filePath(QStringLiteral("trusted.json")));
  PairingManager idle(
      fixture.firstInfo, store,
      [](QByteArray, PairingEndpoint) { return PairingTransportResult{.ok = true}; }
  );
  const auto submission = PairingMessageCodec::encode(PairingMessage(PairingCodeSubmission{
      QUuid::createUuid(), fixture.secondInfo, QStringLiteral("123456")
  }));
  QCOMPARE(
      idle.receiveDatagram(submission, fixture.secondEndpoint).error,
      PairingOperationError::SessionNotFound
  );
}

void PairingManagerTests::persistenceFailureRejectsBothSides()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto blocker = directory.filePath(QStringLiteral("blocker"));
  QFile file(blocker);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  QDateTime now = QDateTime::fromMSecsSinceEpoch(1'730'000'000'000LL, QTimeZone::UTC);
  const auto firstId = DeviceId::generate();
  const auto secondId = DeviceId::generate();
  const auto firstInfo = deviceInfo(firstId, QStringLiteral("First"), '\x11');
  const auto secondInfo = deviceInfo(secondId, QStringLiteral("Second"), '\x22');
  const PairingEndpoint firstEndpoint{QHostAddress(QStringLiteral("127.0.0.1")), 32001};
  const PairingEndpoint secondEndpoint{QHostAddress(QStringLiteral("127.0.0.1")), 32002};
  TrustedDeviceStore failingStore(blocker + QStringLiteral("/trusted.json"));
  TrustedDeviceStore goodStore(directory.filePath(QStringLiteral("good/trusted.json")));
  QList<Packet> firstPackets;
  QList<Packet> secondPackets;
  PairingManager first(
      firstInfo, failingStore,
      [&firstPackets](QByteArray bytes, PairingEndpoint target) {
        firstPackets.append({std::move(bytes), std::move(target)});
        return PairingTransportResult{.ok = true};
      },
      {}, [&now]() { return now; }, []() { return 123456U; }
  );
  PairingManager second(
      secondInfo, goodStore,
      [&secondPackets](QByteArray bytes, PairingEndpoint target) {
        secondPackets.append({std::move(bytes), std::move(target)});
        return PairingTransportResult{.ok = true};
      },
      {}, [&now]() { return now; }
  );

  QVERIFY(first.startPairing(snapshotFor(secondInfo), secondInfo.certificateFingerprintSha256, secondEndpoint).ok());
  QVERIFY(second.receiveDatagram(firstPackets.takeFirst().bytes, firstEndpoint).ok());
  const auto sessionId = first.snapshot()->pairingSessionId;
  QVERIFY(first.confirmMatchingSas(sessionId).ok());
  QVERIFY(second.submitDisplayedSas(sessionId, QStringLiteral("123456")).ok());

  const auto persisted = first.receiveDatagram(secondPackets.takeFirst().bytes, secondEndpoint);

  QCOMPARE(persisted.error, PairingOperationError::PersistenceFailed);
  QCOMPARE(first.snapshot()->state, PairingState::Failed);
  QCOMPARE(first.snapshot()->failureReason, PairingFailureReason::TrustStoreWriteFailed);
  QVERIFY(failingStore.devices().isEmpty());
  QVERIFY(second.receiveDatagram(firstPackets.takeFirst().bytes, firstEndpoint).error != PairingOperationError::None);
  QVERIFY(goodStore.devices().isEmpty());
}

void PairingManagerTests::cancelAndRevokeAreDurable()
{
  Fixture fixture;
  QVERIFY(fixture.startAndDeliverRequest().ok());
  const auto sessionId = fixture.first.snapshot()->pairingSessionId;
  QVERIFY(fixture.second.submitDisplayedSas(sessionId, fixture.first.snapshot()->sixDigitSas).ok());
  QVERIFY(fixture.first.cancel(sessionId).ok());
  QCOMPARE(fixture.first.snapshot()->state, PairingState::Rejected);
  QCOMPARE(fixture.first.snapshot()->failureReason, PairingFailureReason::Cancelled);
  const auto cancelled = fixture.firstPackets.takeFirst();
  QCOMPARE(
      fixture.second.receiveDatagram(cancelled.bytes, fixture.firstEndpoint).error,
      PairingOperationError::InvalidState
  );
  QCOMPARE(fixture.second.snapshot()->state, PairingState::Failed);
  QCOMPARE(fixture.second.snapshot()->failureReason, PairingFailureReason::Cancelled);

  QVERIFY(fixture.firstStore.upsert({
      .deviceId = fixture.secondId,
      .alias = QStringLiteral("Second"),
      .platform = QStringLiteral("windows"),
      .fingerprintSha256 = fixture.secondInfo.certificateFingerprintSha256,
  }));
  QVERIFY(fixture.firstStore.save().ok);
  QVERIFY(fixture.first.revoke(fixture.secondId).ok());
  QVERIFY(fixture.firstStore.find(fixture.secondId)->revoked);

  TrustedDeviceStore reloaded(fixture.firstStore.path());
  QVERIFY(reloaded.load().ok);
  QVERIFY(reloaded.find(fixture.secondId)->revoked);
  QCOMPARE(fixture.first.revoke(DeviceId::generate()).error, PairingOperationError::RevokeFailed);
}

QTEST_MAIN(PairingManagerTests)

#include "PairingManagerTests.moc"
