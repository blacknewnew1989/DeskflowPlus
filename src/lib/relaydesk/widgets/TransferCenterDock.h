/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDockWidget>

class QAction;
class QEvent;
class QLabel;
class QListView;
class QMenu;
class QModelIndex;
class QPushButton;
class QToolButton;

namespace deskflow::relaydesk::model {
class TransferCenterModel;
}

namespace deskflow::relaydesk::widgets {

class TransferCenterDock final : public QDockWidget
{
  Q_OBJECT

public:
  explicit TransferCenterDock(model::TransferCenterModel &transfers, QWidget *parent = nullptr);

  [[nodiscard]] model::TransferCenterModel &transferModel() const;

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateText();
  void updateEmptyState();
  void updateSelection();
  void showHistoryDetails();

  model::TransferCenterModel &m_transfers;
  QListView *m_list = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_detailsButton = nullptr;
  QPushButton *m_openFolderButton = nullptr;
  QPushButton *m_openFileButton = nullptr;
  QPushButton *m_retryButton = nullptr;
  QPushButton *m_pauseButton = nullptr;
  QPushButton *m_resumeButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QToolButton *m_moreButton = nullptr;
  QMenu *m_moreMenu = nullptr;
  QAction *m_detailsMenuAction = nullptr;
  QAction *m_openFolderMenuAction = nullptr;
  QAction *m_openFileMenuAction = nullptr;
  QAction *m_retryMenuAction = nullptr;
  QAction *m_pauseMenuAction = nullptr;
  QAction *m_resumeMenuAction = nullptr;
  QAction *m_cancelMenuAction = nullptr;
};

} // namespace deskflow::relaydesk::widgets
