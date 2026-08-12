/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceSnapshot.h"

#include <QDockWidget>

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
} // namespace deskflow::relaydesk::model

namespace deskflow::relaydesk::widgets {

class DevicesDock final : public QDockWidget
{
  Q_OBJECT

public:
  explicit DevicesDock(model::DeviceHomeModel &devices, model::PairingWizardModel &pairing, QWidget *parent = nullptr);

  [[nodiscard]] model::DeviceHomeModel &deviceModel() const;
  [[nodiscard]] model::PairingWizardModel &pairingModel() const;

Q_SIGNALS:
  void pairingRequested(DeviceSnapshot peer);

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateText();
  void updateEmptyState();
  void updateSelection();
  void requestPairing(const QModelIndex &index);
  void updatePairingPanel();
  void submitPairingCode();

  model::DeviceHomeModel &m_devices;
  model::PairingWizardModel &m_pairing;
  QListView *m_deviceList = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_pairButton = nullptr;
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
};

} // namespace deskflow::relaydesk::widgets
