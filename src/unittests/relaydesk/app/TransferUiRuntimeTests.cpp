/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferUiRuntime.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

const auto kBaseUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);

NegotiatedCapabilities capabilities()
{
  return {
      .protocolMajorVersion = kProtocolMajorVersion,
      .features = {QStringLiteral("file.v1"), QStringLiteral("folder.v1")},
      .chunkBytes = 1024,
      .maxPayloadBytes = 4096,
      .maxConcurrentTransfers = 2,
      .maxConcurrentFiles = 2,
      .maxManifestEntries = 1000,
      .conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask},
  };
}

IncomingOffer incomingOffer()
{
  return {
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .offer =
          {
              .transferId = QUuid::createUuid(),
              .displayName = QStringLiteral("Project"),
              .totalBytes = 2048,
              .fileCount = 2,
              .directoryCount = 1,
              .manifestSha256 = QByteArray(kSha256Bytes, '\x2a'),
              .manifestPageCount = 1,
              .requestedConflictPolicy = ConflictPolicy::AutoRename,
          },
      .peerTrusted = true,
  };
}

TransferSnapshot transferSnapshot(const QString &id, TransferState state)
{
  const auto terminal = TransferControlStateMachine::isTerminal(state);
  return {
      .id = QUuid(id),
      .peerId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .displayName = QStringLiteral("Project"),
      .state = state,
      .progress =
          {
              .completedBytes = terminal ? 100U : 25U,
              .totalBytes = 100,
              .completedFiles = terminal ? 1U : 0U,
              .totalFiles = 1,
              .bytesPerSecond = terminal ? 0.0 : 25.0,
          },
      .errorMessageKey = state == TransferState::Failed ? QStringLiteral("relaydesk.transfer.io_error") : QString(),
      .errorCode = state == TransferState::Failed ? 4008 : 0,
      .canPause = state == TransferState::Transferring,
      .canResume = state == TransferState::Paused,
      .canCancel = !terminal,
      .canRetry = state == TransferState::Failed,
      .createdUtc = kBaseUtc,
      .finishedUtc = terminal ? kBaseUtc.addSecs(10) : QDateTime{},
  };
}

TransferHistoryRecord historyRecord(
    const QString &id, HistoryDirection direction = HistoryDirection::Receiving,
    HistoryStatus status = HistoryStatus::Completed
)
{
  return {
      .transferId = QUuid(id),
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("History peer"),
      .displayName = QStringLiteral("Archive"),
      .direction = direction,
      .fileCount = 1,
      .totalBytes = 4096,
      .startedUtc = kBaseUtc,
      .finishedUtc = kBaseUtc.addSecs(10),
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? 4008 : 0,
      .errorMessageKey = status == HistoryStatus::Failed ? QStringLiteral("relaydesk.transfer.io_error") : QString(),
  };
}

struct Fixture
{
  PairingStateMachine pairingState{{}, {}, []() { return 42U; }};
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingState};
  PermissionStatusModel permissions{PermissionPlatform::Other};
  DevicesDock devicesDock{devices, pairing, permissions};
  TransferCenterModel transfers;
  TransferCenterDock transferDock{transfers};
  TransferOfferStateMachine offerState{capabilities()};
  IncomingOfferModel incoming{
      offerState,
      {
          .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
          .availableBytes = 1'000'000,
          .decisionTimeoutMs = 5000,
      },
  };
};

void writeFile(const QString &path)
{
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
  QCOMPARE(file.write("relaydesk"), 9);
  file.close();
}

} // namespace

class TransferUiRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void forwardsTypedUiIntentsWithoutOwningTransferWork();
  void opensResolvedCompletionOnlyInsideExistingReceiveRoot();
  void rejectsUntrustedCompletionPathsBeforeCallingOpener();
  void rejectsUnavailableCompositionDependencies();
};

