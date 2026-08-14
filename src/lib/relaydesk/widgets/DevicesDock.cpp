/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/DevicesDock.h"

#include "relaydesk/i18n/ProductStrings.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/IncomingOfferModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;

class DeviceCardDelegate final : public QStyledItemDelegate
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
    const auto presence = static_cast<DevicePresence>(index.data(model::DeviceHomeModel::PresenceRole).toInt());
    QColor accent = option.palette.color(QPalette::Mid);
    switch (presence) {
    case DevicePresence::Online:
      accent = option.palette.color(QPalette::Highlight);
      break;
    case DevicePresence::Discovered:
      accent = option.palette.color(QPalette::Link);
      break;
    case DevicePresence::Pairing:
      accent = option.palette.color(QPalette::LinkVisited);
      break;
    case DevicePresence::TrustViolation:
      accent = option.palette.color(QPalette::Text);
      break;
    case DevicePresence::Offline:
      accent = option.palette.color(QPalette::Disabled, QPalette::Text);
      break;
    }
    const QRect accentRect(option.rect.left() + 4, option.rect.top() + 8, 4, option.rect.height() - 16);
    painter->fillRect(accentRect, accent);

    const QRect content = option.rect.adjusted(16, 9, -12, -9);
    const auto foregroundRole =
        option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
    const auto pairable = index.data(model::DeviceHomeModel::CanStartPairingRole).toBool();
    const auto sendable = index.data(model::DeviceHomeModel::CanSendItemsRole).toBool();
    const auto actionText = pairable ? index.data(model::DeviceHomeModel::PairActionTextRole).toString()
                                     : sendable ? i18n::translate(Text::DevicesActionSendFile) : QString();
    const QFontMetrics detailMetrics(option.font);
    const auto actionWidth = actionText.isEmpty() ? 0 : detailMetrics.horizontalAdvance(actionText) + 12;

    QFont nameFont(option.font);
    nameFont.setWeight(QFont::DemiBold);
    painter->setFont(nameFont);
    painter->setPen(option.palette.color(foregroundRole));
    const QFontMetrics nameMetrics(nameFont);
    const QRect nameRect(content.left(), content.top(), content.width() - actionWidth, nameMetrics.height());
    painter->drawText(
        nameRect, Qt::AlignLeft | Qt::AlignVCenter,
        nameMetrics.elidedText(
            index.data(model::DeviceHomeModel::DisplayNameRole).toString(), Qt::ElideRight, nameRect.width()
        )
    );

    if (!actionText.isEmpty()) {
      painter->setPen(option.palette.color(
          option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Link
      ));
      const QRect actionRect(content.right() - actionWidth, content.top(), actionWidth, nameMetrics.height());
      painter->drawText(actionRect, Qt::AlignRight | Qt::AlignVCenter, actionText);
    }

    painter->setFont(option.font);
    QString status = index.data(model::DeviceHomeModel::StatusTextRole).toString();
    const auto latency = index.data(model::DeviceHomeModel::LatencyMsRole).toInt();
    if (latency >= 0) {
      status += QStringLiteral("  ·  ") + i18n::translate(Text::DevicesLatency).arg(QLocale().toString(latency));
    }
    const QRect detailRect(
        content.left(), content.bottom() - detailMetrics.height(), content.width(), detailMetrics.height()
    );
    painter->setPen(option.palette.color(foregroundRole));
    painter->drawText(
        detailRect, Qt::AlignLeft | Qt::AlignVCenter,
        detailMetrics.elidedText(status, Qt::ElideRight, detailRect.width())
    );
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
  {
    return {300, option.fontMetrics.height() * 2 + 30};
  }
};

QString groupedSas(const QString &sas)
{
  return sas.size() == 6 ? sas.left(3) + QStringLiteral(" ") + sas.mid(3) : sas;
}

QList<QUrl> chooseLocalFiles(QWidget &parent)
{
  QList<QUrl> urls;
  const auto paths = QFileDialog::getOpenFileNames(
      &parent, i18n::translate(Text::DevicesActionSendFile)
  );
  urls.reserve(paths.size());
  for (const auto &path : paths)
    urls.push_back(QUrl::fromLocalFile(path));
  return urls;
}

QList<QUrl> chooseLocalFolder(QWidget &parent)
{
  const auto path = QFileDialog::getExistingDirectory(
      &parent, i18n::translate(Text::DevicesActionSendFolder)
  );
  return path.isEmpty() ? QList<QUrl>{} : QList<QUrl>{QUrl::fromLocalFile(path)};
}

} // namespace

