/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class QIcon;
class QEvent;
class QLabel;
class QToolButton;

namespace deskflow::relaydesk::widgets {

class RelayDeskHomeWidget final : public QWidget
{
  Q_OBJECT

public:
  explicit RelayDeskHomeWidget(QWidget *devices, QWidget *transferBar, QWidget *parent = nullptr);

  void setProductName(const QString &name);
  void setProductIcon(const QIcon &icon);
  void setStatusText(const QString &status);
  void setLocalDeviceName(const QString &name);

Q_SIGNALS:
  void settingsRequested();
  void transferHistoryRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  QLabel *m_productIcon = nullptr;
  QLabel *m_productName = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_localDevice = nullptr;
  QToolButton *m_historyButton = nullptr;
  QToolButton *m_settingsButton = nullptr;
  QString m_localDeviceName;
};

} // namespace deskflow::relaydesk::widgets
