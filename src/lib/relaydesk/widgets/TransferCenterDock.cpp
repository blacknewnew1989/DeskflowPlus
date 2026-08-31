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
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;

std::optional<::relaydesk::transfer::TransferId> transferIdForIndex(const QModelIndex &index)
{
  if (!index.isValid()) {
    return std::nullopt;
  }
  const auto transferId = index.data(model::TransferCenterModel::TransferIdRole).toString();
  return ::relaydesk::transfer::TransferId::fromString(transferId);
}

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
    const auto muted =
        option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::PlaceholderText;
    const QRect content = option.rect.adjusted(10, 5, -10, -7);
    const int lineHeight = option.fontMetrics.height();

    QFont titleFont(option.font);
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(option.palette.color(foreground));
    const auto stateText = index.data(model::TransferCenterModel::StateTextRole).toString();
    const auto stateWidth = qMin(content.width() / 3, option.fontMetrics.horizontalAdvance(stateText) + 12);
    painter->drawText(
        QRect(content.left(), content.top(), content.width() - stateWidth - 6, lineHeight),
        Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(titleFont).elidedText(
            index.data(model::TransferCenterModel::DisplayNameRole).toString(), Qt::ElideRight,
            content.width() - stateWidth - 6
        )
    );
    painter->setFont(option.font);
    painter->drawText(
        QRect(content.right() - stateWidth + 1, content.top(), stateWidth, lineHeight),
        Qt::AlignRight | Qt::AlignVCenter, stateText
    );

    QStringList metrics{index.data(model::TransferCenterModel::ProgressTextRole).toString()};
    const auto speed = index.data(model::TransferCenterModel::SpeedTextRole).toString();
    const auto eta = index.data(model::TransferCenterModel::EtaTextRole).toString();
    if (!speed.isEmpty())
      metrics.append(speed);
    if (!eta.isEmpty())
      metrics.append(eta);
    const auto detail = metrics.join(QStringLiteral(" · "));
    const auto peer = index.data(model::TransferCenterModel::DirectionTextRole).toString() + QStringLiteral(" · ") +
                      index.data(model::TransferCenterModel::PeerDisplayNameRole).toString();
    const auto peerWidth = qMin(content.width() / 3, option.fontMetrics.horizontalAdvance(peer) + 8);
    const auto detailTop = content.top() + lineHeight + 3;
    painter->setPen(option.palette.color(muted));
    painter->drawText(
        QRect(content.left(), detailTop, content.width() - peerWidth - 6, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(detail, Qt::ElideRight, content.width() - peerWidth - 6)
    );
    painter->drawText(
        QRect(content.right() - peerWidth + 1, detailTop, peerWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter,
        option.fontMetrics.elidedText(peer, Qt::ElideLeft, peerWidth)
    );

    const auto progressPercent = qBound(0, index.data(model::TransferCenterModel::ProgressPercentRole).toInt(), 100);
    const QRect progressTrack(content.left(), option.rect.bottom() - 4, content.width(), 3);
    auto trackColor = option.palette.color(muted);
    trackColor.setAlpha(70);
    painter->fillRect(progressTrack, trackColor);
    const auto progressColor = option.state.testFlag(QStyle::State_Selected)
                                   ? option.palette.color(QPalette::HighlightedText)
                                   : option.palette.color(QPalette::Highlight);
    painter->fillRect(
        QRect(
            progressTrack.left(), progressTrack.top(), progressTrack.width() * progressPercent / 100,
            progressTrack.height()
        ),
        progressColor
    );
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
  {
    return {360, qBound(56, option.fontMetrics.height() * 2 + 26, 64)};
  }
};

} // namespace