DevicesDock::DevicesDock(
    model::DeviceHomeModel &devices, model::PairingWizardModel &pairing, model::PermissionStatusModel &permissions,
    QWidget *parent
)
    : QDockWidget(parent),
      m_devices(devices),
      m_pairing(pairing),
      m_permissions(permissions)
{
  m_fileChooser = chooseLocalFiles;
  m_folderChooser = chooseLocalFolder;
  setObjectName(QStringLiteral("relaydeskDevicesDock"));
  setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  setMinimumWidth(300);
  setAcceptDrops(true);

  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(12, 10, 12, 12);
  layout->setSpacing(8);

  m_permissionBanner = new QFrame(body);
  m_permissionBanner->setObjectName(QStringLiteral("relaydeskPermissionBanner"));
  m_permissionBanner->setFrameShape(QFrame::StyledPanel);
  auto *permissionLayout = new QVBoxLayout(m_permissionBanner);
  permissionLayout->setContentsMargins(8, 4, 8, 4);
  permissionLayout->setSpacing(4);
  auto *permissionSummaryLayout = new QHBoxLayout();
  permissionSummaryLayout->setContentsMargins(0, 0, 0, 0);
  permissionSummaryLayout->setSpacing(6);
  m_permissionDetailsToggle = new QToolButton(m_permissionBanner);
  m_permissionDetailsToggle->setObjectName(QStringLiteral("relaydeskPermissionDetailsButton"));
  m_permissionDetailsToggle->setAutoRaise(true);
  m_permissionDetailsToggle->setArrowType(Qt::RightArrow);
  permissionSummaryLayout->addWidget(m_permissionDetailsToggle);
  m_permissionTitle = new QLabel(m_permissionBanner);
  m_permissionTitle->setObjectName(QStringLiteral("relaydeskPermissionTitle"));
  m_permissionTitle->setTextFormat(Qt::PlainText);
  m_permissionTitle->setWordWrap(false);
  QFont permissionTitleFont(m_permissionTitle->font());
  permissionTitleFont.setWeight(QFont::DemiBold);
  m_permissionTitle->setFont(permissionTitleFont);
  permissionSummaryLayout->addWidget(m_permissionTitle);
  m_permissionMessage = new QLabel(m_permissionBanner);
  m_permissionMessage->setObjectName(QStringLiteral("relaydeskPermissionMessage"));
  m_permissionMessage->setTextFormat(Qt::PlainText);
  m_permissionMessage->setWordWrap(false);
  m_permissionMessage->setMinimumWidth(0);
  m_permissionMessage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  permissionSummaryLayout->addWidget(m_permissionMessage, 1);
  m_openPermissionSettingsButton = new QPushButton(m_permissionBanner);
  m_openPermissionSettingsButton->setObjectName(QStringLiteral("relaydeskOpenPermissionSettingsButton"));
  m_openPermissionSettingsButton->setFlat(true);
  permissionSummaryLayout->addWidget(m_openPermissionSettingsButton);
  permissionLayout->addLayout(permissionSummaryLayout);

  m_permissionDetailsPanel = new QDialog(this);
  m_permissionDetailsPanel->setObjectName(QStringLiteral("relaydeskPermissionDetailsPanel"));
  m_permissionDetailsPanel->setModal(false);
  auto *permissionDetailsLayout = new QVBoxLayout(m_permissionDetailsPanel);
  permissionDetailsLayout->setContentsMargins(12, 12, 12, 12);
  permissionDetailsLayout->setSpacing(6);
  for (int row = 0; row < m_permissions.rowCount(); ++row) {
    auto *detailRow = new QFrame(m_permissionDetailsPanel);
    detailRow->setObjectName(QStringLiteral("relaydeskPermissionDetailRow%1").arg(row));
    detailRow->setFrameShape(QFrame::StyledPanel);
    auto *detailLayout = new QGridLayout(detailRow);
    detailLayout->setContentsMargins(8, 6, 8, 6);
    detailLayout->setHorizontalSpacing(8);
    detailLayout->setVerticalSpacing(2);

    auto *title = new QLabel(detailRow);
    title->setObjectName(QStringLiteral("relaydeskPermissionDetailTitle%1").arg(row));
    title->setTextFormat(Qt::PlainText);
    QFont titleFont(title->font());
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    auto *status = new QLabel(detailRow);
    status->setObjectName(QStringLiteral("relaydeskPermissionDetailStatus%1").arg(row));
    status->setTextFormat(Qt::PlainText);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *settings = new QPushButton(detailRow);
    settings->setObjectName(QStringLiteral("relaydeskPermissionSettingsButton%1").arg(row));
    settings->setFlat(true);
    auto *purpose = new QLabel(detailRow);
    purpose->setObjectName(QStringLiteral("relaydeskPermissionDetailPurpose%1").arg(row));
    purpose->setTextFormat(Qt::PlainText);
    purpose->setWordWrap(true);
    auto *capability = new QLabel(detailRow);
    capability->setObjectName(QStringLiteral("relaydeskPermissionDetailCapability%1").arg(row));
    capability->setTextFormat(Qt::PlainText);
    capability->setWordWrap(true);

    detailLayout->addWidget(title, 0, 0);
    detailLayout->addWidget(status, 0, 1);
    detailLayout->addWidget(settings, 0, 2);
    detailLayout->addWidget(purpose, 1, 0, 1, 3);
    detailLayout->addWidget(capability, 2, 0, 1, 3);
    permissionDetailsLayout->addWidget(detailRow);

    m_permissionDetailTitles.append(title);
    m_permissionDetailPurposes.append(purpose);
    m_permissionDetailStatuses.append(status);
    m_permissionDetailCapabilities.append(capability);
    m_permissionDetailSettingsButtons.append(settings);
    connect(settings, &QPushButton::clicked, this, [this, row]() { (void)m_permissions.requestOpenSettings(row); });
  }
  m_permissionDetailsPanel->setVisible(false);
  layout->addWidget(m_permissionBanner);

  m_permissionBanner->installEventFilter(this);
  m_permissionTitle->installEventFilter(this);
  m_permissionMessage->installEventFilter(this);
  m_permissionTitle->setCursor(Qt::PointingHandCursor);
  m_permissionMessage->setCursor(Qt::PointingHandCursor);

  m_emptyLabel = new QLabel(body);
  m_emptyLabel->setObjectName(QStringLiteral("relaydeskDevicesEmptyLabel"));
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setMargin(18);
  layout->addWidget(m_emptyLabel, 1);

  m_deviceList = new QListView(body);
  m_deviceList->setObjectName(QStringLiteral("relaydeskDevicesView"));
  m_deviceList->setModel(&m_devices);
  m_deviceList->setItemDelegate(new DeviceCardDelegate(m_deviceList));
  m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_deviceList->setUniformItemSizes(true);
  m_deviceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_deviceList->setFrameShape(QFrame::NoFrame);
  m_deviceList->setAcceptDrops(true);
  m_deviceList->setDragDropMode(QAbstractItemView::DropOnly);
  m_deviceList->setDefaultDropAction(Qt::CopyAction);
  m_deviceList->viewport()->setAcceptDrops(true);
  m_deviceList->viewport()->installEventFilter(this);
  layout->addWidget(m_deviceList, 1);

  m_pairButton = new QPushButton(body);
  m_pairButton->setObjectName(QStringLiteral("relaydeskPairSelectedButton"));
  layout->addWidget(m_pairButton);

  auto *sendActions = new QHBoxLayout();
  m_sendFilesButton = new QPushButton(body);
  m_sendFilesButton->setObjectName(QStringLiteral("relaydeskSendFilesButton"));
  m_sendFolderButton = new QPushButton(body);
  m_sendFolderButton->setObjectName(QStringLiteral("relaydeskSendFolderButton"));
  sendActions->addWidget(m_sendFilesButton);
  sendActions->addWidget(m_sendFolderButton);
  layout->addLayout(sendActions);

  m_sendFeedback = new QLabel(body);
  m_sendFeedback->setObjectName(QStringLiteral("relaydeskSendFeedback"));
  m_sendFeedback->setWordWrap(true);
  m_sendFeedback->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  layout->addWidget(m_sendFeedback);

  m_incomingOfferPanel = new QFrame(body);
  m_incomingOfferPanel->setObjectName(QStringLiteral("relaydeskIncomingOfferPanel"));
  m_incomingOfferPanel->setFrameShape(QFrame::StyledPanel);
  auto *incomingLayout = new QVBoxLayout(m_incomingOfferPanel);
  incomingLayout->setContentsMargins(12, 12, 12, 12);
  incomingLayout->setSpacing(6);
  m_incomingOfferHeading = new QLabel(m_incomingOfferPanel);
  m_incomingOfferHeading->setObjectName(QStringLiteral("relaydeskIncomingOfferHeading"));
  m_incomingOfferHeading->setTextFormat(Qt::PlainText);
  QFont incomingHeadingFont(m_incomingOfferHeading->font());
  incomingHeadingFont.setWeight(QFont::DemiBold);
  m_incomingOfferHeading->setFont(incomingHeadingFont);
  incomingLayout->addWidget(m_incomingOfferHeading);
  m_incomingOfferName = new QLabel(m_incomingOfferPanel);
  m_incomingOfferName->setObjectName(QStringLiteral("relaydeskIncomingOfferName"));
  m_incomingOfferName->setTextFormat(Qt::PlainText);
  m_incomingOfferName->setWordWrap(true);
  incomingLayout->addWidget(m_incomingOfferName);
  m_incomingOfferSummary = new QLabel(m_incomingOfferPanel);
  m_incomingOfferSummary->setObjectName(QStringLiteral("relaydeskIncomingOfferSummary"));
  m_incomingOfferSummary->setTextFormat(Qt::PlainText);
  incomingLayout->addWidget(m_incomingOfferSummary);
  m_incomingOfferDestination = new QLabel(m_incomingOfferPanel);
  m_incomingOfferDestination->setObjectName(QStringLiteral("relaydeskIncomingOfferDestination"));
  m_incomingOfferDestination->setTextFormat(Qt::PlainText);
  m_incomingOfferDestination->setWordWrap(true);
  incomingLayout->addWidget(m_incomingOfferDestination);
  m_incomingOfferConflict = new QLabel(m_incomingOfferPanel);
  m_incomingOfferConflict->setObjectName(QStringLiteral("relaydeskIncomingOfferConflict"));
  m_incomingOfferConflict->setTextFormat(Qt::PlainText);
  incomingLayout->addWidget(m_incomingOfferConflict);
  m_incomingOfferError = new QLabel(m_incomingOfferPanel);
  m_incomingOfferError->setObjectName(QStringLiteral("relaydeskIncomingOfferError"));
  m_incomingOfferError->setTextFormat(Qt::PlainText);
  m_incomingOfferError->setWordWrap(true);
  incomingLayout->addWidget(m_incomingOfferError);

  auto *incomingActions = new QHBoxLayout();
  m_rejectIncomingOfferButton = new QPushButton(m_incomingOfferPanel);
  m_rejectIncomingOfferButton->setObjectName(QStringLiteral("relaydeskRejectIncomingOfferButton"));
  m_changeIncomingOfferSettingsButton = new QPushButton(m_incomingOfferPanel);
  m_changeIncomingOfferSettingsButton->setObjectName(QStringLiteral("relaydeskChangeIncomingOfferSettingsButton"));
  m_acceptIncomingOfferButton = new QPushButton(m_incomingOfferPanel);
  m_acceptIncomingOfferButton->setObjectName(QStringLiteral("relaydeskAcceptIncomingOfferButton"));
  m_dismissIncomingOfferButton = new QPushButton(m_incomingOfferPanel);
  m_dismissIncomingOfferButton->setObjectName(QStringLiteral("relaydeskDismissIncomingOfferButton"));
  incomingActions->addWidget(m_rejectIncomingOfferButton);
  incomingActions->addWidget(m_changeIncomingOfferSettingsButton);
  incomingActions->addStretch();
  incomingActions->addWidget(m_acceptIncomingOfferButton);
  incomingActions->addWidget(m_dismissIncomingOfferButton);
  incomingLayout->addLayout(incomingActions);
  layout->addWidget(m_incomingOfferPanel);

  m_pairingPanel = new QFrame(body);
  m_pairingPanel->setObjectName(QStringLiteral("relaydeskPairingPanel"));
  m_pairingPanel->setFrameShape(QFrame::StyledPanel);
  auto *pairingLayout = new QVBoxLayout(m_pairingPanel);
  pairingLayout->setContentsMargins(12, 12, 12, 12);
  pairingLayout->setSpacing(7);

  m_pairingPeer = new QLabel(m_pairingPanel);
  QFont peerFont(m_pairingPeer->font());
  peerFont.setWeight(QFont::DemiBold);
  m_pairingPeer->setFont(peerFont);
  pairingLayout->addWidget(m_pairingPeer);

  m_pairingState = new QLabel(m_pairingPanel);
  m_pairingState->setObjectName(QStringLiteral("relaydeskPairingStateLabel"));
  m_pairingState->setWordWrap(true);
  pairingLayout->addWidget(m_pairingState);

  m_pairingCode = new QLabel(m_pairingPanel);
  m_pairingCode->setObjectName(QStringLiteral("relaydeskPairingSasLabel"));
  auto sasFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  sasFont.setPointSizeF(qMax(sasFont.pointSizeF() + 5.0, 16.0));
  sasFont.setWeight(QFont::DemiBold);
  m_pairingCode->setFont(sasFont);
  m_pairingCode->setAlignment(Qt::AlignCenter);
  m_pairingCode->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  pairingLayout->addWidget(m_pairingCode);

  m_pairingExpiry = new QLabel(m_pairingPanel);
  m_pairingAttempts = new QLabel(m_pairingPanel);
  pairingLayout->addWidget(m_pairingExpiry);
  pairingLayout->addWidget(m_pairingAttempts);

  m_pairingError = new QLabel(m_pairingPanel);
  m_pairingError->setObjectName(QStringLiteral("relaydeskPairingErrorLabel"));
  m_pairingError->setWordWrap(true);
  m_pairingError->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  pairingLayout->addWidget(m_pairingError);

  m_pairingCodeEntry = new QLineEdit(m_pairingPanel);
  m_pairingCodeEntry->setObjectName(QStringLiteral("relaydeskPairingCodeEntry"));
  m_pairingCodeEntry->setMaxLength(6);
  m_pairingCodeEntry->setAlignment(Qt::AlignCenter);
  m_pairingCodeEntry->setInputMethodHints(Qt::ImhDigitsOnly);
  m_pairingCodeEntry->setValidator(
      new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9]{0,6}$")), m_pairingCodeEntry)
  );
  pairingLayout->addWidget(m_pairingCodeEntry);

  auto *pairingActions = new QHBoxLayout();
  m_confirmCodeButton = new QPushButton(m_pairingPanel);
  m_confirmCodeButton->setObjectName(QStringLiteral("relaydeskConfirmMatchingSasButton"));
  m_submitCodeButton = new QPushButton(m_pairingPanel);
  m_submitCodeButton->setObjectName(QStringLiteral("relaydeskSubmitPairingCodeButton"));
  m_cancelPairingButton = new QPushButton(m_pairingPanel);
  m_cancelPairingButton->setObjectName(QStringLiteral("relaydeskCancelPairingButton"));
  pairingActions->addWidget(m_confirmCodeButton);
  pairingActions->addWidget(m_submitCodeButton);
  pairingActions->addWidget(m_cancelPairingButton);
  pairingLayout->addLayout(pairingActions);

  m_fingerprintToggle = new QToolButton(m_pairingPanel);
  m_fingerprintToggle->setObjectName(QStringLiteral("relaydeskFingerprintToggle"));
  m_fingerprintToggle->setCheckable(true);
  m_fingerprintToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_fingerprintToggle->setArrowType(Qt::RightArrow);
  pairingLayout->addWidget(m_fingerprintToggle, 0, Qt::AlignLeft);

  m_fingerprintSummary = new QLabel(m_pairingPanel);
  m_fingerprintSummary->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  pairingLayout->addWidget(m_fingerprintSummary);
  m_fingerprintDetails = new QLabel(m_pairingPanel);
  m_fingerprintDetails->setObjectName(QStringLiteral("relaydeskFingerprintDetails"));
  m_fingerprintDetails->setWordWrap(true);
  m_fingerprintDetails->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  pairingLayout->addWidget(m_fingerprintDetails);
  layout->addWidget(m_pairingPanel);
  setWidget(body);

  connect(&m_devices, &QAbstractItemModel::rowsInserted, this, &DevicesDock::updateEmptyState);
  connect(&m_devices, &QAbstractItemModel::rowsRemoved, this, &DevicesDock::updateEmptyState);
  connect(&m_devices, &QAbstractItemModel::modelReset, this, &DevicesDock::updateEmptyState);
  connect(&m_devices, &QAbstractItemModel::dataChanged, this, &DevicesDock::updateSelection);
  connect(m_deviceList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &DevicesDock::updateSelection);
  connect(m_deviceList, &QListView::activated, this, &DevicesDock::requestPairing);
  connect(m_pairButton, &QPushButton::clicked, this, [this]() { requestPairing(m_deviceList->currentIndex()); });
  connect(m_sendFilesButton, &QPushButton::clicked, this, [this]() { chooseAndSend(false); });
  connect(m_sendFolderButton, &QPushButton::clicked, this, [this]() { chooseAndSend(true); });
  connect(m_acceptIncomingOfferButton, &QPushButton::clicked, this, [this]() {
    if (m_incomingOffers != nullptr)
      (void)m_incomingOffers->accept();
  });
  connect(m_rejectIncomingOfferButton, &QPushButton::clicked, this, [this]() {
    if (m_incomingOffers != nullptr)
      (void)m_incomingOffers->reject();
  });
  connect(m_changeIncomingOfferSettingsButton, &QPushButton::clicked, this, [this]() {
    Q_EMIT incomingOfferSettingsRequested();
  });
  connect(m_dismissIncomingOfferButton, &QPushButton::clicked, this, [this]() {
    if (m_incomingOffers != nullptr)
      m_incomingOffers->dismiss();
  });

  connect(&m_pairing, &model::PairingWizardModel::changed, this, &DevicesDock::updatePairingPanel);
  connect(&m_permissions, &model::PermissionStatusModel::snapshotChanged, this, &DevicesDock::updatePermissionBanner);
  connect(&m_permissions, &model::PermissionStatusModel::snapshotChanged, this, &DevicesDock::updateSelection);
  connect(m_openPermissionSettingsButton, &QPushButton::clicked, this, [this]() {
    (void)m_permissions.requestPrimarySettings();
  });
  connect(m_permissionDetailsToggle, &QToolButton::clicked, this, [this]() {
    updatePermissionDetails();
    m_permissionDetailsPanel->show();
    m_permissionDetailsPanel->raise();
    m_permissionDetailsPanel->activateWindow();
  });
  connect(m_confirmCodeButton, &QPushButton::clicked, &m_pairing, &model::PairingWizardModel::confirmMatchingSas);
  connect(m_submitCodeButton, &QPushButton::clicked, this, &DevicesDock::submitPairingCode);
  connect(m_pairingCodeEntry, &QLineEdit::returnPressed, this, &DevicesDock::submitPairingCode);
  connect(m_cancelPairingButton, &QPushButton::clicked, &m_pairing, &model::PairingWizardModel::cancel);
  connect(m_pairingCodeEntry, &QLineEdit::textChanged, this, &DevicesDock::updatePairingPanel);
  connect(m_fingerprintToggle, &QToolButton::toggled, this, [this](bool expanded) {
    m_fingerprintToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_fingerprintDetails->setVisible(expanded);
  });

  updateText();
  updateEmptyState();
  updateSelection();
  showSendFeedback({});
  updateIncomingOfferPanel();
  updatePairingPanel();
  updatePermissionBanner();
}

