/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QFrame>
#include <QString>

class QEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QProgressBar;
class QPushButton;

namespace deskflow::relaydesk::model {
class TransferCenterModel;
}

namespace deskflow::relaydesk::widgets {

class TransferMiniBar final : public QFrame
{
  Q_OBJECT

public:
  explicit TransferMiniBar(model::TransferCenterModel &transfers, QWidget *parent = nullptr);

Q_SIGNALS:
  void detailsRequested();

protected:
  void changeEvent(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  enum class PrimaryAction
  {
    None,
    Pause,
    Resume,
    Retry,
  };

  void refresh();
  void triggerPrimaryAction();

  model::TransferCenterModel &m_transfers;
  QLabel *m_title = nullptr;
  QLabel *m_metrics = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_primaryAction = nullptr;
  QString m_transferId;
  PrimaryAction m_action = PrimaryAction::None;
};

} // namespace deskflow::relaydesk::widgets