void TransferUiRuntimeTests::forwardsTypedUiIntentsWithoutOwningTransferWork()
{
  Fixture fixture;
  TransferUiRuntime runtime(fixture.devicesDock, fixture.transferDock, fixture.incoming);

  std::optional<DeviceSnapshot> sentPeer;
  QList<QUrl> sentItems;
  std::optional<SendOptions> sentOptions;
  connect(
      &runtime, &TransferUiRuntime::sendItemsRequested, this,
      [&](DeviceSnapshot peer, QList<QUrl> items, SendOptions options) {
        sentPeer = std::move(peer);
        sentItems = std::move(items);
        sentOptions = options;
      }
  );
  const DeviceSnapshot peer{
      .id = DeviceId::generate(),
      .displayName = QStringLiteral("Studio Mac"),
      .presence = DevicePresence::Online,
      .trusted = true,
      .capabilities = {.fileV1 = true},
  };
  const QList<QUrl> items{QUrl::fromLocalFile(QStringLiteral("C:/source/file.txt"))};
  const SendOptions options{.conflictPolicy = ConflictPolicy::AutoRename};
  Q_EMIT fixture.devicesDock.sendItemsRequested(peer, items, options);
  QVERIFY(sentPeer.has_value());
  QCOMPARE(*sentPeer, peer);
  QCOMPARE(sentItems, items);
  QVERIFY(sentOptions.has_value());
  QCOMPARE(*sentOptions, options);

  std::optional<TransferAccept> accepted;
  std::optional<TransferReject> rejected;
  connect(&runtime, &TransferUiRuntime::incomingOfferAccepted, this, [&](TransferAccept response) {
    accepted = std::move(response);
  });
  connect(&runtime, &TransferUiRuntime::incomingOfferRejected, this, [&](TransferReject response) {
    rejected = std::move(response);
  });
  const auto acceptedOffer = incomingOffer();
  QVERIFY(fixture.incoming.receiveOffer(acceptedOffer));
  QVERIFY(fixture.incoming.accept());
  QVERIFY(accepted.has_value());
  QCOMPARE(accepted->transferId, acceptedOffer.offer.transferId);
  const auto rejectedOffer = incomingOffer();
  QVERIFY(fixture.incoming.receiveOffer(rejectedOffer));
  QVERIFY(fixture.incoming.reject());
  QVERIFY(rejected.has_value());
  QCOMPARE(rejected->transferId, rejectedOffer.offer.transferId);

  std::optional<TransferSnapshot> paused;
  std::optional<TransferSnapshot> resumed;
  std::optional<TransferSnapshot> cancelled;
  std::optional<TransferSnapshot> retried;
  std::optional<TransferHistoryRecord> historyRetried;
  connect(&runtime, &TransferUiRuntime::pauseRequested, this, [&](TransferSnapshot snapshot) {
    paused = std::move(snapshot);
  });
  connect(&runtime, &TransferUiRuntime::resumeRequested, this, [&](TransferSnapshot snapshot) {
    resumed = std::move(snapshot);
  });
  connect(&runtime, &TransferUiRuntime::cancelRequested, this, [&](TransferSnapshot snapshot) {
    cancelled = std::move(snapshot);
  });
  connect(&runtime, &TransferUiRuntime::retryRequested, this, [&](TransferSnapshot snapshot) {
    retried = std::move(snapshot);
  });
  connect(&runtime, &TransferUiRuntime::historyRetryRequested, this, [&](TransferHistoryRecord record) {
    historyRetried = std::move(record);
  });

  const auto active =
      transferSnapshot(QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferState::Transferring);
  const auto pausedTransfer =
      transferSnapshot(QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferState::Paused);
  const auto failed = transferSnapshot(QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferState::Failed);
  const auto failedHistory = historyRecord(
      QStringLiteral("44444444-4444-4444-8444-444444444444"), HistoryDirection::Receiving, HistoryStatus::Failed
  );
  QVERIFY(fixture.transfers.upsertTransfer(active));
  QVERIFY(fixture.transfers.upsertTransfer(pausedTransfer));
  QVERIFY(fixture.transfers.upsertTransfer(failed));
  fixture.transfers.setHistoryRecords({failedHistory});
  QVERIFY(fixture.transfers.requestPause(active.id));
  QVERIFY(fixture.transfers.requestCancel(active.id));
  QVERIFY(fixture.transfers.requestResume(pausedTransfer.id));
  QVERIFY(fixture.transfers.requestRetry(failed.id));
  QVERIFY(fixture.transfers.requestRetry(failedHistory.transferId));
  QCOMPARE(paused, active);
  QCOMPARE(cancelled, active);
  QCOMPARE(resumed, pausedTransfer);
  QCOMPARE(retried, failed);
  QCOMPARE(historyRetried, failedHistory);
}