model::DeviceHomeModel &DevicesDock::deviceModel() const
{
  return m_devices;
}

model::PairingWizardModel &DevicesDock::pairingModel() const
{
  return m_pairing;
}

model::PermissionStatusModel &DevicesDock::permissionModel() const
{
  return m_permissions;
}

void DevicesDock::setFileChooser(ItemChooser chooser)
{
  m_fileChooser = std::move(chooser);
}

void DevicesDock::setFolderChooser(ItemChooser chooser)
{
  m_folderChooser = std::move(chooser);
}

void DevicesDock::setIncomingOfferModel(model::IncomingOfferModel *incomingOffers)
{
  if (m_incomingOffers == incomingOffers)
    return;
  if (m_incomingOffers != nullptr)
    disconnect(m_incomingOffers, nullptr, this, nullptr);
  m_incomingOffers = incomingOffers;
  if (m_incomingOffers != nullptr) {
    connect(m_incomingOffers, &model::IncomingOfferModel::changed, this, &DevicesDock::updateIncomingOfferPanel);
    connect(m_incomingOffers, &QObject::destroyed, this, [this]() {
      m_incomingOffers = nullptr;
      updateIncomingOfferPanel();
    });
  }
  updateIncomingOfferPanel();
}

void DevicesDock::changeEvent(QEvent *event)
{
  QDockWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    updateText();
    updateSelection();
    updatePairingPanel();
    updatePermissionBanner();
    updateIncomingOfferPanel();
    m_deviceList->viewport()->update();
  }
}

