/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/discovery/DiscoverySettings.h"
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
class QDialog;
class QDropEvent;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QModelIndex;
class QPushButton;
class QToolButton;
class QAction;
class QWidget;

namespace deskflow::relaydesk::model {
class DeviceHomeModel;
class IncomingOfferModel;
class PairingWizardModel;
class PermissionStatusModel;
} // namespace deskflow::relaydesk::model

namespace deskflow::relaydesk::widgets {

class DevicesDock final : public QDockWidget
{
  Q_OBJECT

public:
  using ManualAddressesSaveReceipt = std::function<void(bool success)>;

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
  void setIncomingOfferModel(model::IncomingOfferModel *incomingOffers);
  void showIncomingConflictPrompt(::relaydesk::transfer::IncomingConflictPrompt prompt);
  void showIncomingConflictCancelTransportFailure(const ::relaydesk::transfer::TransferId &transferId);
  void clearIncomingConflictPrompts(const ::relaydesk::transfer::TransferId &transferId);
  void setManualAddresses(QList<ManualAddress> addresses);

Q_SIGNALS:
  void pairingRequested(DeviceId peerDeviceId);
  void trustRevocationRequested(DeviceId peerDeviceId);
  void sendItemsRequested(DeviceId peerDeviceId, QList<QUrl> localItems, ::relaydesk::transfer::SendOptions options);
  void sendItemsRejected(QString message);
  void incomingOfferSettingsRequested();
  void incomingConflictDecisionRequested(
      ::relaydesk::transfer::TransferId transferId, QUuid conflictId,
      ::relaydesk::transfer::IncomingConflictDecision decision
  );
  void manualAddressesSaveRequested(QList<ManualAddress> addresses, ManualAddressesSaveReceipt receipt);

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
  void activateDevice(const QModelIndex &index);
  void requestPairing(const QModelIndex &index);
  void requestTrustRevocation(const QModelIndex &index);
  void manageManualAddresses();
  void updatePairingPanel();
  void updatePermissionBanner();
  void updatePermissionDetails();
  void updateIncomingOfferPanel();
  void updateIncomingConflictPanel();
  void updateActivityPanel();
  void resolveIncomingConflict(::relaydesk::transfer::IncomingConflictDecision decision);
  void submitPairingCode();
  void chooseAndSend(bool folder);
  [[nodiscard]] QModelIndex targetIndexAt(const QPoint &position) const;
  [[nodiscard]] std::optional<DeviceId> sendTarget(const QModelIndex &index) const;
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
  QToolButton *m_permissionDetailsToggle = nullptr;
  QDialog *m_permissionDetailsPanel = nullptr;
  QList<QLabel *> m_permissionDetailTitles;
  QList<QLabel *> m_permissionDetailPurposes;
  QList<QLabel *> m_permissionDetailStatuses;
  QList<QLabel *> m_permissionDetailCapabilities;
  QList<QPushButton *> m_permissionDetailSettingsButtons;
  QListView *m_deviceList = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_pairButton = nullptr;
  QPushButton *m_manageManualAddressesButton = nullptr;
  QToolButton *m_moreButton = nullptr;
  QMenu *m_moreMenu = nullptr;
  QAction *m_revokeTrustAction = nullptr;
  QPushButton *m_sendFilesButton = nullptr;
  QPushButton *m_sendFolderButton = nullptr;
  QLabel *m_sendFeedback = nullptr;
  QWidget *m_activityRegion = nullptr;
  QFrame *m_incomingOfferPanel = nullptr;
  QLabel *m_incomingOfferHeading = nullptr;
  QLabel *m_incomingOfferName = nullptr;
  QLabel *m_incomingOfferSummary = nullptr;
  QLabel *m_incomingOfferDestination = nullptr;
  QLabel *m_incomingOfferConflict = nullptr;
  QLabel *m_incomingOfferError = nullptr;
  QPushButton *m_acceptIncomingOfferButton = nullptr;
  QPushButton *m_rejectIncomingOfferButton = nullptr;
  QPushButton *m_changeIncomingOfferSettingsButton = nullptr;
  QPushButton *m_dismissIncomingOfferButton = nullptr;
  QFrame *m_incomingConflictPanel = nullptr;
  QLabel *m_incomingConflictTitle = nullptr;
  QLabel *m_incomingConflictPath = nullptr;
  QLabel *m_incomingConflictError = nullptr;
  QPushButton *m_overwriteIncomingConflictButton = nullptr;
  QPushButton *m_autoRenameIncomingConflictButton = nullptr;
  QPushButton *m_skipIncomingConflictButton = nullptr;
  QPushButton *m_cancelIncomingConflictButton = nullptr;
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
  model::IncomingOfferModel *m_incomingOffers = nullptr;
  QList<::relaydesk::transfer::IncomingConflictPrompt> m_incomingConflictPrompts;
  QString m_incomingConflictErrorText;
  QList<ManualAddress> m_manualAddresses;
};

} // namespace deskflow::relaydesk::widgets