void TransferUiRuntimeTests::opensResolvedCompletionOnlyInsideExistingReceiveRoot()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto receiveRoot = temporary.filePath(QStringLiteral("receive"));
  const auto folder = QDir(receiveRoot).filePath(QStringLiteral("Project"));
  QVERIFY(QDir().mkpath(folder));
  const auto completedPath = QDir(folder).filePath(QStringLiteral("report.txt"));
  writeFile(completedPath);

  const auto record = historyRecord(QStringLiteral("11111111-1111-4111-8111-111111111111"));
  int resolverCalls = 0;
  std::optional<TransferHistoryRecord> resolvedRecord;
  QList<QUrl> openedUrls;
  TransferUiRuntime runtime(
      fixture.devicesDock, fixture.transferDock, fixture.incoming,
      [&](const TransferHistoryRecord &candidate) -> std::optional<ResolvedTransferCompletion> {
        ++resolverCalls;
        resolvedRecord = candidate;
        return ResolvedTransferCompletion{receiveRoot, completedPath};
      },
      [&](const QUrl &url) {
        openedUrls.append(url);
        return true;
      }
  );
  QList<TransferUiRuntime::OpenTarget> openedTargets;
  connect(
      &runtime, &TransferUiRuntime::completionOpened, this,
      [&](TransferId id, TransferUiRuntime::OpenTarget target, const QUrl &) {
        QCOMPARE(id, record.transferId);
        openedTargets.append(target);
      }
  );

  fixture.transfers.setHistoryRecords({record});
  QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
  QVERIFY(fixture.transfers.requestOpenFolder(record.transferId));
  QCOMPARE(resolverCalls, 2);
  QCOMPARE(resolvedRecord, std::optional<TransferHistoryRecord>{record});
  QCOMPARE(openedUrls.size(), 2);
  QCOMPARE(openedUrls.at(0).toLocalFile(), QFileInfo(completedPath).canonicalFilePath());
  QCOMPARE(openedUrls.at(1).toLocalFile(), QDir(folder).canonicalPath());
  QCOMPARE(
      openedTargets,
      QList<TransferUiRuntime::OpenTarget>({TransferUiRuntime::OpenTarget::File, TransferUiRuntime::OpenTarget::Folder})
  );
  QCOMPARE(fixture.transfers.historyRecord(record.transferId), std::optional<TransferHistoryRecord>{record});
}

