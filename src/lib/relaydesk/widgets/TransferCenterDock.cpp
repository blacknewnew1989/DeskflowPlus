/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferCenterDock.h"

#include "relaydesk/i18n/ProductStrings.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/widgets/TransferHistoryDetailsDialog.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QVBoxLayout>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;

class TransferRowDelegate final : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
  {
    QStyleOptionViewItem panel(option);
    initStyleOption(&panel, index);
    panel.text.clear();
    const auto *style = panel.widget != nullptr ? panel.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &panel, painter, panel.widget);

    painter->save();
    const auto foreground = option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
    const auto muted = option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::PlaceholderText;
    const QRect content = option.rect.adjusted(12, 9, -12, -9);
    const int lineHeight = option.fontMetrics.height();

    QFont titleFont(option.font);
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(option.palette.color(foreground));
    painter->drawText(
        QRect(content.left(), content.top(), content.width() * 2 / 3, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(titleFont).elidedText(index.data(model::TransferCenterModel::DisplayNameRole).toString(), Qt::ElideRight,
                                           content.width() * 2 / 3)
    );
    painter->setFont(option.font);
    painter->drawText(
        QRect(content.left() + content.width() * 2 / 3, content.top(), content.width() / 3, lineHeight),
        Qt::AlignRight | Qt::AlignVCenter, index.data(model::TransferCenterModel::StateTextRole).toString()
    );

    painter->setPen(option.palette.color(muted));
    const auto peer = index.data(model::TransferCenterModel::DirectionTextRole).toString() + QStringLiteral(" · ")
                      + index.data(model::TransferCenterModel::PeerDisplayNameRole).toString();
    painter->drawText(
        QRect(content.left(), content.top() + lineHeight + 3, content.width(), lineHeight),
        Qt::AlignLeft | Qt::AlignVCenter, option.fontMetrics.elidedText(peer, Qt::ElideRight, content.width())
    );

    QStyleOptionProgressBar progress;
    progress.rect = QRect(content.left(), content.top() + (lineHeight + 3) * 2, content.width(), 18);
    progress.minimum = 0;
    progress.maximum = 100;
    progress.progress = index.data(model::TransferCenterModel::ProgressPercentRole).toInt();
    progress.text = QStringLiteral("%1%").arg(progress.progress);
    progress.textVisible = true;
    progress.state = option.state;
    progress.palette = option.palette;
    style->drawControl(QStyle::CE_ProgressBar, &progress, painter, panel.widget);

    QStringList metrics{index.data(model::TransferCenterModel::ProgressTextRole).toString()};
    const auto speed = index.data(model::TransferCenterModel::SpeedTextRole).toString();
    const auto eta = index.data(model::TransferCenterModel::EtaTextRole).toString();
    if (!speed.isEmpty())
      metrics.append(speed);
    if (!eta.isEmpty())
      metrics.append(eta);
    const auto detail = metrics.join(QStringLiteral(" · "));
    const auto path = index.data(model::TransferCenterModel::CurrentPathRole).toString();
    painter->setPen(option.palette.color(muted));
    painter->drawText(
        QRect(content.left(), progress.rect.bottom() + 5, content.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(detail, Qt::ElideRight, content.width())
    );
    if (!path.isEmpty()) {
      painter->drawText(
          QRect(content.left(), progress.rect.bottom() + 5 + lineHeight + 2, content.width(), lineHeight),
          Qt::AlignLeft | Qt::AlignVCenter, option.fontMetrics.elidedText(path, Qt::ElideMiddle, content.width())
      );
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
  {
    return {380, option.fontMetrics.height() * 4 + 60};
  }
};

} // namespace

TransferCenterDock::TransferCenterDock(model::TransferCenterModel &transfers, QWidget *parent)
    : QDockWidget(parent), m_transfers(transfers)
{
  setObjectName(QStringLiteral("relaydeskTransferCenterDock"));
  setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  setMinimumWidth(360);

  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(6);
  m_emptyLabel = new QLabel(body);
  m_emptyLabel->setObjectName(QStringLiteral("relaydeskTransfersEmptyLabel"));
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setMargin(18);
  layout->addWidget(m_emptyLabel, 1);
  m_list = new QListView(body);
  m_list->setObjectName(QStringLiteral("relaydeskTransfersView"));
  m_list->setModel(&m_transfers);
  m_list->setItemDelegate(new TransferRowDelegate(m_list));
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setUniformItemSizes(true);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  layout->addWidget(m_list, 1);
  auto *historyActions = new QHBoxLayout();
  m_detailsButton = new QPushButton(body);
  m_detailsButton->setObjectName(QStringLiteral("relaydeskTransferDetailsButton"));
  m_openFolderButton = new QPushButton(body);
  m_openFolderButton->setObjectName(QStringLiteral("relaydeskTransferOpenFolderButton"));
  m_openFileButton = new QPushButton(body);
  m_openFileButton->setObjectName(QStringLiteral("relaydeskTransferOpenFileButton"));
  m_retryButton = new QPushButton(body);
  m_retryButton->setObjectName(QStringLiteral("relaydeskTransferRetryButton"));
  historyActions->addWidget(m_detailsButton);
  historyActions->addWidget(m_openFolderButton);
  historyActions->addWidget(m_openFileButton);
  historyActions->addStretch();
  historyActions->addWidget(m_retryButton);
  layout->addLayout(historyActions);
  auto *actions = new QHBoxLayout();
  m_pauseButton = new QPushButton(body);
  m_pauseButton->setObjectName(QStringLiteral("relaydeskTransferPauseButton"));
  m_resumeButton = new QPushButton(body);
  m_resumeButton->setObjectName(QStringLiteral("relaydeskTransferResumeButton"));
  m_cancelButton = new QPushButton(body);
  m_cancelButton->setObjectName(QStringLiteral("relaydeskTransferCancelButton"));
  actions->addWidget(m_pauseButton);
  actions->addWidget(m_resumeButton);
  actions->addStretch();
  actions->addWidget(m_cancelButton);
  layout->addLayout(actions);
  setWidget(body);

  connect(&m_transfers, &QAbstractItemModel::rowsInserted, this, &TransferCenterDock::updateEmptyState);
  connect(&m_transfers, &QAbstractItemModel::rowsRemoved, this, &TransferCenterDock::updateEmptyState);
  connect(&m_transfers, &QAbstractItemModel::modelReset, this, &TransferCenterDock::updateEmptyState);
  connect(&m_transfers, &QAbstractItemModel::dataChanged, this, &TransferCenterDock::updateSelection);
  connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, &TransferCenterDock::updateSelection);
  connect(m_list, &QListView::activated, this, [this](const QModelIndex &index) {
    if (index.data(model::TransferCenterModel::HasHistoryDetailsRole).toBool())
      showHistoryDetails();
  });
  connect(m_detailsButton, &QPushButton::clicked, this, &TransferCenterDock::showHistoryDetails);
  connect(m_openFolderButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestOpenFolder(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  connect(m_openFileButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestOpenFile(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  connect(m_retryButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestRetry(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestPause(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  connect(m_resumeButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestResume(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
    const auto index = m_list->currentIndex();
    if (index.isValid())
      (void)m_transfers.requestCancel(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  });
  updateText();
  updateEmptyState();
  updateSelection();
}

model::TransferCenterModel &TransferCenterDock::transferModel() const
{
  return m_transfers;
}

void TransferCenterDock::changeEvent(QEvent *event)
{
  QDockWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    updateText();
    m_list->viewport()->update();
  }
}

void TransferCenterDock::updateText()
{
  setWindowTitle(i18n::translate(Text::TransferTitle));
  m_emptyLabel->setText(i18n::translate(Text::TransferEmpty));
  m_list->setAccessibleName(i18n::translate(Text::TransferTitle));
  m_detailsButton->setText(i18n::translate(Text::TransferActionDetails));
  m_detailsButton->setAccessibleName(m_detailsButton->text());
  m_openFolderButton->setText(i18n::translate(Text::TransferActionOpenFolder));
  m_openFolderButton->setAccessibleName(m_openFolderButton->text());
  m_openFileButton->setText(i18n::translate(Text::TransferActionOpenFile));
  m_openFileButton->setAccessibleName(m_openFileButton->text());
  m_retryButton->setText(i18n::translate(Text::TransferActionRetry));
  m_retryButton->setAccessibleName(m_retryButton->text());
  m_pauseButton->setText(i18n::translate(Text::TransferActionPause));
  m_pauseButton->setAccessibleName(i18n::translate(Text::TransferActionPause));
  m_resumeButton->setText(i18n::translate(Text::TransferActionResume));
  m_resumeButton->setAccessibleName(i18n::translate(Text::TransferActionResume));
  m_cancelButton->setText(i18n::translate(Text::TransferActionCancel));
  m_cancelButton->setAccessibleName(i18n::translate(Text::TransferActionCancel));
}

void TransferCenterDock::updateEmptyState()
{
  const auto empty = m_transfers.rowCount() == 0;
  m_emptyLabel->setVisible(empty);
  m_list->setVisible(!empty);
  if (empty)
    m_list->setCurrentIndex({});
  updateSelection();
}

void TransferCenterDock::updateSelection()
{
  const auto index = m_list->currentIndex();
  const auto hasTransfers = m_transfers.rowCount() > 0;
  const auto historical = index.isValid() && index.data(model::TransferCenterModel::IsHistoricalRole).toBool();
  const auto hasDetails = index.isValid() && index.data(model::TransferCenterModel::HasHistoryDetailsRole).toBool();
  const auto canOpenFolder = index.isValid() && index.data(model::TransferCenterModel::CanOpenFolderRole).toBool();
  const auto canOpenFile = index.isValid() && index.data(model::TransferCenterModel::CanOpenFileRole).toBool();
  const auto canRetry = index.isValid() && index.data(model::TransferCenterModel::CanRetryRole).toBool();

  m_detailsButton->setVisible(hasDetails);
  m_detailsButton->setEnabled(hasDetails);
  m_openFolderButton->setVisible(canOpenFolder);
  m_openFolderButton->setEnabled(canOpenFolder);
  m_openFileButton->setVisible(canOpenFile);
  m_openFileButton->setEnabled(canOpenFile);
  m_retryButton->setVisible(canRetry);
  m_retryButton->setEnabled(canRetry);
  m_pauseButton->setVisible(hasTransfers && !historical);
  m_resumeButton->setVisible(hasTransfers && !historical);
  m_cancelButton->setVisible(hasTransfers && !historical);
  m_pauseButton->setEnabled(index.isValid() && index.data(model::TransferCenterModel::CanPauseRole).toBool());
  m_resumeButton->setEnabled(index.isValid() && index.data(model::TransferCenterModel::CanResumeRole).toBool());
  m_cancelButton->setEnabled(index.isValid() && index.data(model::TransferCenterModel::CanCancelRole).toBool());
}

void TransferCenterDock::showHistoryDetails()
{
  const auto index = m_list->currentIndex();
  if (!index.isValid() || !index.data(model::TransferCenterModel::HasHistoryDetailsRole).toBool())
    return;
  const auto record = m_transfers.historyRecord(QUuid(index.data(model::TransferCenterModel::TransferIdRole).toString()));
  if (!record.has_value())
    return;
  auto *dialog = new TransferHistoryDetailsDialog(*record, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

} // namespace deskflow::relaydesk::widgets
