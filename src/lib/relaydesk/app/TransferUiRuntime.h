/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferHistoryStore.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace deskflow::relaydesk::model {
class IncomingOfferModel;
}

namespace deskflow::relaydesk::widgets {
class DevicesDock;
class TransferCenterDock;
} // namespace deskflow::relaydesk::widgets

namespace deskflow::relaydesk {

class IFileTransferService;

struct ResolvedTransferCompletion
{
  QString receiveRoot;
  QString completedPath;

  [[nodiscard]] bool operator==(const ResolvedTransferCompletion &) const = default;
};

// Bridges typed UI intents without pretending to be the file-transfer service.
// The A6-owned runtime remains responsible for network, disk and history state.
class TransferUiRuntime final : public QObject
{
  Q_OBJECT

public:
  enum class OpenTarget
  {
    Folder,
    File,
  };
  Q_ENUM(OpenTarget)

  enum class OpenError
  {
    NotCompletedReceive,
    ResolverUnavailable,
    ResolutionUnavailable,
    InvalidReceiveRoot,
    InvalidCompletedPath,
    CompletedPathMissing,
    CompletedPathOutsideReceiveRoot,
    CompletedPathTypeMismatch,
    OpenerUnavailable,
    OpenFailed,
  };
  Q_ENUM(OpenError)

  using CompletionResolver =
      std::function<std::optional<ResolvedTransferCompletion>(const ::relaydesk::transfer::TransferHistoryRecord &record
      )>;
  using UrlOpener = std::function<bool(const QUrl &url)>;

  explicit TransferUiRuntime(
      IFileTransferService &service, widgets::DevicesDock &devicesDock,
      widgets::TransferCenterDock &transferCenterDock,
      model::IncomingOfferModel &incomingOffers, CompletionResolver completionResolver = {}, UrlOpener urlOpener = {},
      QObject *parent = nullptr
  );

  Q_DISABLE_COPY_MOVE(TransferUiRuntime)

Q_SIGNALS:
  void completionOpened(::relaydesk::transfer::TransferId transferId, OpenTarget target, QUrl url);
  void completionOpenRejected(::relaydesk::transfer::TransferId transferId, OpenTarget target, OpenError error);

private:
  void openCompletion(const ::relaydesk::transfer::TransferHistoryRecord &record, OpenTarget target);
  [[nodiscard]] std::optional<QUrl> validatedCompletionUrl(
      const ::relaydesk::transfer::TransferHistoryRecord &record, const ResolvedTransferCompletion &completion,
      OpenTarget target, OpenError &error
  ) const;

  CompletionResolver m_completionResolver;
  UrlOpener m_urlOpener;
};

} // namespace deskflow::relaydesk
