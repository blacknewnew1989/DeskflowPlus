/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Symless Ltd.
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once
#include <QDialog>

#include "gui/config/IServerConfig.h"
#include "gui/core/CoreProcess.h"
#include "relaydesk/transfer/TransferSettings.h"

namespace Ui {
class SettingsDialog;
}
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

class SettingsDialog : public QDialog
{
  using IServerConfig = deskflow::gui::IServerConfig;
  using CoreProcess = deskflow::gui::CoreProcess;

  Q_OBJECT

public:
  void extracted();
  SettingsDialog(QWidget *parent, const IServerConfig &serverConfig, const CoreProcess &coreProcess);
  ~SettingsDialog() override;
  void focusFileTransferSettings();

Q_SIGNALS:
  void shown();
  void transferSettingsSaved(::relaydesk::transfer::TransferSettings settings);

protected:
  void changeEvent(QEvent *e) override;

private:
  void initConnections() const;
  void regenCertificates();
  void browseCertificatePath();
  void browseLogPath();
  void browseReceiveFolder();
  void setLogToFile(bool logToFile);
  bool saveStartAtLogin();
  void loadStartAtLogin();
  void accept() override;
  void showEvent(QShowEvent *event) override;
  bool isClientMode() const;
  void updateTlsControls();
  void updateTlsControlsEnabled();
  void updateInputRoleControls();
  void showReadOnlyMessage();
  void updateText();
  void resizeToContents();
  [[nodiscard]] ::relaydesk::transfer::TransferSettings transferSettingsFromControls() const;
  bool saveTransferSettings();

  /// @brief Load all settings.
  void loadFromConfig();

  /// @brief Updates the key length value based on the loaded file.
  void updateKeyLengthOnFile(const QString &path);

  /// @brief Enables controls when they should be.
  void updateControls();

  /// @brief updates the setting vaule for key size.
  void updateRequestedKeySize() const;

  /// @brief update if the log level warning is shown
  void logLevelChanged();

  std::unique_ptr<Ui::SettingsDialog> ui;
  const IServerConfig &m_serverConfig;
  const CoreProcess &m_coreProcess;
  bool m_startAtLoginAvailable = false;
  bool m_fileTransferOnly = false;
  QGroupBox *m_fileTransferGroup = nullptr;
  QLabel *m_receiveFolderLabel = nullptr;
  QLineEdit *m_receiveFolder = nullptr;
  QPushButton *m_browseReceiveFolderButton = nullptr;
  QLabel *m_incomingPolicyLabel = nullptr;
  QComboBox *m_incomingPolicy = nullptr;
  QLabel *m_conflictPolicyLabel = nullptr;
  QComboBox *m_conflictPolicy = nullptr;
};
