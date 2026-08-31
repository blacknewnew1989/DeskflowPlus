/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferUiRuntime.h"

#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/transfer/TransferControlStateMachine.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace deskflow::relaydesk {
namespace {

bool isUnderRoot(const QString &canonicalRoot, const QString &canonicalPath)
{
  const auto relative = QDir(canonicalRoot).relativeFilePath(canonicalPath);
  return relative == QStringLiteral(".") ||
         (!QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") &&
          !relative.startsWith(QStringLiteral("../")) && !relative.startsWith(QStringLiteral("..\\")));
}

} // namespace

TransferUiRuntime::TransferUiRuntime(
    IFileTransferService &service, widgets::DevicesDock &devicesDock,
    widgets::TransferCenterDock &transferCenterDock,
    model::IncomingOfferModel &incomingOffers, CompletionResolver completionResolver, UrlOpener urlOpener,
    QObject *parent
)
    : QObject(parent),
      m_completionResolver(std::move(completionResolver)),
      m_urlOpener(std::move(urlOpener))
{
  devicesDock.setIncomingOfferModel(&incomingOffers);

  connect(
      &devicesDock, &widgets::DevicesDock::sendItemsRequested, this,
      [&service, &devicesDock](const DeviceId &peerDeviceId, const QList<QUrl> &localItems,
                              const ::relaydesk::transfer::SendOptions &options) {
        const auto result = service.send(peerDeviceId, localItems, options);
        if (!result.ok()) devicesDock.showSendFailure(result.error);
      }
  );
  connect(
      &incomingOffers, &model::IncomingOfferModel::acceptRequested, &service,
      [&service](const ::relaydesk::transfer::TransferId &transferId,
                 const ::relaydesk::transfer::ReceiveOptions &options) {
        service.accept(transferId, options);
      }
  );
  connect(
      &incomingOffers, &model::IncomingOfferModel::rejectRequested, &service,
      [&service](const ::relaydesk::transfer::TransferId &transferId,
                 ::relaydesk::transfer::RejectReason reason) { service.reject(transferId, reason); }
  );
  connect(
      &service, &IFileTransferService::incomingConflictDecisionRequired, &devicesDock,
      [&devicesDock](const ::relaydesk::transfer::IncomingConflictPrompt &prompt) {
        devicesDock.showIncomingConflictPrompt(prompt);
      }
  );
  connect(
      &devicesDock, &widgets::DevicesDock::incomingConflictDecisionRequested, &service,
      [&service](const ::relaydesk::transfer::TransferId &transferId, const QUuid &conflictId,
                 ::relaydesk::transfer::IncomingConflictDecision decision) {
        service.resolveIncomingConflict(transferId, conflictId, decision);
      }
  );
  connect(
      &service, &IFileTransferService::transferOperationFinished, &devicesDock,
      [&devicesDock](const ::relaydesk::transfer::TransferOperationResult &result) {
        if (result.operation != ::relaydesk::transfer::TransferOperation::Cancel)
          return;
        if (result.outcome == ::relaydesk::transfer::TransferOperationOutcome::Rejected &&
            result.error == ::relaydesk::transfer::TransferOperationError::TransportFailed) {
          devicesDock.showIncomingConflictCancelTransportFailure(result.transferId);
        } else if (result.ok()) {
          devicesDock.clearIncomingConflictPrompts(result.transferId);
        }
      }
  );

  auto &transfers = transferCenterDock.transferModel();
  connect(&transfers, &model::TransferCenterModel::pauseRequested, &service, &IFileTransferService::pause);
  connect(&transfers, &model::TransferCenterModel::resumeRequested, &service, &IFileTransferService::resume);
  connect(&transfers, &model::TransferCenterModel::cancelRequested, &service, &IFileTransferService::cancel);
  connect(&transfers, &model::TransferCenterModel::retryRequested, &service, &IFileTransferService::retry);
  connect(&transfers, &model::TransferCenterModel::historyRetryRequested, &service, &IFileTransferService::retry);
  connect(
      &service, &IFileTransferService::transferOperationFinished, &transfers,
      [&transfers](const ::relaydesk::transfer::TransferOperationResult &result) {
        using namespace ::relaydesk::transfer;
        if (result.operation == TransferOperation::Retry && result.outcome == TransferOperationOutcome::Rejected) {
          transfers.setHistoryRetryAvailable(result.transferId, true);
        }
      }
  );
  connect(
      &service, &IFileTransferService::incomingOffer, &incomingOffers,
      [&incomingOffers](const ::relaydesk::transfer::IncomingOffer &offer) {
        (void)incomingOffers.receiveOffer(offer);
      }
  );
  connect(
      &service, &IFileTransferService::transferAdded, &transfers,
      [&transfers](const ::relaydesk::transfer::TransferSnapshot &snapshot) {
        (void)transfers.upsertTransfer(snapshot);
      }
  );
  connect(
      &service, &IFileTransferService::transferChanged, &transfers,
      [&transfers](const ::relaydesk::transfer::TransferSnapshot &snapshot) {
        (void)transfers.upsertTransfer(snapshot);
      }
  );
  connect(
      &service, &IFileTransferService::transferChanged, &devicesDock,
      [&devicesDock](const ::relaydesk::transfer::TransferSnapshot &snapshot) {
        if (::relaydesk::transfer::TransferControlStateMachine::isTerminal(snapshot.state))
          devicesDock.clearIncomingConflictPrompts(snapshot.id);
      }
  );
  connect(
      &service, &IFileTransferService::transferRemoved, &transfers,
      [&transfers](const ::relaydesk::transfer::TransferId &transferId) {
        (void)transfers.removeTransfer(transferId);
      }
  );
  connect(
      &service, &IFileTransferService::transferRemoved, &devicesDock,
      [&devicesDock](const ::relaydesk::transfer::TransferId &transferId) {
        devicesDock.clearIncomingConflictPrompts(transferId);
      }
  );
  transfers.setTransfers(service.activeTransfers());
  connect(
      &transfers, &model::TransferCenterModel::openFolderRequested, this,
      [this](const ::relaydesk::transfer::TransferHistoryRecord &record) { openCompletion(record, OpenTarget::Folder); }
  );
  connect(
      &transfers, &model::TransferCenterModel::openFileRequested, this,
      [this](const ::relaydesk::transfer::TransferHistoryRecord &record) { openCompletion(record, OpenTarget::File); }
  );
}

