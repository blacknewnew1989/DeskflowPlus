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
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include "../FakePairingService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::test;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

const auto kBaseUtc = QDateTime::fromMSecsSinceEpoch(1'780'000'000'000LL, Qt::UTC);

IncomingOffer incomingOffer()
{
  return {
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("Studio Mac"),
      .offer =
          {
              .transferId = TransferId::generate(),
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
      .id = *TransferId::fromString(id),
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
      .errorCode = state == TransferState::Failed ? TransferErrorCode::SenderFailed : TransferErrorCode::None,
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
      .transferId = *TransferId::fromString(id),
      .peerDeviceId = DeviceId::generate(),
      .peerDisplayName = QStringLiteral("History peer"),
      .displayName = QStringLiteral("Archive"),
      .direction = direction,
      .fileCount = 1,
      .totalBytes = 4096,
      .startedUtc = kBaseUtc,
      .finishedUtc = kBaseUtc.addSecs(10),
      .status = status,
      .errorCode = status == HistoryStatus::Failed ? TransferErrorCode::SenderFailed : TransferErrorCode::None,
  };
}

class FakeFileTransferService final : public IFileTransferService
{
public:
  [[nodiscard]] TransferStartResult send(
      const DeviceId &target, const QList<QUrl> &localItems, const SendOptions &options
  ) override
  {
    ++sendCalls;
    sentTarget = target;
    sentItems = localItems;
    sentOptions = options;
    return {.transferId = TransferId::generate()};
  }

  void accept(const TransferId &transferId, const ReceiveOptions &options) override
  {
    ++acceptCalls;
    acceptedId = transferId;
    acceptedOptions = options;
  }

  void reject(const TransferId &transferId, RejectReason reason) override
  {
    ++rejectCalls;
    rejectedId = transferId;
    rejectedReason = reason;
  }

  void pause(const TransferId &transferId) override
  {
    ++pauseCalls;
    pausedId = transferId;
  }

  void resume(const TransferId &transferId) override
  {
    ++resumeCalls;
    resumedId = transferId;
  }

  void cancel(const TransferId &transferId, const TransferCancelOptions &options) override
  {
    ++cancelCalls;
    cancelledId = transferId;
    cancelOptions = options;
  }

  void retry(const TransferId &transferId) override
  {
    ++retryCalls;
    retriedIds.append(transferId);
  }

  void resolveIncomingConflict(const TransferId &transferId, const QUuid &conflictId, IncomingConflictDecision decision) override
  {
    ++resolveConflictCalls;
    resolvedConflictTransferId = transferId;
    resolvedConflictId = conflictId;
    resolvedConflictDecision = decision;
  }

  [[nodiscard]] QList<TransferSnapshot> activeTransfers() const override
  {
    ++activeTransferCalls;
    return initialTransfers;
  }

  int sendCalls = 0;
  int acceptCalls = 0;
  int rejectCalls = 0;
  int pauseCalls = 0;
  int resumeCalls = 0;
  int cancelCalls = 0;
  int retryCalls = 0;
  int resolveConflictCalls = 0;
  mutable int activeTransferCalls = 0;
  std::optional<DeviceId> sentTarget;
  QList<QUrl> sentItems;
  std::optional<SendOptions> sentOptions;
  std::optional<TransferId> acceptedId;
  std::optional<ReceiveOptions> acceptedOptions;
  std::optional<TransferId> rejectedId;
  std::optional<RejectReason> rejectedReason;
  std::optional<TransferId> pausedId;
  std::optional<TransferId> resumedId;
  std::optional<TransferId> cancelledId;
  std::optional<TransferCancelOptions> cancelOptions;
  QList<TransferId> retriedIds;
  QList<TransferSnapshot> initialTransfers;
  std::optional<TransferId> resolvedConflictTransferId;
  std::optional<QUuid> resolvedConflictId;
  std::optional<IncomingConflictDecision> resolvedConflictDecision;
};

struct Fixture
{
  FakeFileTransferService service;
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingService};
  PermissionStatusModel permissions{PermissionPlatform::Other};
  DevicesDock devicesDock{devices, pairing, permissions};
  TransferCenterModel transfers;
  TransferCenterDock transferDock{transfers};
  IncomingOfferModel incoming{
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
  void composesTypedUiIntentsThroughOneServiceBoundary();
  void opensResolvedCompletionOnlyInsideExistingReceiveRoot();
  void rejectsUntrustedCompletionPathsBeforeCallingOpener();
  void rejectsUnavailableCompositionDependencies();
  void bridgesIncomingConflictDecisionsThroughTypedUiIntent();
};

void TransferUiRuntimeTests::composesTypedUiIntentsThroughOneServiceBoundary()
{
  Fixture fixture;
  TransferUiRuntime runtime(fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming);
  QCOMPARE(fixture.service.activeTransferCalls, 1);

  const auto peerId = DeviceId::generate();
  const QList<QUrl> items{QUrl::fromLocalFile(QStringLiteral("C:/source/file.txt"))};
  const SendOptions options{.conflictPolicy = ConflictPolicy::AutoRename};
  Q_EMIT fixture.devicesDock.sendItemsRequested(peerId, items, options);
  QCOMPARE(fixture.service.sendCalls, 1);
  QCOMPARE(fixture.service.sentTarget, std::optional<DeviceId>{peerId});
  QCOMPARE(fixture.service.sentItems, items);
  QCOMPARE(fixture.service.sentOptions, std::optional<SendOptions>{options});

  QSignalSpy offerChanges(&fixture.incoming, &IncomingOfferModel::changed);
  const auto acceptedOffer = incomingOffer();
  Q_EMIT fixture.service.incomingOffer(acceptedOffer);
  QCOMPARE(offerChanges.count(), 1);
  QCOMPARE(fixture.incoming.offer(), std::optional<IncomingOffer>{acceptedOffer});
  QVERIFY(fixture.incoming.accept());
  QCOMPARE(fixture.service.acceptCalls, 1);
  QCOMPARE(fixture.service.acceptedId, std::optional<TransferId>{acceptedOffer.offer.transferId});
  QVERIFY(fixture.service.acceptedOptions.has_value());
  QCOMPARE(fixture.service.acceptedOptions->destinationRoot, QStringLiteral("Downloads/RelayDesk"));
  const auto rejectedOffer = incomingOffer();
  Q_EMIT fixture.service.incomingOffer(rejectedOffer);
  QVERIFY(fixture.incoming.reject());
  QCOMPARE(fixture.service.rejectCalls, 1);
  QCOMPARE(fixture.service.rejectedId, std::optional<TransferId>{rejectedOffer.offer.transferId});
  QCOMPARE(fixture.service.rejectedReason, std::optional<RejectReason>{RejectReason::UserDeclined});

  const auto active =
      transferSnapshot(QStringLiteral("11111111-1111-4111-8111-111111111111"), TransferState::Transferring);
  const auto pausedTransfer =
      transferSnapshot(QStringLiteral("22222222-2222-4222-8222-222222222222"), TransferState::Paused);
  const auto failed = transferSnapshot(QStringLiteral("33333333-3333-4333-8333-333333333333"), TransferState::Failed);
  const auto failedHistory = historyRecord(
      QStringLiteral("44444444-4444-4444-8444-444444444444"), HistoryDirection::Receiving, HistoryStatus::Failed
  );
  QSignalSpy inserted(&fixture.transfers, &QAbstractItemModel::rowsInserted);
  Q_EMIT fixture.service.transferAdded(active);
  Q_EMIT fixture.service.transferAdded(pausedTransfer);
  Q_EMIT fixture.service.transferAdded(failed);
  QCOMPARE(inserted.count(), 3);
  fixture.transfers.setHistoryRecords({failedHistory});
  QVERIFY(fixture.transfers.requestPause(active.id));
  const TransferCancelOptions cancelOptions{
      .reason = TransferCancelReason::UserRequested,
      .partialDisposition = PartialDisposition::Remove,
  };
  QVERIFY(fixture.transfers.requestCancel(active.id, cancelOptions));
  QVERIFY(fixture.transfers.requestResume(pausedTransfer.id));
  QVERIFY(fixture.transfers.requestRetry(failed.id));
  QVERIFY(fixture.transfers.requestRetry(failedHistory.transferId));
  QCOMPARE(fixture.service.pauseCalls, 1);
  QCOMPARE(fixture.service.pausedId, std::optional<TransferId>{active.id});
  QCOMPARE(fixture.service.cancelCalls, 1);
  QCOMPARE(fixture.service.cancelledId, std::optional<TransferId>{active.id});
  QCOMPARE(fixture.service.cancelOptions, std::optional<TransferCancelOptions>{cancelOptions});
  QCOMPARE(fixture.service.resumeCalls, 1);
  QCOMPARE(fixture.service.resumedId, std::optional<TransferId>{pausedTransfer.id});
  QCOMPARE(fixture.service.retryCalls, 2);
  QCOMPARE(fixture.service.retriedIds, QList<TransferId>({failed.id, failedHistory.transferId}));

  auto changed = active;
  changed.displayName = QStringLiteral("Updated project");
  QSignalSpy dataChanged(&fixture.transfers, &QAbstractItemModel::dataChanged);
  Q_EMIT fixture.service.transferChanged(changed);
  QCOMPARE(dataChanged.count(), 1);
  QCOMPARE(fixture.transfers.snapshot(active.id), std::optional<TransferSnapshot>{changed});
  QSignalSpy removed(&fixture.transfers, &QAbstractItemModel::rowsRemoved);
  Q_EMIT fixture.service.transferRemoved(active.id);
  QCOMPARE(removed.count(), 1);
  QVERIFY(!fixture.transfers.snapshot(active.id).has_value());
}

void TransferUiRuntimeTests::bridgesIncomingConflictDecisionsThroughTypedUiIntent()
{
  Fixture fixture;
  TransferUiRuntime runtime(fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming);
  const IncomingConflictPrompt prompt{
      .transferId = TransferId::generate(),
      .conflictId = QUuid::createUuid(),
      .relativeProtocolPath = QStringLiteral("Project/conflict.txt"),
  };
  fixture.devicesDock.show();
  Q_EMIT fixture.service.incomingConflictDecisionRequired(prompt);

  auto *overwrite = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictOverwriteButton"));
  QVERIFY(overwrite != nullptr);
  QTest::mouseClick(overwrite, Qt::LeftButton);
  QCOMPARE(fixture.service.resolveConflictCalls, 1);
  QCOMPARE(fixture.service.resolvedConflictTransferId, std::optional<TransferId>{prompt.transferId});
  QCOMPARE(fixture.service.resolvedConflictId, std::optional<QUuid>{prompt.conflictId});
  QCOMPARE(
      fixture.service.resolvedConflictDecision,
      std::optional<IncomingConflictDecision>{IncomingConflictDecision::Overwrite}
  );

  Q_EMIT fixture.service.incomingConflictDecisionRequired(prompt);
  auto *cancel = fixture.devicesDock.findChild<QPushButton *>(QStringLiteral("relaydeskIncomingConflictCancelButton"));
  QVERIFY(cancel != nullptr);
  QTest::mouseClick(cancel, Qt::LeftButton);
  QTest::mouseClick(cancel, Qt::LeftButton);
  QCOMPARE(fixture.service.resolveConflictCalls, 3);
  QCOMPARE(fixture.service.resolvedConflictTransferId, std::optional<TransferId>{prompt.transferId});
  QCOMPARE(fixture.service.resolvedConflictId, std::optional<QUuid>{prompt.conflictId});
  QCOMPARE(
      fixture.service.resolvedConflictDecision,
      std::optional<IncomingConflictDecision>{IncomingConflictDecision::CancelTransfer}
  );
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
      fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming,
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
      fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming,
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
    TransferUiRuntime runtime(fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming);
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
        fixture.service, fixture.devicesDock, fixture.transferDock, fixture.incoming,
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
