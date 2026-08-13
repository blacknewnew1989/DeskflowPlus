/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferTypes.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

namespace deskflow::relaydesk::model {

struct IncomingOfferSettingsSnapshot
{
  QString destinationRoot;
  quint64 availableBytes = 0;
  bool autoAcceptTrustedDevices = false;
  qint64 decisionTimeoutMs = 120'000;

  [[nodiscard]] bool operator==(const IncomingOfferSettingsSnapshot &) const = default;
};

class IncomingOfferModel final : public QObject
{
  Q_OBJECT

public:
  enum class Status
  {
    Idle,
    AwaitingDecision,
    Accepted,
    Rejected,
    Expired,
    Error,
  };
  Q_ENUM(Status)

  using Clock = std::function<qint64()>;

  explicit IncomingOfferModel(
      IncomingOfferSettingsSnapshot settings, QObject *parent = nullptr
  );
  IncomingOfferModel(
      IncomingOfferSettingsSnapshot settings, Clock clock, QObject *parent = nullptr
  );

  bool receiveOffer(const ::relaydesk::transfer::IncomingOffer &offer);
  bool accept();
  bool reject();
  bool expireIfNeeded();
  void dismiss();
  void setSettings(const IncomingOfferSettingsSnapshot &settings);

  [[nodiscard]] IncomingOfferSettingsSnapshot settings() const;
  [[nodiscard]] Status status() const;
  [[nodiscard]] bool visible() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool peerTrusted() const;
  [[nodiscard]] bool canAccept() const;
  [[nodiscard]] QString headingText() const;
  [[nodiscard]] QString offerName() const;
  [[nodiscard]] QString summaryText() const;
  [[nodiscard]] QString destinationText() const;
  [[nodiscard]] QString conflictText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] std::optional<::relaydesk::transfer::IncomingOffer> offer() const;
Q_SIGNALS:
  void changed();
  void acceptRequested(
      ::relaydesk::transfer::TransferId transferId,
      ::relaydesk::transfer::ReceiveOptions options
  );
  void rejectRequested(
      ::relaydesk::transfer::TransferId transferId,
      ::relaydesk::transfer::RejectReason reason
  );

private:
  [[nodiscard]] bool acceptInternal(::relaydesk::transfer::AcceptanceOrigin origin);
  void scheduleExpiry();
  void updateSafeError();

  IncomingOfferSettingsSnapshot m_settings;
  Clock m_clock;
  QTimer m_expiryTimer;
  std::optional<::relaydesk::transfer::IncomingOffer> m_offer;
  Status m_status = Status::Idle;
  QString m_errorText;
  qint64 m_receivedAtMs = 0;
  bool m_dismissed = false;
};

} // namespace deskflow::relaydesk::model
