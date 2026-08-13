/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferRuntimeComposition.h"

#include "relaydesk/app/TransferHistoryRuntime.h"

#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include <utility>

namespace deskflow::relaydesk {

TransferRuntimeComposition::TransferRuntimeComposition(
    std::unique_ptr<IFileTransferService> service, TransferRuntimeLifecycle lifecycle,
    widgets::DevicesDock &devicesDock, widgets::TransferCenterDock &transferCenterDock,
    model::IncomingOfferSettingsSnapshot incomingOfferSettings,
    QString historyPath,
    TransferUiRuntime::CompletionResolver completionResolver, TransferUiRuntime::UrlOpener urlOpener,
    QObject *parent
)
    : QObject(parent), m_service(std::move(service)), m_lifecycle(std::move(lifecycle)),
      m_incomingOffers(std::move(incomingOfferSettings), this),
      m_uiRuntime(
          *m_service, devicesDock, transferCenterDock, m_incomingOffers, std::move(completionResolver),
          std::move(urlOpener), this
      )
{
  Q_ASSERT(m_service != nullptr);
  if (!historyPath.isEmpty()) {
    m_historyRuntime = std::make_unique<TransferHistoryRuntime>(
        *m_service, transferCenterDock.transferModel(), m_incomingOffers,
        m_incomingOffers.settings().destinationRoot, std::move(historyPath), this
    );
  }
}

TransferRuntimeComposition::~TransferRuntimeComposition()
{
  stop();
}

bool TransferRuntimeComposition::start(QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (m_running) {
    return true;
  }
  if (!m_lifecycle.start) {
    if (diagnostic != nullptr) {
      *diagnostic = QStringLiteral("file transfer runtime start callback is unavailable");
    }
    return false;
  }
  m_running = m_lifecycle.start(diagnostic);
  if (m_running && m_historyRuntime != nullptr) {
    m_historyRuntime->start();
  }
  return m_running;
}

void TransferRuntimeComposition::stop()
{
  if (!m_running) {
    return;
  }
  m_running = false;
  if (m_lifecycle.stop) {
    m_lifecycle.stop();
  }
}

bool TransferRuntimeComposition::isRunning() const noexcept
{
  return m_running;
}

IFileTransferService &TransferRuntimeComposition::service() const noexcept
{
  return *m_service;
}

model::IncomingOfferModel &TransferRuntimeComposition::incomingOffers() noexcept
{
  return m_incomingOffers;
}

} // namespace deskflow::relaydesk
