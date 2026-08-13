/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/app/TransferUiRuntime.h"
#include "relaydesk/model/IncomingOfferModel.h"

#include <QObject>

#include <functional>
#include <memory>

namespace deskflow::relaydesk::widgets {
class DevicesDock;
class TransferCenterDock;
} // namespace deskflow::relaydesk::widgets

namespace deskflow::relaydesk {

class IFileTransferService;
class TransferHistoryRuntime;

struct TransferRuntimeLifecycle
{
  std::function<bool(QString *diagnostic)> start;
  std::function<void()> stop;
};

// Product composition root for the one frozen file-service boundary. It owns
// the service before constructing the UI adapter and stops it before either is
// destroyed.
class TransferRuntimeComposition final : public QObject
{
  Q_OBJECT

public:
  TransferRuntimeComposition(
      std::unique_ptr<IFileTransferService> service, TransferRuntimeLifecycle lifecycle,
      widgets::DevicesDock &devicesDock, widgets::TransferCenterDock &transferCenterDock,
      model::IncomingOfferSettingsSnapshot incomingOfferSettings,
      QString historyPath = {},
      TransferUiRuntime::CompletionResolver completionResolver = {},
      TransferUiRuntime::UrlOpener urlOpener = {}, QObject *parent = nullptr
  );
  ~TransferRuntimeComposition() override;

  Q_DISABLE_COPY_MOVE(TransferRuntimeComposition)

  [[nodiscard]] bool start(QString *diagnostic = nullptr);
  void stop();
  [[nodiscard]] bool isRunning() const noexcept;
  [[nodiscard]] IFileTransferService &service() const noexcept;
  [[nodiscard]] model::IncomingOfferModel &incomingOffers() noexcept;

private:
  std::unique_ptr<IFileTransferService> m_service;
  TransferRuntimeLifecycle m_lifecycle;
  model::IncomingOfferModel m_incomingOffers;
  TransferUiRuntime m_uiRuntime;
  std::unique_ptr<TransferHistoryRuntime> m_historyRuntime;
  bool m_running = false;
};

} // namespace deskflow::relaydesk