void TransferUiRuntime::openCompletion(const ::relaydesk::transfer::TransferHistoryRecord &record, OpenTarget target)
{
  using ::relaydesk::transfer::HistoryDirection;
  using ::relaydesk::transfer::HistoryStatus;

  if (record.status != HistoryStatus::Completed || record.direction != HistoryDirection::Receiving) {
    Q_EMIT completionOpenRejected(record.transferId, target, OpenError::NotCompletedReceive);
    return;
  }
  if (!m_completionResolver) {
    Q_EMIT completionOpenRejected(record.transferId, target, OpenError::ResolverUnavailable);
    return;
  }

  const auto completion = m_completionResolver(record);
  if (!completion.has_value()) {
    Q_EMIT completionOpenRejected(record.transferId, target, OpenError::ResolutionUnavailable);
    return;
  }

  OpenError error = OpenError::InvalidCompletedPath;
  const auto url = validatedCompletionUrl(record, *completion, target, error);
  if (!url.has_value()) {
    Q_EMIT completionOpenRejected(record.transferId, target, error);
    return;
  }
  if (!m_urlOpener) {
    Q_EMIT completionOpenRejected(record.transferId, target, OpenError::OpenerUnavailable);
    return;
  }
  if (!m_urlOpener(*url)) {
    Q_EMIT completionOpenRejected(record.transferId, target, OpenError::OpenFailed);
    return;
  }
  Q_EMIT completionOpened(record.transferId, target, *url);
}

std::optional<QUrl> TransferUiRuntime::validatedCompletionUrl(
    const ::relaydesk::transfer::TransferHistoryRecord &record, const ResolvedTransferCompletion &completion,
    OpenTarget target, OpenError &error
) const
{
  const QFileInfo rootInfo(completion.receiveRoot);
  if (!rootInfo.isAbsolute() || !rootInfo.exists() || !rootInfo.isDir()) {
    error = OpenError::InvalidReceiveRoot;
    return std::nullopt;
  }
  const auto canonicalRoot = rootInfo.canonicalFilePath();
  if (canonicalRoot.isEmpty()) {
    error = OpenError::InvalidReceiveRoot;
    return std::nullopt;
  }

  const QFileInfo completedInfo(completion.completedPath);
  if (!completedInfo.isAbsolute()) {
    error = OpenError::InvalidCompletedPath;
    return std::nullopt;
  }
  if (!completedInfo.exists()) {
    error = OpenError::CompletedPathMissing;
    return std::nullopt;
  }
  const auto canonicalCompleted = completedInfo.canonicalFilePath();
  if (canonicalCompleted.isEmpty()) {
    error = OpenError::CompletedPathMissing;
    return std::nullopt;
  }
  if (!isUnderRoot(canonicalRoot, canonicalCompleted)) {
    error = OpenError::CompletedPathOutsideReceiveRoot;
    return std::nullopt;
  }

  QString pathToOpen;
  if (target == OpenTarget::File) {
    if (record.fileCount != 1 || !completedInfo.isFile()) {
      error = OpenError::CompletedPathTypeMismatch;
      return std::nullopt;
    }
    pathToOpen = canonicalCompleted;
  } else if (completedInfo.isDir()) {
    pathToOpen = canonicalCompleted;
  } else if (completedInfo.isFile()) {
    pathToOpen = QFileInfo(canonicalCompleted).dir().canonicalPath();
  } else {
    error = OpenError::CompletedPathTypeMismatch;
    return std::nullopt;
  }

  if (pathToOpen.isEmpty() || !isUnderRoot(canonicalRoot, pathToOpen)) {
    error = OpenError::CompletedPathOutsideReceiveRoot;
    return std::nullopt;
  }
  return QUrl::fromLocalFile(pathToOpen);
}

} // namespace deskflow::relaydesk