void TransferUiRuntimeTests::rejectsUntrustedCompletionPathsBeforeCallingOpener()
{
  Fixture fixture;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto receiveRoot = temporary.filePath(QStringLiteral("receive"));
  const auto outsideRoot = temporary.filePath(QStringLiteral("outside"));
  QVERIFY(QDir().mkpath(receiveRoot));
  QVERIFY(QDir().mkpath(outsideRoot));
  const auto validPath = QDir(receiveRoot).filePath(QStringLiteral("valid.txt"));
  const auto outsidePath = QDir(outsideRoot).filePath(QStringLiteral("outside.txt"));
  writeFile(validPath);
  writeFile(outsidePath);

  const auto sending = historyRecord(QStringLiteral("11111111-1111-4111-8111-111111111111"), HistoryDirection::Sending);
  const auto outside = historyRecord(QStringLiteral("22222222-2222-4222-8222-222222222222"));
  const auto missing = historyRecord(QStringLiteral("33333333-3333-4333-8333-333333333333"));
  const auto invalidRoot = historyRecord(QStringLiteral("44444444-4444-4444-8444-444444444444"));
  const auto unresolved = historyRecord(QStringLiteral("55555555-5555-4555-8555-555555555555"));
  const auto openerFailure = historyRecord(QStringLiteral("66666666-6666-4666-8666-666666666666"));
  const auto directoryAsFile = historyRecord(QStringLiteral("77777777-7777-4777-8777-777777777777"));

  int openerCalls = 0;
  TransferUiRuntime runtime(
      fixture.devicesDock, fixture.transferDock, fixture.incoming,
      [&](const TransferHistoryRecord &record) -> std::optional<ResolvedTransferCompletion> {
        if (record.transferId == outside.transferId)
          return ResolvedTransferCompletion{receiveRoot, outsidePath};
        if (record.transferId == missing.transferId)
          return ResolvedTransferCompletion{receiveRoot, QDir(receiveRoot).filePath(QStringLiteral("missing.txt"))};
        if (record.transferId == invalidRoot.transferId)
          return ResolvedTransferCompletion{QStringLiteral("relative-root"), validPath};
        if (record.transferId == unresolved.transferId)
          return std::nullopt;
        if (record.transferId == directoryAsFile.transferId)
          return ResolvedTransferCompletion{receiveRoot, receiveRoot};
        return ResolvedTransferCompletion{receiveRoot, validPath};
      },
      [&](const QUrl &) {
        ++openerCalls;
        return false;
      }
  );

  QList<TransferUiRuntime::OpenError> errors;
  connect(
      &runtime, &TransferUiRuntime::completionOpenRejected, this,
      [&](TransferId, TransferUiRuntime::OpenTarget, TransferUiRuntime::OpenError error) { errors.append(error); }
  );
  fixture.transfers.setHistoryRecords(
      {sending, outside, missing, invalidRoot, unresolved, openerFailure, directoryAsFile}
  );
  QVERIFY(fixture.transfers.requestOpenFile(sending.transferId));
  QVERIFY(fixture.transfers.requestOpenFile(outside.transferId));
  QVERIFY(fixture.transfers.requestOpenFile(missing.transferId));
  QVERIFY(fixture.transfers.requestOpenFile(invalidRoot.transferId));
  QVERIFY(fixture.transfers.requestOpenFile(unresolved.transferId));
  QVERIFY(fixture.transfers.requestOpenFile(directoryAsFile.transferId));
  QCOMPARE(openerCalls, 0);
  QVERIFY(fixture.transfers.requestOpenFile(openerFailure.transferId));
  QCOMPARE(openerCalls, 1);
  QCOMPARE(
      errors, QList<TransferUiRuntime::OpenError>({
                  TransferUiRuntime::OpenError::NotCompletedReceive,
                  TransferUiRuntime::OpenError::CompletedPathOutsideReceiveRoot,
                  TransferUiRuntime::OpenError::CompletedPathMissing,
                  TransferUiRuntime::OpenError::InvalidReceiveRoot,
                  TransferUiRuntime::OpenError::ResolutionUnavailable,
                  TransferUiRuntime::OpenError::CompletedPathTypeMismatch,
                  TransferUiRuntime::OpenError::OpenFailed,
              })
  );
}

void TransferUiRuntimeTests::rejectsUnavailableCompositionDependencies()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto receiveRoot = temporary.filePath(QStringLiteral("receive"));
  QVERIFY(QDir().mkpath(receiveRoot));
  const auto completedPath = QDir(receiveRoot).filePath(QStringLiteral("completed.txt"));
  writeFile(completedPath);
  const auto record = historyRecord(QStringLiteral("11111111-1111-4111-8111-111111111111"));

  {
    Fixture fixture;
    TransferUiRuntime runtime(fixture.devicesDock, fixture.transferDock, fixture.incoming);
    std::optional<TransferUiRuntime::OpenError> error;
    connect(
        &runtime, &TransferUiRuntime::completionOpenRejected, this,
        [&](TransferId, TransferUiRuntime::OpenTarget, TransferUiRuntime::OpenError candidate) { error = candidate; }
    );
    fixture.transfers.setHistoryRecords({record});
    QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
    QCOMPARE(error, std::optional<TransferUiRuntime::OpenError>{TransferUiRuntime::OpenError::ResolverUnavailable});
  }

  {
    Fixture fixture;
    TransferUiRuntime runtime(
        fixture.devicesDock, fixture.transferDock, fixture.incoming,
        [&](const TransferHistoryRecord &) -> std::optional<ResolvedTransferCompletion> {
          return ResolvedTransferCompletion{receiveRoot, completedPath};
        }
    );
    std::optional<TransferUiRuntime::OpenError> error;
    connect(
        &runtime, &TransferUiRuntime::completionOpenRejected, this,
        [&](TransferId, TransferUiRuntime::OpenTarget, TransferUiRuntime::OpenError candidate) { error = candidate; }
    );
    fixture.transfers.setHistoryRecords({record});
    QVERIFY(fixture.transfers.requestOpenFile(record.transferId));
    QCOMPARE(error, std::optional<TransferUiRuntime::OpenError>{TransferUiRuntime::OpenError::OpenerUnavailable});
  }
}

QTEST_MAIN(TransferUiRuntimeTests)

#include "TransferUiRuntimeTests.moc"
