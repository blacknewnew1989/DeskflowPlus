/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QDialog>

class QEvent;
class QLabel;
class QPushButton;

namespace deskflow::relaydesk::widgets {

class TransferHistoryDetailsDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit TransferHistoryDetailsDialog(
      ::relaydesk::transfer::TransferHistoryRecord record, QWidget *parent = nullptr
  );

  [[nodiscard]] const ::relaydesk::transfer::TransferHistoryRecord &record() const noexcept;

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateText();

  ::relaydesk::transfer::TransferHistoryRecord m_record;
  QLabel *m_nameCaption = nullptr;
  QLabel *m_nameValue = nullptr;
  QLabel *m_peerCaption = nullptr;
  QLabel *m_peerValue = nullptr;
  QLabel *m_directionCaption = nullptr;
  QLabel *m_directionValue = nullptr;
  QLabel *m_statusCaption = nullptr;
  QLabel *m_statusValue = nullptr;
  QLabel *m_itemsCaption = nullptr;
  QLabel *m_itemsValue = nullptr;
  QLabel *m_sizeCaption = nullptr;
  QLabel *m_sizeValue = nullptr;
  QLabel *m_startedCaption = nullptr;
  QLabel *m_startedValue = nullptr;
  QLabel *m_finishedCaption = nullptr;
  QLabel *m_finishedValue = nullptr;
  QLabel *m_durationCaption = nullptr;
  QLabel *m_durationValue = nullptr;
  QLabel *m_errorCaption = nullptr;
  QLabel *m_errorValue = nullptr;
  QPushButton *m_closeButton = nullptr;
};

} // namespace deskflow::relaydesk::widgets
