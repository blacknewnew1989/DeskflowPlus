/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingStateMachine.h"

#include <QObject>
#include <QString>

#include <optional>

namespace deskflow::relaydesk::model {

class PairingWizardModel final : public QObject
{
  Q_OBJECT

  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString title READ title CONSTANT)
  Q_PROPERTY(QString peerName READ peerName NOTIFY changed)
  Q_PROPERTY(int state READ state NOTIFY changed)
  Q_PROPERTY(QString stateText READ stateText NOTIFY changed)
  Q_PROPERTY(QString sixDigitSas READ sixDigitSas NOTIFY changed)
  Q_PROPERTY(QString expiresAtText READ expiresAtText NOTIFY changed)
  Q_PROPERTY(int attemptsRemaining READ attemptsRemaining NOTIFY changed)
  Q_PROPERTY(QString attemptsRemainingText READ attemptsRemainingText NOTIFY changed)
  Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
  Q_PROPERTY(QString shortFingerprint READ shortFingerprint NOTIFY changed)
  Q_PROPERTY(QString fullFingerprint READ fullFingerprint NOTIFY changed)
  Q_PROPERTY(QString fingerprintLabel READ fingerprintLabel CONSTANT)
  Q_PROPERTY(QString codePrompt READ codePrompt CONSTANT)
  Q_PROPERTY(QString confirmActionText READ confirmActionText CONSTANT)
  Q_PROPERTY(QString submitActionText READ submitActionText CONSTANT)
  Q_PROPERTY(QString cancelActionText READ cancelActionText CONSTANT)
  Q_PROPERTY(bool canConfirmMatchingSas READ canConfirmMatchingSas NOTIFY changed)
  Q_PROPERTY(bool canSubmitCode READ canSubmitCode NOTIFY changed)
  Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
  Q_PROPERTY(bool terminal READ terminal NOTIFY changed)

public:
  explicit PairingWizardModel(PairingStateMachine &pairing, QObject *parent = nullptr);

  [[nodiscard]] bool active() const;
  [[nodiscard]] QString title() const;
  [[nodiscard]] QString peerName() const;
  [[nodiscard]] int state() const;
  [[nodiscard]] QString stateText() const;
  [[nodiscard]] QString sixDigitSas() const;
  [[nodiscard]] QString expiresAtText() const;
  [[nodiscard]] int attemptsRemaining() const;
  [[nodiscard]] QString attemptsRemainingText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QString shortFingerprint() const;
  [[nodiscard]] QString fullFingerprint() const;
  [[nodiscard]] QString fingerprintLabel() const;
  [[nodiscard]] QString codePrompt() const;
  [[nodiscard]] QString confirmActionText() const;
  [[nodiscard]] QString submitActionText() const;
  [[nodiscard]] QString cancelActionText() const;
  [[nodiscard]] bool canConfirmMatchingSas() const;
  [[nodiscard]] bool canSubmitCode() const;
  [[nodiscard]] bool canCancel() const;
  [[nodiscard]] bool terminal() const;

  [[nodiscard]] bool start(
      const DeviceSnapshot &peer, const QByteArray &peerFingerprintSha256,
      const std::optional<QString> &receivedSas = std::nullopt
  );

public Q_SLOTS:
  bool confirmMatchingSas();
  bool submitDisplayedSas(const QString &sixDigits);
  bool cancel();
  bool expireIfNeeded();

Q_SIGNALS:
  void changed();
  void actionFailed(QString message);
  void startRequested(DeviceSnapshot peer, QByteArray peerFingerprintSha256, QString receivedSas);

private Q_SLOTS:
  void pairingChanged(const PairingSnapshot &snapshot);

private:
  [[nodiscard]] static QString displayName(const DeviceSnapshot &peer);
  [[nodiscard]] static QString formatFingerprint(const QByteArray &fingerprint, bool shortened);
  [[nodiscard]] static QString translatedMessageKey(const QString &messageKey);
  [[nodiscard]] static bool isTerminal(PairingState state);
  [[nodiscard]] static QString stateText(PairingState state);
  [[nodiscard]] static QString actionErrorText(PairingError error);
  bool applyResult(const PairingActionResult &result);
  void updateSnapshot(const PairingSnapshot &snapshot);

  PairingStateMachine &m_pairing;
  std::optional<PairingSnapshot> m_snapshot;
  QByteArray m_peerFingerprint;
  QString m_actionErrorText;
};

} // namespace deskflow::relaydesk::model
