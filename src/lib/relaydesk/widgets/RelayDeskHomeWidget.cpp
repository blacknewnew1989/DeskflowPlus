/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/RelayDeskHomeWidget.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;

constexpr int kHeaderHeight = 52;
constexpr int kFooterHeight = 26;

} // namespace

RelayDeskHomeWidget::RelayDeskHomeWidget(QWidget *devices, QWidget *transferBar, QWidget *parent) : QWidget(parent)
{
  Q_ASSERT(devices != nullptr);
  Q_ASSERT(transferBar != nullptr);

  setObjectName(QStringLiteral("relaydeskCompactHome"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *header = new QFrame(this);
  header->setObjectName(QStringLiteral("relaydeskHomeHeader"));
  header->setFrameShape(QFrame::NoFrame);
  header->setFixedHeight(kHeaderHeight);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(14, 0, 12, 0);
  headerLayout->setSpacing(8);

  m_productIcon = new QLabel(header);
  m_productIcon->setObjectName(QStringLiteral("relaydeskHomeProductIcon"));
  m_productIcon->setFixedSize(30, 30);
  m_productIcon->setAlignment(Qt::AlignCenter);
  headerLayout->addWidget(m_productIcon);

  m_productName = new QLabel(header);
  m_productName->setObjectName(QStringLiteral("relaydeskHomeProductName"));
  QFont productFont(m_productName->font());
  productFont.setPointSize(productFont.pointSize() + 2);
  productFont.setWeight(QFont::DemiBold);
  m_productName->setFont(productFont);
  headerLayout->addWidget(m_productName);
  headerLayout->addSpacing(14);

  m_status = new QLabel(header);
  m_status->setObjectName(QStringLiteral("relaydeskHomeStatus"));
  m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  headerLayout->addWidget(m_status, 1);

  m_historyButton = new QToolButton(header);
  m_historyButton->setObjectName(QStringLiteral("relaydeskHomeHistoryButton"));
  m_historyButton->setAutoRaise(true);
  m_historyButton->setIcon(
      QIcon::fromTheme(QStringLiteral("view-history"), QIcon::fromTheme(QStringLiteral("view-refresh")))
  );
  m_historyButton->setIconSize(QSize(22, 22));
  m_historyButton->setToolTip(i18n::translate(Text::TransferTitle));
  m_historyButton->setAccessibleName(i18n::translate(Text::TransferTitle));
  headerLayout->addWidget(m_historyButton);

  m_settingsButton = new QToolButton(header);
  m_settingsButton->setObjectName(QStringLiteral("relaydeskHomeSettingsButton"));
  m_settingsButton->setAutoRaise(true);
  m_settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_settingsButton->setIconSize(QSize(22, 22));
  m_settingsButton->setToolTip(i18n::translate(Text::SettingsTitle));
  m_settingsButton->setAccessibleName(i18n::translate(Text::SettingsTitle));
  headerLayout->addWidget(m_settingsButton);
  layout->addWidget(header);

  devices->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(devices, 1);

  transferBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  layout->addWidget(transferBar);

  auto *footer = new QFrame(this);
  footer->setObjectName(QStringLiteral("relaydeskHomeFooter"));
  footer->setFrameShape(QFrame::NoFrame);
  footer->setFixedHeight(kFooterHeight);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(14, 0, 14, 0);
  m_localDevice = new QLabel(footer);
  m_localDevice->setObjectName(QStringLiteral("relaydeskHomeLocalDevice"));
  m_localDevice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  footerLayout->addWidget(m_localDevice);
  layout->addWidget(footer);

  connect(m_historyButton, &QToolButton::clicked, this, &RelayDeskHomeWidget::transferHistoryRequested);
  connect(m_settingsButton, &QToolButton::clicked, this, &RelayDeskHomeWidget::settingsRequested);
}

void RelayDeskHomeWidget::setProductName(const QString &name)
{
  m_productName->setText(name);
}

void RelayDeskHomeWidget::setProductIcon(const QIcon &icon)
{
  m_productIcon->setPixmap(icon.pixmap(m_productIcon->size()));
}

void RelayDeskHomeWidget::setStatusText(const QString &status)
{
  const auto text = QStringLiteral("●  %1").arg(status);
  m_status->setText(text);
  m_status->setToolTip(status);
  m_status->setAccessibleName(status);
}

void RelayDeskHomeWidget::setLocalDeviceName(const QString &name)
{
  m_localDeviceName = name;
  const auto text = QStringLiteral("%1 · %2").arg(i18n::translate(Text::DevicesCurrent), name);
  m_localDevice->setText(text);
  m_localDevice->setToolTip(text);
  m_localDevice->setAccessibleName(text);
}

void RelayDeskHomeWidget::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() != QEvent::LanguageChange)
    return;

  m_historyButton->setToolTip(i18n::translate(Text::TransferTitle));
  m_historyButton->setAccessibleName(i18n::translate(Text::TransferTitle));
  m_settingsButton->setToolTip(i18n::translate(Text::SettingsTitle));
  m_settingsButton->setAccessibleName(i18n::translate(Text::SettingsTitle));
  setLocalDeviceName(m_localDeviceName);
}

} // namespace deskflow::relaydesk::widgets
