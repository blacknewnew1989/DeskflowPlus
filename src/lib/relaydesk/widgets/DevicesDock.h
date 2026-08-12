/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/transfer/TransferTypes.h"

#include <QDockWidget>
#include <QList>
#include <QPersistentModelIndex>
#include <QUrl>

#include <functional>
#include <optional>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QModelIndex;
class QPushButton;
class QToolButton;

namespace deskflow::relaydesk::model {
class DeviceHomeModel;
class PairingWizardModel;
class PermissionStatusModel;
} // namespace deskflow::relaydesk::model

namespace deskflow::relaydesk::widgets {

class DevicesDock final : public QDockWidget
{
  Q_OBJECT

public:
  using ItemChooser = std::function<QList<QUrl>(QWidget &parent)>;

  explicit DevicesDock(
      model::DeviceHomeModel &devices, model::PairingWizardModel &pairing, model::PermissionStatusModel &permissions,
      QWidget *parent = nullptr
  );

  [[nodiscard]] model::DeviceHomeModel &deviceModel() const;
  [[nodiscard]] model::PairingWizardModel &pairingModel() const;
  [[nodiscard]] model::PermissionStatusModel &permissionModel() const;
  void setFileChooser(ItemChooser chooser);
  void setFolderChooser(ItemChooser chooser);

Q_SIGNALS:
  void pairingRequested(DeviceSnapshot peer);
  void sendItemsRequested(
      DeviceSnapshot peer, QList<QUrl> localItems, ::relaydesk::transfer::SendOptions options
  );
  void sendItemsRejected(QString message);

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void updateText();
  void updateEmptyState();
  void updateSelection();
  void requestPairing(const QModelIndex &index);
  void updatePairingPanel();
  void updatePermissionBanner();
  void submitPairingCode();
  void chooseAndSend(bool folder);
  [[nodiscard]] QModelIndex targetIndexAt(const QPoint &position) const;
  [[nodiscard]] std::optional<DeviceSnapshot> sendTarget(const QModelIndex &index) const;
  [[nodiscard]] QString validateLocalItems(const QList<QUrl> &items) const;
  [[nodiscard]] bool updateDropTarget(const QModelIndex &index, const QList<QUrl> &items);
  void clearDropTarget();
  void showSendFeedback(const QString &message);
  [[nodiscard]] bool publishSendIntent(const QModelIndex &index, const QList<QUrl> &items);

  model::DeviceHomeModel &m_devices;
  model::PairingWizardModel &m_pairing;
  model::PermissionStatusModel &m_permissions;
  QFrame *m_permissionBanner = nullptr;
  QLabel *m_permissionTitle = nullptr;
  QLabel *m_permissionMessage = nullptr;
  QPushButton *m_openPermissionSettingsButton = nullptr;
  QListView *m_deviceList = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_pairButton = nullptr;
  QPushButton *m_sendFilesButton = nullptr;
  QPushButton *m_sendFolderButton = nullptr;
  QLabel *m_sendFeedback = nullptr;
  QFrame *m_pairingPanel = nullptr;
  QLabel *m_pairingPeer = nullptr;
  QLabel *m_pairingState = nullptr;
  QLabel *m_pairingCode = nullptr;
  QLabel *m_pairingExpiry = nullptr;
  QLabel *m_pairingAttempts = nullptr;
  QLabel *m_pairingError = nullptr;
  QLineEdit *m_pairingCodeEntry = nullptr;
  QPushButton *m_confirmCodeButton = nullptr;
  QPushButton *m_submitCodeButton = nullptr;
  QPushButton *m_cancelPairingButton = nullptr;
  QToolButton *m_fingerprintToggle = nullptr;
  QLabel *m_fingerprintSummary = nullptr;
  QLabel *m_fingerprintDetails = nullptr;
  ItemChooser m_fileChooser;
  ItemChooser m_folderChooser;
  QPersistentModelIndex m_dropTargetIndex;
};

} // namespace deskflow::relaydesk::widgets