bool DevicesDock::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_permissionBanner || watched == m_permissionTitle || watched == m_permissionMessage) {
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
      m_permissionDetailsToggle->click();
      return true;
    }
    return QDockWidget::eventFilter(watched, event);
  }

  if (watched != m_deviceList->viewport())
    return QDockWidget::eventFilter(watched, event);

  switch (event->type()) {
  case QEvent::DragEnter: {
    auto *drag = static_cast<QDragEnterEvent *>(event);
    const auto index = m_deviceList->indexAt(drag->position().toPoint());
    const auto items = drag->mimeData()->hasUrls() ? drag->mimeData()->urls() : QList<QUrl>{};
    if (updateDropTarget(index, items)) {
      drag->setDropAction(Qt::CopyAction);
      drag->accept();
    } else {
      drag->ignore();
    }
    return true;
  }
  case QEvent::DragMove: {
    auto *drag = static_cast<QDragMoveEvent *>(event);
    const auto index = m_deviceList->indexAt(drag->position().toPoint());
    const auto items = drag->mimeData()->hasUrls() ? drag->mimeData()->urls() : QList<QUrl>{};
    if (updateDropTarget(index, items)) {
      drag->setDropAction(Qt::CopyAction);
      drag->accept();
    } else {
      drag->ignore();
    }
    return true;
  }
  case QEvent::DragLeave:
    clearDropTarget();
    static_cast<QDragLeaveEvent *>(event)->accept();
    return true;
  case QEvent::Drop: {
    auto *drop = static_cast<QDropEvent *>(event);
    const auto index = m_deviceList->indexAt(drop->position().toPoint());
    const auto items = drop->mimeData()->hasUrls() ? drop->mimeData()->urls() : QList<QUrl>{};
    const auto published = publishSendIntent(index, items);
    m_dropTargetIndex = QPersistentModelIndex{};
    if (published) {
      showSendFeedback({});
      drop->setDropAction(Qt::CopyAction);
      drop->accept();
    } else {
      drop->ignore();
    }
    return true;
  }
  default:
    return QDockWidget::eventFilter(watched, event);
  }
}

