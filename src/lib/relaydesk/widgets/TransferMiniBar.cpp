/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferMiniBar.h"

#include "relaydesk/i18n/ProductStrings.h"
#include "relaydesk/model/TransferCenterModel.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;

int presentedRow(const model::TransferCenterModel &transfers)
{
  for (int row = 0; row < transfers.rowCount(); ++row) {
    if (!transfers.index(row, 0).data(model::TransferCenterModel::IsTerminalRole).toBool())
      return row;
  }
  return transfers.rowCount() > 0 ? 0 : -1;
}

} // namespace

TransferMiniBar::TransferMiniBar(model::TransferCenterModel &transfers, QWidget *parent)
    : QFrame(parent),
      m_transfers(transfers)
{
  setObjectName(QStringLiteral("relaydeskTransferMiniBar"));
  setFrameShape(QFrame::StyledPanel);
  setFixedHeight(52);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 4, 8, 4);
  layout->setSpacing(8);

  auto *summary = new QVBoxLayout();
  summary->setContentsMargins(0, 0, 0, 0);
  summary->setSpacing(1);

  m_title = new QLabel(this);
  m_title->setObjectName(QStringLiteral("relaydeskTransferMiniBarTitle"));
  m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
  QFont titleFont = m_title->font();
  titleFont.setWeight(QFont::DemiBold);
  m_title->setFont(titleFont);
  summary->addWidget(m_title);

  m_metrics = new QLabel(this);
  m_metrics->setObjectName(QStringLiteral("relaydeskTransferMiniBarMetrics"));
  m_metrics->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_metrics->setAttribute(Qt::WA_TransparentForMouseEvents);
  summary->addWidget(m_metrics);

  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("relaydeskTransferMiniBarProgress"));
  m_progress->setRange(0, 100);
  m_progress->setTextVisible(false);
  m_progress->setFixedHeight(3);
  m_progress->setAttribute(Qt::WA_TransparentForMouseEvents);
  summary->addWidget(m_progress);
  layout->addLayout(summary, 1);

  m_primaryAction = new QPushButton(this);
  m_primaryAction->setObjectName(QStringLiteral("relaydeskTransferMiniBarPrimaryAction"));
  layout->addWidget(m_primaryAction);

  connect(&m_transfers, &QAbstractItemModel::rowsInserted, this, &TransferMiniBar::refresh);
  connect(&m_transfers, &QAbstractItemModel::rowsRemoved, this, &TransferMiniBar::refresh);
  connect(&m_transfers, &QAbstractItemModel::rowsMoved, this, &TransferMiniBar::refresh);
  connect(&m_transfers, &QAbstractItemModel::modelReset, this, &TransferMiniBar::refresh);
  connect(&m_transfers, &QAbstractItemModel::layoutChanged, this, &TransferMiniBar::refresh);
  connect(&m_transfers, &QAbstractItemModel::dataChanged, this, &TransferMiniBar::refresh);
  connect(m_primaryAction, &QPushButton::clicked, this, &TransferMiniBar::triggerPrimaryAction);

  refresh();
}

void TransferMiniBar::changeEvent(QEvent *event)
{
  QFrame::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    refresh();
}

void TransferMiniBar::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
    event->accept();
    Q_EMIT detailsRequested();
    return;
  }
  QFrame::keyPressEvent(event);
}

void TransferMiniBar::mouseReleaseEvent(QMouseEvent *event)
{
  QFrame::mouseReleaseEvent(event);
  if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
    Q_EMIT detailsRequested();
}

void TransferMiniBar::refresh()
{
  const auto row = presentedRow(m_transfers);
  if (row < 0) {
    m_transferId.clear();
    m_action = PrimaryAction::None;
    m_primaryAction->hide();
    hide();
    return;
  }

  const auto index = m_transfers.index(row, 0);
  m_transferId = index.data(model::TransferCenterModel::TransferIdRole).toString();
  m_title->setText(index.data(model::TransferCenterModel::DisplayNameRole).toString());

  QStringList metrics{index.data(model::TransferCenterModel::ProgressTextRole).toString()};
  const auto speed = index.data(model::TransferCenterModel::SpeedTextRole).toString();
  if (!speed.isEmpty())
    metrics.append(speed);
  else
    metrics.append(index.data(model::TransferCenterModel::StateTextRole).toString());
  metrics.removeAll(QString{});
  m_metrics->setText(metrics.join(QStringLiteral(" · ")));
  m_progress->setValue(qBound(0, index.data(model::TransferCenterModel::ProgressPercentRole).toInt(), 100));

  const auto accessibleSummary = index.data(model::TransferCenterModel::AccessibleSummaryRole).toString();
  setAccessibleName(accessibleSummary);
  setToolTip(accessibleSummary);

  if (index.data(model::TransferCenterModel::CanRetryRole).toBool())
    m_action = PrimaryAction::Retry;
  else if (index.data(model::TransferCenterModel::CanResumeRole).toBool())
    m_action = PrimaryAction::Resume;
  else if (index.data(model::TransferCenterModel::CanPauseRole).toBool())
    m_action = PrimaryAction::Pause;
  else
    m_action = PrimaryAction::None;

  switch (m_action) {
  case PrimaryAction::Pause:
    m_primaryAction->setText(i18n::translate(Text::TransferActionPause));
    break;
  case PrimaryAction::Resume:
    m_primaryAction->setText(i18n::translate(Text::TransferActionResume));
    break;
  case PrimaryAction::Retry:
    m_primaryAction->setText(i18n::translate(Text::TransferActionRetry));
    break;
  case PrimaryAction::None:
    m_primaryAction->setText({});
    break;
  }
  m_primaryAction->setAccessibleName(m_primaryAction->text());
  m_primaryAction->setVisible(m_action != PrimaryAction::None);
  show();
}

void TransferMiniBar::triggerPrimaryAction()
{
  const auto transferId = ::relaydesk::transfer::TransferId::fromString(m_transferId);
  if (!transferId.has_value())
    return;

  switch (m_action) {
  case PrimaryAction::Pause:
    (void)m_transfers.requestPause(*transferId);
    break;
  case PrimaryAction::Resume:
    (void)m_transfers.requestResume(*transferId);
    break;
  case PrimaryAction::Retry:
    (void)m_transfers.requestRetry(*transferId);
    break;
  case PrimaryAction::None:
    break;
  }
}

} // namespace deskflow::relaydesk::widgets