TransferCenterDock::TransferCenterDock(model::TransferCenterModel &transfers, QWidget *parent)
    : QDockWidget(parent),
      m_transfers(transfers)
{
  setObjectName(QStringLiteral("relaydeskTransferCenterDock"));
  setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  setMinimumWidth(320);

  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(8, 6, 8, 8);
  layout->setSpacing(4);
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
  m_list->setSpacing(2);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setFrameShape(QFrame::NoFrame);
  layout->addWidget(m_list, 1);
  m_feedback = new QLabel(body);
  m_feedback->setObjectName(QStringLiteral("relaydeskTransferFeedback"));
  m_feedback->setWordWrap(true);
  m_feedback->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  layout->addWidget(m_feedback);

  auto *actions = new QHBoxLayout();
  actions->setSpacing(5);
  actions->addStretch();
  m_detailsButton = new QPushButton(body);
  m_detailsButton->setObjectName(QStringLiteral("relaydeskTransferDetailsButton"));
  m_openFolderButton = new QPushButton(body);
  m_openFolderButton->setObjectName(QStringLiteral("relaydeskTransferOpenFolderButton"));
  m_openFileButton = new QPushButton(body);
  m_openFileButton->setObjectName(QStringLiteral("relaydeskTransferOpenFileButton"));
  m_retryButton = new QPushButton(body);
  m_retryButton->setObjectName(QStringLiteral("relaydeskTransferRetryButton"));
  m_pauseButton = new QPushButton(body);
  m_pauseButton->setObjectName(QStringLiteral("relaydeskTransferPauseButton"));
  m_resumeButton = new QPushButton(body);
  m_resumeButton->setObjectName(QStringLiteral("relaydeskTransferResumeButton"));
  m_cancelButton = new QPushButton(body);
  m_cancelButton->setObjectName(QStringLiteral("relaydeskTransferCancelButton"));
  actions->addWidget(m_detailsButton);
  actions->addWidget(m_openFolderButton);
  actions->addWidget(m_openFileButton);
  actions->addWidget(m_retryButton);
  actions->addWidget(m_pauseButton);
  actions->addWidget(m_resumeButton);
  actions->addWidget(m_cancelButton);

  m_moreButton = new QToolButton(body);
  m_moreButton->setObjectName(QStringLiteral("relaydeskTransferMoreButton"));
  m_moreButton->setText(QStringLiteral("\u2026"));
  m_moreButton->setPopupMode(QToolButton::InstantPopup);
  m_moreMenu = new QMenu(m_moreButton);
  m_moreMenu->setObjectName(QStringLiteral("relaydeskTransferMoreMenu"));
  m_moreButton->setMenu(m_moreMenu);
  m_detailsMenuAction = m_moreMenu->addAction(QString{});
  m_detailsMenuAction->setObjectName(QStringLiteral("relaydeskTransferDetailsMenuAction"));
  m_openFolderMenuAction = m_moreMenu->addAction(QString{});
  m_openFolderMenuAction->setObjectName(QStringLiteral("relaydeskTransferOpenFolderMenuAction"));
  m_openFileMenuAction = m_moreMenu->addAction(QString{});
  m_openFileMenuAction->setObjectName(QStringLiteral("relaydeskTransferOpenFileMenuAction"));
  m_retryMenuAction = m_moreMenu->addAction(QString{});
  m_retryMenuAction->setObjectName(QStringLiteral("relaydeskTransferRetryMenuAction"));
  m_pauseMenuAction = m_moreMenu->addAction(QString{});
  m_pauseMenuAction->setObjectName(QStringLiteral("relaydeskTransferPauseMenuAction"));
  m_resumeMenuAction = m_moreMenu->addAction(QString{});
  m_resumeMenuAction->setObjectName(QStringLiteral("relaydeskTransferResumeMenuAction"));
  m_cancelMenuAction = m_moreMenu->addAction(QString{});
  m_cancelMenuAction->setObjectName(QStringLiteral("relaydeskTransferCancelMenuAction"));
  actions->addWidget(m_moreButton);
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
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestOpenFolder(*id);
  });
  connect(m_openFileButton, &QPushButton::clicked, this, [this]() {
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestOpenFile(*id);
  });
  connect(m_retryButton, &QPushButton::clicked, this, [this]() {
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestRetry(*id);
  });
  connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestPause(*id);
  });
  connect(m_resumeButton, &QPushButton::clicked, this, [this]() {
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestResume(*id);
  });
  connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
    if (const auto id = transferIdForIndex(m_list->currentIndex()); id.has_value())
      (void)m_transfers.requestCancel(*id);
  });
  connect(m_detailsMenuAction, &QAction::triggered, m_detailsButton, &QPushButton::click);
  connect(m_openFolderMenuAction, &QAction::triggered, m_openFolderButton, &QPushButton::click);
  connect(m_openFileMenuAction, &QAction::triggered, m_openFileButton, &QPushButton::click);
  connect(m_retryMenuAction, &QAction::triggered, m_retryButton, &QPushButton::click);
  connect(m_pauseMenuAction, &QAction::triggered, m_pauseButton, &QPushButton::click);
  connect(m_resumeMenuAction, &QAction::triggered, m_resumeButton, &QPushButton::click);
  connect(m_cancelMenuAction, &QAction::triggered, m_cancelButton, &QPushButton::click);
  updateText();
  updateEmptyState();
  updateSelection();
}

model::TransferCenterModel &TransferCenterDock::transferModel() const
{
  return m_transfers;
}

void TransferCenterDock::showCompletionOpenFailure()
{
  m_completionOpenFailed = true;
  updateFeedback();
}

void TransferCenterDock::clearCompletionOpenFailure()
{
  if (!m_completionOpenFailed)
    return;
  m_completionOpenFailed = false;
  updateFeedback();
}