void DevicesDock::dragEnterEvent(QDragEnterEvent *event)
{
  const auto items = event->mimeData()->hasUrls() ? event->mimeData()->urls() : QList<QUrl>{};
  if (updateDropTarget(targetIndexAt(event->position().toPoint()), items)) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void DevicesDock::dragMoveEvent(QDragMoveEvent *event)
{
  const auto items = event->mimeData()->hasUrls() ? event->mimeData()->urls() : QList<QUrl>{};
  if (updateDropTarget(targetIndexAt(event->position().toPoint()), items)) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void DevicesDock::dragLeaveEvent(QDragLeaveEvent *event)
{
  clearDropTarget();
  event->accept();
}

void DevicesDock::dropEvent(QDropEvent *event)
{
  const auto items = event->mimeData()->hasUrls() ? event->mimeData()->urls() : QList<QUrl>{};
  const auto published = publishSendIntent(targetIndexAt(event->position().toPoint()), items);
  m_dropTargetIndex = QPersistentModelIndex{};
  if (published) {
    showSendFeedback({});
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void DevicesDock::updateText()
{
  setWindowTitle(i18n::translate(Text::DevicesTitle));
  m_deviceList->setAccessibleName(i18n::translate(Text::DevicesTitle));
  m_emptyLabel->setText(i18n::translate(Text::DevicesEmptyWaiting));
  m_pairingCodeEntry->setPlaceholderText(m_pairing.codePrompt());
  m_pairingCodeEntry->setAccessibleName(m_pairing.codePrompt());
  m_confirmCodeButton->setText(m_pairing.confirmActionText());
  m_submitCodeButton->setText(m_pairing.submitActionText());
  m_cancelPairingButton->setText(m_pairing.cancelActionText());
  m_fingerprintToggle->setText(m_pairing.fingerprintLabel());
  const auto detailsText = i18n::translate(Text::TransferActionDetails);
  m_permissionDetailsToggle->setToolTip(detailsText);
  m_permissionDetailsToggle->setAccessibleName(detailsText);
  m_permissionDetailsPanel->setWindowTitle(detailsText);
  m_openPermissionSettingsButton->setText(m_permissions.openSettingsActionText());
  m_sendFilesButton->setText(i18n::translate(Text::DevicesActionSendFile));
  m_sendFilesButton->setAccessibleName(i18n::translate(Text::DevicesActionSendFile));
  m_sendFolderButton->setText(i18n::translate(Text::DevicesActionSendFolder));
  m_sendFolderButton->setAccessibleName(i18n::translate(Text::DevicesActionSendFolder));
  m_sendFeedback->setAccessibleName(i18n::translate(Text::DevicesActionSendFile));
  m_acceptIncomingOfferButton->setText(i18n::translate(Text::TransferActionAccept));
  m_acceptIncomingOfferButton->setAccessibleName(i18n::translate(Text::TransferActionAccept));
  m_rejectIncomingOfferButton->setText(i18n::translate(Text::TransferActionReject));
  m_rejectIncomingOfferButton->setAccessibleName(i18n::translate(Text::TransferActionReject));
  m_changeIncomingOfferSettingsButton->setText(i18n::translate(Text::TransferActionChangeSettings));
  m_changeIncomingOfferSettingsButton->setAccessibleName(i18n::translate(Text::TransferActionChangeSettings));
  m_dismissIncomingOfferButton->setText(i18n::translate(Text::TransferActionDismiss));
  m_dismissIncomingOfferButton->setAccessibleName(i18n::translate(Text::TransferActionDismiss));
}

void DevicesDock::updateEmptyState()
{
  const auto empty = m_devices.rowCount() == 0;
  m_emptyLabel->setVisible(empty);
  m_deviceList->setVisible(!empty);
  if (empty)
    m_deviceList->setCurrentIndex({});
  updateSelection();
}

void DevicesDock::updateSelection()
{
  const auto index = m_deviceList->currentIndex();
  const auto devicePairable = index.isValid() && index.data(model::DeviceHomeModel::CanStartPairingRole).toBool();
  const auto pairable = devicePairable && m_permissions.canConnectDevices();
  m_pairButton->setEnabled(pairable);
  m_pairButton->setVisible(m_devices.rowCount() != 0);
  m_pairButton->setText(
      devicePairable ? index.data(model::DeviceHomeModel::PairActionTextRole).toString()
                     : i18n::translate(Text::PairingActionStart)
  );
  const auto sendable = index.isValid() && index.data(model::DeviceHomeModel::CanSendItemsRole).toBool();
  m_sendFilesButton->setVisible(m_devices.rowCount() != 0);
  m_sendFolderButton->setVisible(m_devices.rowCount() != 0);
  m_sendFilesButton->setEnabled(sendable);
  m_sendFolderButton->setEnabled(sendable);
}

void DevicesDock::chooseAndSend(bool folder)
{
  const auto &chooser = folder ? m_folderChooser : m_fileChooser;
  const auto index = m_deviceList->currentIndex();
  if (!index.isValid()) {
    showSendFeedback(i18n::translate(Text::DevicesSendSelectDevice));
    Q_EMIT sendItemsRejected(m_sendFeedback->text());
    return;
  }
  if (!sendTarget(index).has_value()) {
    showSendFeedback(i18n::translate(Text::DevicesSendUnavailable));
    Q_EMIT sendItemsRejected(m_sendFeedback->text());
    return;
  }
  if (!chooser) {
    showSendFeedback(i18n::translate(Text::DevicesSendEmpty));
    Q_EMIT sendItemsRejected(m_sendFeedback->text());
    return;
  }
  (void)publishSendIntent(index, chooser(*this));
}

QModelIndex DevicesDock::targetIndexAt(const QPoint &position) const
{
  return m_deviceList->indexAt(m_deviceList->viewport()->mapFrom(this, position));
}

std::optional<DeviceId> DevicesDock::sendTarget(const QModelIndex &index) const
{
  if (!index.isValid() || !index.data(model::DeviceHomeModel::CanSendItemsRole).toBool())
    return std::nullopt;
  return DeviceId::fromString(index.data(model::DeviceHomeModel::DeviceIdRole).toString());
}

QString DevicesDock::validateLocalItems(const QList<QUrl> &items) const
{
  if (items.isEmpty())
    return i18n::translate(Text::DevicesSendEmpty);

  for (const auto &url : items) {
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty())
      return i18n::translate(Text::DevicesSendLocalOnly);
    const QFileInfo info(url.toLocalFile());
    if (!info.exists() || !info.isReadable() || (!info.isFile() && !info.isDir()))
      return i18n::translate(Text::DevicesSendUnreadable);
  }
  return {};
}

bool DevicesDock::updateDropTarget(const QModelIndex &index, const QList<QUrl> &items)
{
  if (!sendTarget(index).has_value()) {
    m_dropTargetIndex = QPersistentModelIndex{};
    showSendFeedback(i18n::translate(Text::DevicesSendUnavailable));
    return false;
  }
  const auto error = validateLocalItems(items);
  if (!error.isEmpty()) {
    m_dropTargetIndex = QPersistentModelIndex{};
    showSendFeedback(error);
    return false;
  }

  m_dropTargetIndex = index;
  m_deviceList->setCurrentIndex(index);
  showSendFeedback(
      i18n::translatePlural(Text::DevicesDropItems, items.size()) + QStringLiteral(" · ")
      + i18n::translate(Text::DevicesDropSendHere)
  );
  return true;
}

void DevicesDock::clearDropTarget()
{
  m_dropTargetIndex = QPersistentModelIndex{};
  showSendFeedback({});
}

void DevicesDock::showSendFeedback(const QString &message)
{
  m_sendFeedback->setText(message);
  m_sendFeedback->setAccessibleDescription(message);
  m_sendFeedback->setVisible(!message.isEmpty());
}

bool DevicesDock::publishSendIntent(const QModelIndex &index, const QList<QUrl> &items)
{
  const auto peer = sendTarget(index);
  if (!peer.has_value()) {
    showSendFeedback(index.isValid() ? i18n::translate(Text::DevicesSendUnavailable)
                                     : i18n::translate(Text::DevicesSendSelectDevice));
    Q_EMIT sendItemsRejected(m_sendFeedback->text());
    return false;
  }
  const auto error = validateLocalItems(items);
  if (!error.isEmpty()) {
    showSendFeedback(error);
    Q_EMIT sendItemsRejected(error);
    return false;
  }

  Q_EMIT sendItemsRequested(*peer, items, ::relaydesk::transfer::SendOptions{});
  showSendFeedback({});
  return true;
}

void DevicesDock::requestPairing(const QModelIndex &index)
{
  if (!m_permissions.canConnectDevices() || !index.isValid() ||
      !index.data(model::DeviceHomeModel::CanStartPairingRole).toBool())
    return;
  const auto id = DeviceId::fromString(index.data(model::DeviceHomeModel::DeviceIdRole).toString());
  if (!id.has_value())
    return;
  Q_EMIT pairingRequested(*id);
}

void DevicesDock::updateIncomingOfferPanel()
{
  const auto visible = m_incomingOffers != nullptr && m_incomingOffers->visible();
  m_incomingOfferPanel->setVisible(visible);
  if (!visible)
    return;

  m_incomingOfferHeading->setText(m_incomingOffers->headingText());
  m_incomingOfferName->setText(m_incomingOffers->offerName());
  m_incomingOfferSummary->setText(m_incomingOffers->summaryText());
  m_incomingOfferDestination->setText(m_incomingOffers->destinationText());
  m_incomingOfferConflict->setText(m_incomingOffers->conflictText());
  m_incomingOfferError->setText(m_incomingOffers->errorText());
  m_incomingOfferError->setVisible(!m_incomingOffers->errorText().isEmpty());

  const auto active = m_incomingOffers->active();
  m_acceptIncomingOfferButton->setVisible(active && m_incomingOffers->peerTrusted());
  m_acceptIncomingOfferButton->setEnabled(m_incomingOffers->canAccept());
  m_rejectIncomingOfferButton->setVisible(active);
  m_changeIncomingOfferSettingsButton->setVisible(active);
  m_dismissIncomingOfferButton->setVisible(!active);

  m_incomingOfferPanel->setAccessibleName(m_incomingOffers->headingText());
  m_incomingOfferPanel->setAccessibleDescription(
      m_incomingOffers->summaryText() + QStringLiteral(". ") + m_incomingOffers->destinationText()
  );
}

void DevicesDock::updatePairingPanel()
{
  const auto active = m_pairing.active();
  m_pairingPanel->setVisible(active);
  if (!active)
    return;

  m_pairingPeer->setText(m_pairing.peerName());
  m_pairingState->setText(m_pairing.stateText());
  m_pairingCode->setText(groupedSas(m_pairing.sixDigitSas()));
  m_pairingCode->setVisible(!m_pairing.sixDigitSas().isEmpty());
  m_pairingExpiry->setText(i18n::translate(Text::PairingExpiresAt).arg(m_pairing.expiresAtText()));
  m_pairingExpiry->setVisible(!m_pairing.expiresAtText().isEmpty() && !m_pairing.terminal());
  m_pairingAttempts->setText(m_pairing.attemptsRemainingText());
  m_pairingAttempts->setVisible(m_pairing.canSubmitCode());
  m_pairingError->setText(m_pairing.errorText());
  m_pairingError->setVisible(!m_pairing.errorText().isEmpty());

  const auto acceptsUserAction = m_pairing.canSubmitCode();
  if (!acceptsUserAction)
    m_pairingCodeEntry->clear();
  m_pairingCodeEntry->setVisible(acceptsUserAction);
  m_confirmCodeButton->setVisible(m_pairing.canConfirmMatchingSas());
  m_submitCodeButton->setVisible(acceptsUserAction);
  m_submitCodeButton->setEnabled(acceptsUserAction && m_pairingCodeEntry->text().size() == 6);
  m_cancelPairingButton->setVisible(m_pairing.canCancel());

  m_fingerprintSummary->setText(m_pairing.shortFingerprint());
  m_fingerprintDetails->setText(m_pairing.fullFingerprint());
  m_fingerprintDetails->setVisible(m_fingerprintToggle->isChecked());
}

void DevicesDock::submitPairingCode()
{
  if (!m_pairing.canSubmitCode() || m_pairingCodeEntry->text().size() != 6)
    return;
  if (m_pairing.submitDisplayedSas(m_pairingCodeEntry->text()))
    m_pairingCodeEntry->clear();
}

void DevicesDock::updatePermissionBanner()
{
  updatePermissionDetails();
  const auto visible = m_permissions.bannerVisible();
  m_permissionBanner->setVisible(visible);
  if (!visible)
    return;

  m_permissionTitle->setText(m_permissions.bannerTitle());
  m_permissionMessage->setText(m_permissions.bannerMessage());
  m_permissionMessage->setToolTip(m_permissions.bannerMessage());
  m_openPermissionSettingsButton->setText(m_permissions.openSettingsActionText());
  m_openPermissionSettingsButton->setVisible(m_permissions.canOpenPrimarySettings());
  m_permissionBanner->setAccessibleName(m_permissions.bannerTitle());
  m_permissionBanner->setAccessibleDescription(m_permissions.bannerMessage());
  m_openPermissionSettingsButton->setAccessibleName(m_permissions.openSettingsActionText());
}

void DevicesDock::updatePermissionDetails()
{
  const auto rowCount = qMin(m_permissions.rowCount(), m_permissionDetailTitles.size());
  for (int row = 0; row < rowCount; ++row) {
    const auto index = m_permissions.index(row, 0);
    const auto title = index.data(model::PermissionStatusModel::TitleRole).toString();
    const auto purpose = index.data(model::PermissionStatusModel::PurposeTextRole).toString();
    const auto status = index.data(model::PermissionStatusModel::StatusTextRole).toString();
    const auto capability = index.data(model::PermissionStatusModel::AffectedCapabilityTextRole).toString();
    const auto action = index.data(model::PermissionStatusModel::ActionTextRole).toString();
    const auto canOpenSettings = index.data(model::PermissionStatusModel::CanOpenSettingsRole).toBool();

    m_permissionDetailTitles.at(row)->setText(title);
    m_permissionDetailPurposes.at(row)->setText(purpose);
    m_permissionDetailStatuses.at(row)->setText(status);
    m_permissionDetailCapabilities.at(row)->setText(capability);
    auto *settings = m_permissionDetailSettingsButtons.at(row);
    settings->setText(action);
    settings->setVisible(canOpenSettings);
    settings->setAccessibleName(title + QStringLiteral(" — ") + action);

    auto *detailRow = m_permissionDetailTitles.at(row)->parentWidget();
    detailRow->setAccessibleName(title);
    detailRow->setAccessibleDescription(
        QStringList{purpose, status, capability}.join(QStringLiteral(" · "))
    );
  }
}

} // namespace deskflow::relaydesk::widgets
