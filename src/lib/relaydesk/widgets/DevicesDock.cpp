/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/DevicesDock.h"

#include "relaydesk/i18n/ProductStrings.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

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
    QFont nameFont(option.font);
    nameFont.setWeight(QFont::DemiBold);
    painter->setFont(nameFont);
    painter->setPen(option.palette.color(foregroundRole));
    const QFontMetrics nameMetrics(nameFont);
    painter->drawText(
        content.left(), content.top(), content.width(), nameMetrics.height(), Qt::AlignLeft | Qt::AlignVCenter,
        nameMetrics.elidedText(
            index.data(model::DeviceHomeModel::DisplayNameRole).toString(), Qt::ElideRight, content.width()
        )
    );

    painter->setFont(option.font);
    const QFontMetrics detailMetrics(option.font);
    QString status = index.data(model::DeviceHomeModel::StatusTextRole).toString();
    const auto latency = index.data(model::DeviceHomeModel::LatencyMsRole).toInt();
    if (latency >= 0) {
      status += QStringLiteral("  ·  ") + i18n::translate(Text::DevicesLatency).arg(QLocale().toString(latency));
    }

    const auto pairable = index.data(model::DeviceHomeModel::CanStartPairingRole).toBool();
    const auto pairText = pairable ? index.data(model::DeviceHomeModel::PairActionTextRole).toString() : QString();
    const auto pairWidth = pairable ? detailMetrics.horizontalAdvance(pairText) + 12 : 0;
    const QRect detailRect(
        content.left(), content.bottom() - detailMetrics.height(), content.width() - pairWidth, detailMetrics.height()
    );
    painter->setPen(option.palette.color(foregroundRole));
    painter->drawText(
        detailRect, Qt::AlignLeft | Qt::AlignVCenter,
        detailMetrics.elidedText(status, Qt::ElideRight, detailRect.width())
    );
    if (pairable) {
      painter->setPen(option.palette.color(
          option.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Link
      ));
      const QRect pairRect(content.right() - pairWidth, detailRect.top(), pairWidth, detailRect.height());
      painter->drawText(pairRect, Qt::AlignRight | Qt::AlignVCenter, pairText);
    }
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
  setObjectName(QStringLiteral("relaydeskDevicesDock"));
  setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  setMinimumWidth(300);

  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(12, 10, 12, 12);
  layout->setSpacing(8);

  m_permissionBanner = new QFrame(body);
  m_permissionBanner->setObjectName(QStringLiteral("relaydeskPermissionBanner"));
  m_permissionBanner->setFrameShape(QFrame::StyledPanel);
  auto *permissionLayout = new QVBoxLayout(m_permissionBanner);
  permissionLayout->setContentsMargins(10, 9, 10, 9);
  permissionLayout->setSpacing(5);
  m_permissionTitle = new QLabel(m_permissionBanner);
  m_permissionTitle->setObjectName(QStringLiteral("relaydeskPermissionTitle"));
  QFont permissionTitleFont(m_permissionTitle->font());
  permissionTitleFont.setWeight(QFont::DemiBold);
  m_permissionTitle->setFont(permissionTitleFont);
  permissionLayout->addWidget(m_permissionTitle);
  m_permissionMessage = new QLabel(m_permissionBanner);
  m_permissionMessage->setObjectName(QStringLiteral("relaydeskPermissionMessage"));
  m_permissionMessage->setWordWrap(true);
  permissionLayout->addWidget(m_permissionMessage);
  m_openPermissionSettingsButton = new QPushButton(m_permissionBanner);
  m_openPermissionSettingsButton->setObjectName(QStringLiteral("relaydeskOpenPermissionSettingsButton"));
  permissionLayout->addWidget(m_openPermissionSettingsButton, 0, Qt::AlignLeft);
  layout->addWidget(m_permissionBanner);

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
  layout->addWidget(m_deviceList, 1);

  m_pairButton = new QPushButton(body);
  m_pairButton->setObjectName(QStringLiteral("relaydeskPairSelectedButton"));
  layout->addWidget(m_pairButton);

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

  connect(&m_pairing, &model::PairingWizardModel::changed, this, &DevicesDock::updatePairingPanel);
  connect(&m_permissions, &model::PermissionStatusModel::snapshotChanged, this, &DevicesDock::updatePermissionBanner);
  connect(m_openPermissionSettingsButton, &QPushButton::clicked, this, [this]() {
    (void)m_permissions.requestPrimarySettings();
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

void DevicesDock::changeEvent(QEvent *event)
{
  QDockWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    updateText();
    updateSelection();
    updatePairingPanel();
    updatePermissionBanner();
    m_deviceList->viewport()->update();
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
  m_openPermissionSettingsButton->setText(m_permissions.openSettingsActionText());
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
  const auto pairable = index.isValid() && index.data(model::DeviceHomeModel::CanStartPairingRole).toBool();
  m_pairButton->setEnabled(pairable);
  m_pairButton->setVisible(m_devices.rowCount() != 0);
  m_pairButton->setText(
      pairable ? index.data(model::DeviceHomeModel::PairActionTextRole).toString()
               : i18n::translate(Text::PairingActionStart)
  );
}

void DevicesDock::requestPairing(const QModelIndex &index)
{
  if (!index.isValid() || !index.data(model::DeviceHomeModel::CanStartPairingRole).toBool())
    return;
  const auto id = DeviceId::fromString(index.data(model::DeviceHomeModel::DeviceIdRole).toString());
  if (!id.has_value())
    return;
  const auto peer = m_devices.snapshot(*id);
  if (peer.has_value())
    Q_EMIT pairingRequested(*peer);
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
  const auto visible = m_permissions.bannerVisible();
  m_permissionBanner->setVisible(visible);
  if (!visible)
    return;

  m_permissionTitle->setText(m_permissions.bannerTitle());
  m_permissionMessage->setText(m_permissions.bannerMessage());
  m_openPermissionSettingsButton->setText(m_permissions.openSettingsActionText());
  m_openPermissionSettingsButton->setVisible(m_permissions.canOpenPrimarySettings());
  m_permissionBanner->setAccessibleName(m_permissions.bannerTitle());
  m_permissionBanner->setAccessibleDescription(m_permissions.bannerMessage());
  m_openPermissionSettingsButton->setAccessibleName(m_permissions.openSettingsActionText());
}

} // namespace deskflow::relaydesk::widgets
