/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/IncomingOfferModel.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <limits>
#include <utility>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;
using ::relaydesk::transfer::AcceptanceOrigin;
using ::relaydesk::transfer::ConflictPolicy;
using ::relaydesk::transfer::RejectReason;

qint64 systemClockMs()
{
  return QDateTime::currentMSecsSinceEpoch();
}

QString formattedBytes(quint64 bytes)
{
  if (bytes <= static_cast<quint64>(std::numeric_limits<qint64>::max()))
    return QLocale().formattedDataSize(static_cast<qint64>(bytes));
  return QLocale().toString(bytes) + QStringLiteral(" B");
}

} // namespace

IncomingOfferModel::IncomingOfferModel(
    IncomingOfferSettingsSnapshot settings, QObject *parent
)
    : IncomingOfferModel(std::move(settings), systemClockMs, parent)
{
}

IncomingOfferModel::IncomingOfferModel(
    IncomingOfferSettingsSnapshot settings, Clock clock, QObject *parent
)
    : QObject(parent),
      m_settings(std::move(settings)),
      m_clock(clock ? std::move(clock) : Clock(systemClockMs))
{
  m_expiryTimer.setSingleShot(true);
  connect(&m_expiryTimer, &QTimer::timeout, this, [this]() {
    if (!expireIfNeeded())
      scheduleExpiry();
  });
}

bool IncomingOfferModel::receiveOffer(const ::relaydesk::transfer::IncomingOffer &offer)
{
  if (m_offer.has_value() && m_offer->offer.transferId == offer.offer.transferId)
    return true;
  if (active())
    return false;

  m_expiryTimer.stop();
  m_offer.reset();
  m_status = Status::Idle;
  m_errorText.clear();
  m_dismissed = false;

  m_offer = offer;
  m_status = Status::AwaitingDecision;
  m_receivedAtMs = m_clock();
  updateSafeError();

  if (offer.peerTrusted && offer.mayAutoAccept && m_settings.autoAcceptTrustedDevices && canAccept())
    return acceptInternal(AcceptanceOrigin::TrustedDevicePolicy);

  scheduleExpiry();
  Q_EMIT changed();
  return true;
}

bool IncomingOfferModel::accept()
{
  if (expireIfNeeded())
    return false;
  return acceptInternal(AcceptanceOrigin::UserDecision);
}

bool IncomingOfferModel::acceptInternal(AcceptanceOrigin origin)
{
  if (!canAccept())
    return false;

  m_expiryTimer.stop();
  m_status = Status::Accepted;
  m_errorText.clear();
  Q_EMIT changed();
  Q_EMIT acceptRequested(
      m_offer->offer.transferId,
      {
          .destinationRoot = m_settings.destinationRoot,
          .conflictPolicy = m_settings.defaultConflictPolicy,
          .failurePartialDisposition = ::relaydesk::transfer::PartialDisposition::Keep,
          .acceptanceOrigin = origin,
      }
  );
  return true;
}

bool IncomingOfferModel::reject()
{
  if (expireIfNeeded() || !active())
    return false;

  m_expiryTimer.stop();
  m_status = Status::Rejected;
  m_errorText.clear();
  Q_EMIT changed();
  Q_EMIT rejectRequested(m_offer->offer.transferId, RejectReason::UserDeclined);
  return true;
}

bool IncomingOfferModel::expireIfNeeded()
{
  if (!active())
    return false;
  const auto timeout = std::max<qint64>(1, m_settings.decisionTimeoutMs);
  const auto elapsed = std::max<qint64>(0, m_clock() - m_receivedAtMs);
  if (elapsed < timeout)
    return false;

  m_expiryTimer.stop();
  m_status = Status::Expired;
  m_errorText = i18n::translate(Text::TransferIncomingExpired);
  Q_EMIT changed();
  Q_EMIT rejectRequested(m_offer->offer.transferId, RejectReason::PolicyDenied);
  return true;
}

void IncomingOfferModel::dismiss()
{
  if (!visible() || active())
    return;
  m_dismissed = true;
  Q_EMIT changed();
}