void TransferCenterDock::showHistoryFailure()
{
  m_historyUnavailable = true;
  updateFeedback();
}

void TransferCenterDock::changeEvent(QEvent *event)
{
  QDockWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    updateText();
    updateSelection();
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
  m_detailsMenuAction->setText(m_detailsButton->text());
  m_openFolderMenuAction->setText(m_openFolderButton->text());
  m_openFileMenuAction->setText(m_openFileButton->text());
  m_retryMenuAction->setText(m_retryButton->text());
  m_pauseMenuAction->setText(m_pauseButton->text());
  m_resumeMenuAction->setText(m_resumeButton->text());
  m_cancelMenuAction->setText(m_cancelButton->text());
  updateFeedback();
}

void TransferCenterDock::updateFeedback()
{
  if (m_completionOpenFailed) {
    m_feedback->setText(i18n::translate(Text::TransferFeedbackOpenFailed));
    m_feedback->setAccessibleDescription(m_feedback->text());
    m_feedback->setVisible(true);
  } else if (m_historyUnavailable) {
    m_feedback->setText(i18n::translate(Text::TransferFeedbackHistoryUnavailable));
    m_feedback->setAccessibleDescription(m_feedback->text());
    m_feedback->setVisible(true);
  } else {
    m_feedback->clear();
    m_feedback->setAccessibleDescription({});
    m_feedback->setVisible(false);
  }
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
  const auto hasDetails = index.isValid() && index.data(model::TransferCenterModel::HasHistoryDetailsRole).toBool();
  const auto canOpenFolder = index.isValid() && index.data(model::TransferCenterModel::CanOpenFolderRole).toBool();
  const auto canOpenFile = index.isValid() && index.data(model::TransferCenterModel::CanOpenFileRole).toBool();
  const auto canRetry = index.isValid() && index.data(model::TransferCenterModel::CanRetryRole).toBool();
  const auto canPause = index.isValid() && index.data(model::TransferCenterModel::CanPauseRole).toBool();
  const auto canResume = index.isValid() && index.data(model::TransferCenterModel::CanResumeRole).toBool();
  const auto canCancel = index.isValid() && index.data(model::TransferCenterModel::CanCancelRole).toBool();

  struct ActionState
  {
    QPushButton *button;
    QAction *menuAction;
    bool available;
  };
  const QList<ActionState> actions{
      {m_detailsButton, m_detailsMenuAction, hasDetails},
      {m_openFolderButton, m_openFolderMenuAction, canOpenFolder},
      {m_openFileButton, m_openFileMenuAction, canOpenFile},
      {m_retryButton, m_retryMenuAction, canRetry},
      {m_pauseButton, m_pauseMenuAction, canPause},
      {m_resumeButton, m_resumeMenuAction, canResume},
      {m_cancelButton, m_cancelMenuAction, canCancel},
  };

  QPushButton *primary = nullptr;
  if (canRetry)
    primary = m_retryButton;
  else if (canResume)
    primary = m_resumeButton;
  else if (canPause)
    primary = m_pauseButton;
  else if (canOpenFile)
    primary = m_openFileButton;
  else if (canOpenFolder)
    primary = m_openFolderButton;
  else if (hasDetails)
    primary = m_detailsButton;
  else if (canCancel)
    primary = m_cancelButton;

  QStringList secondaryNames;
  for (const auto &action : actions) {
    const auto primaryAction = action.available && action.button == primary;
    action.button->setVisible(primaryAction);
    action.button->setEnabled(action.available);
    action.menuAction->setVisible(action.available && !primaryAction);
    action.menuAction->setEnabled(action.available);
    if (action.available && !primaryAction)
      secondaryNames.append(action.menuAction->text());
  }

  const auto hasSecondaryActions = !secondaryNames.isEmpty();
  m_moreButton->setVisible(hasSecondaryActions);
  m_moreButton->setEnabled(hasSecondaryActions);
  const auto accessibleActions = secondaryNames.join(QStringLiteral(" · "));
  m_moreButton->setAccessibleName(accessibleActions);
  m_moreButton->setToolTip(accessibleActions);
}

void TransferCenterDock::showHistoryDetails()
{
  const auto index = m_list->currentIndex();
  if (!index.isValid() || !index.data(model::TransferCenterModel::HasHistoryDetailsRole).toBool())
    return;
  const auto transferId = transferIdForIndex(index);
  if (!transferId.has_value())
    return;
  const auto record = m_transfers.historyRecord(*transferId);
  if (!record.has_value())
    return;
  auto *dialog = new TransferHistoryDetailsDialog(*record, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

} // namespace deskflow::relaydesk::widgets