void IncomingOfferModel::setSettings(const IncomingOfferSettingsSnapshot &settings)
{
  if (m_settings == settings)
    return;
  m_settings = settings;
  updateSafeError();
  scheduleExpiry();
  Q_EMIT changed();
}

IncomingOfferSettingsSnapshot IncomingOfferModel::settings() const
{
  return m_settings;
}

IncomingOfferModel::Status IncomingOfferModel::status() const
{
  return m_status;
}

bool IncomingOfferModel::visible() const
{
  return m_offer.has_value() && !m_dismissed && (active() || m_status == Status::Expired || m_status == Status::Error);
}

bool IncomingOfferModel::active() const
{
  return m_status == Status::AwaitingDecision;
}

bool IncomingOfferModel::peerTrusted() const
{
  return m_offer.has_value() && m_offer->peerTrusted;
}

bool IncomingOfferModel::canAccept() const
{
  if (!active() || !peerTrusted())
    return false;
  const auto destination = m_settings.destinationRoot.trimmed();
  return !destination.isEmpty() && destination.toUtf8().size() <= ::relaydesk::transfer::kMaxControlStringUtf8Bytes &&
         m_settings.availableBytes >= m_offer->offer.totalBytes;
}

QString IncomingOfferModel::headingText() const
{
  if (!m_offer.has_value())
    return {};
  const auto peerName = m_offer->peerDisplayName.trimmed().isEmpty()
                            ? i18n::translate(Text::TransferIncomingUnknownDevice)
                            : m_offer->peerDisplayName;
  return i18n::translate(Text::TransferIncomingWantsToSend).arg(peerName);
}

QString IncomingOfferModel::offerName() const
{
  return m_offer.has_value() ? m_offer->offer.displayName : QString();
}

QString IncomingOfferModel::summaryText() const
{
  if (!m_offer.has_value())
    return {};
  const auto itemCount = m_offer->offer.fileCount + m_offer->offer.directoryCount;
  return i18n::translatePlural(Text::DevicesDropItems, static_cast<int>(itemCount)) + QStringLiteral(" · ")
         + formattedBytes(m_offer->offer.totalBytes);
}

QString IncomingOfferModel::destinationText() const
{
  return i18n::translate(Text::TransferIncomingSaveTo).arg(m_settings.destinationRoot);
}

QString IncomingOfferModel::conflictText() const
{
  return m_settings.defaultConflictPolicy == ConflictPolicy::Ask
             ? i18n::translate(Text::TransferIncomingAsk)
             : i18n::translate(Text::TransferIncomingAutoRename);
}

QString IncomingOfferModel::errorText() const
{
  return m_errorText;
}

std::optional<::relaydesk::transfer::IncomingOffer> IncomingOfferModel::offer() const
{
  return m_offer;
}

void IncomingOfferModel::scheduleExpiry()
{
  m_expiryTimer.stop();
  if (!active())
    return;
  const auto timeout = std::max<qint64>(1, m_settings.decisionTimeoutMs);
  const auto elapsed = std::max<qint64>(0, m_clock() - m_receivedAtMs);
  const auto remaining = std::max<qint64>(1, timeout - elapsed);
  m_expiryTimer.start(static_cast<int>(std::min<qint64>(remaining, std::numeric_limits<int>::max())));
}

void IncomingOfferModel::updateSafeError()
{
  if (!active() || !m_offer.has_value())
    return;
  if (!m_offer->peerTrusted) {
    m_errorText = i18n::translate(Text::TransferIncomingPairFirst);
  } else if (m_settings.destinationRoot.trimmed().isEmpty() ||
             m_settings.destinationRoot.toUtf8().size() > ::relaydesk::transfer::kMaxControlStringUtf8Bytes) {
    m_errorText = i18n::translate(Text::TransferIncomingDestinationUnavailable);
  } else if (m_settings.availableBytes < m_offer->offer.totalBytes) {
    m_errorText = i18n::translate(Text::TransferErrorDiskFull);
  } else {
    m_errorText.clear();
  }
}

} // namespace deskflow::relaydesk::model
