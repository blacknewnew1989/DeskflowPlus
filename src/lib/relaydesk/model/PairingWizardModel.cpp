/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/model/PairingWizardModel.h"

#include "relaydesk/i18n/ProductStrings.h"
#include <QLocale>
#include <QRegularExpression>

namespace deskflow::relaydesk::model {
namespace {

using i18n::Text;

const auto kSixDigitPattern = QRegularExpression(QStringLiteral("^[0-9]{6}$"));

} // namespace

PairingWizardModel::PairingWizardModel(QObject *parent) : QObject(parent)
{
}

PairingWizardModel::PairingWizardModel(IPairingService &service, QObject *parent) : QObject(parent)
{
  bindService(service);
}

void PairingWizardModel::bindService(IPairingService &service)
{
  if (m_service != nullptr) {
    disconnect(m_service, &IPairingService::pairingChanged, this, &PairingWizardModel::pairingChanged);
  }
  m_service = &service;
  m_snapshot = service.snapshot();
  m_peerFingerprint.clear();
  if (m_snapshot.has_value()) {
    m_peerFingerprint = service.pendingFingerprint(m_snapshot->pairingSessionId).value_or(QByteArray{});
  }
  connect(m_service, &IPairingService::pairingChanged, this, &PairingWizardModel::pairingChanged);
  Q_EMIT changed();
}

bool PairingWizardModel::active() const
{
  return m_snapshot.has_value();
}

QString PairingWizardModel::title() const
{
  return i18n::translate(Text::PairingTitle);
}

QString PairingWizardModel::peerName() const
{
  return m_snapshot.has_value() ? displayName(m_snapshot->peer) : QString();
}

int PairingWizardModel::state() const
{
  return m_snapshot.has_value() ? static_cast<int>(m_snapshot->state) : static_cast<int>(PairingState::Idle);
}

QString PairingWizardModel::stateText() const
{
  return PairingWizardModel::stateText(m_snapshot.has_value() ? m_snapshot->state : PairingState::Idle);
}

QString PairingWizardModel::sixDigitSas() const
{
  if (!m_snapshot.has_value() || m_snapshot->state != PairingState::AwaitingUserComparison)
    return {};
  return m_snapshot->sixDigitSas;
}

QString PairingWizardModel::expiresAtText() const
{
  if (!m_snapshot.has_value() || !m_snapshot->expiresAtUtc.isValid())
    return {};
  return QLocale().toString(m_snapshot->expiresAtUtc.toLocalTime(), QLocale::ShortFormat);
}

int PairingWizardModel::attemptsRemaining() const
{
  return m_snapshot.has_value() ? m_snapshot->attemptsRemaining : 0;
}

QString PairingWizardModel::attemptsRemainingText() const
{
  return m_snapshot.has_value() ? i18n::translatePlural(Text::PairingAttemptsRemaining, attemptsRemaining())
                                : QString();
}

QString PairingWizardModel::errorText() const
{
  if (m_actionError != ActionError::None)
    return actionErrorText(m_actionError);
  return m_snapshot.has_value() ? failureText(m_snapshot->failureReason) : QString();
}

QString PairingWizardModel::shortFingerprint() const
{
  return formatFingerprint(m_peerFingerprint, true);
}

QString PairingWizardModel::fullFingerprint() const
{
  return formatFingerprint(m_peerFingerprint, false);
}

QString PairingWizardModel::fingerprintLabel() const
{
  return i18n::translate(Text::PairingFingerprintLabel);
}

QString PairingWizardModel::codePrompt() const
{
  return i18n::translate(Text::PairingCodePrompt);
}

QString PairingWizardModel::confirmActionText() const
{
  return i18n::translate(Text::PairingActionCodesMatch);
}

QString PairingWizardModel::submitActionText() const
{
  return i18n::translate(Text::PairingActionSubmitCode);
}

QString PairingWizardModel::cancelActionText() const
{
  return i18n::translate(Text::PairingActionCancel);
}

bool PairingWizardModel::canConfirmMatchingSas() const
{
  return m_snapshot.has_value() && m_snapshot->state == PairingState::AwaitingUserComparison;
}

bool PairingWizardModel::canSubmitCode() const
{
  return canConfirmMatchingSas();
}

bool PairingWizardModel::canCancel() const
{
  return m_snapshot.has_value() && !isTerminal(m_snapshot->state);
}

bool PairingWizardModel::terminal() const
{
  return m_snapshot.has_value() && isTerminal(m_snapshot->state);
}

bool PairingWizardModel::confirmMatchingSas()
{
  if (!m_snapshot.has_value())
    return applyResult({.error = PairingError::SessionNotFound});
  return m_service != nullptr ? applyResult(m_service->confirmMatchingSas(m_snapshot->pairingSessionId))
                              : applyResult({.error = PairingError::InvalidState});
}

bool PairingWizardModel::submitDisplayedSas(const QString &sixDigits)
{
  if (!kSixDigitPattern.match(sixDigits).hasMatch()) {
    m_actionError = ActionError::CodeInvalid;
    const auto message = errorText();
    Q_EMIT changed();
    Q_EMIT actionFailed(message);
    return false;
  }
  if (!m_snapshot.has_value())
    return applyResult({.error = PairingError::SessionNotFound});
  return m_service != nullptr
             ? applyResult(m_service->submitDisplayedSas(m_snapshot->pairingSessionId, sixDigits))
             : applyResult({.error = PairingError::InvalidState});
}

bool PairingWizardModel::cancel()
{
  if (!m_snapshot.has_value())
    return applyResult({.error = PairingError::SessionNotFound});
  return m_service != nullptr ? applyResult(m_service->cancel(m_snapshot->pairingSessionId))
                              : applyResult({.error = PairingError::InvalidState});
}

void PairingWizardModel::pairingChanged(const PairingSnapshot &snapshot)
{
  m_actionError = ActionError::None;
  m_peerFingerprint = m_service != nullptr
                          ? m_service->pendingFingerprint(snapshot.pairingSessionId).value_or(QByteArray{})
                          : QByteArray{};
  updateSnapshot(snapshot);
}

QString PairingWizardModel::displayName(const DeviceSnapshot &peer)
{
  return peer.alias.trimmed().isEmpty() ? peer.displayName : peer.alias;
}

QString PairingWizardModel::formatFingerprint(const QByteArray &fingerprint, bool shortened)
{
  if (fingerprint.size() != 32)
    return i18n::translate(Text::PairingFingerprintUnavailable);

  const auto upperHex = fingerprint.toHex(':').toUpper();
  if (!shortened)
    return QString::fromLatin1(upperHex);

  const auto components = upperHex.split(':');
  return QString::fromLatin1(components.sliced(0, 4).join(':')) + QStringLiteral(" … ") +
         QString::fromLatin1(components.sliced(28, 4).join(':'));
}

QString PairingWizardModel::failureText(PairingFailureReason reason)
{
  switch (reason) {
  case PairingFailureReason::None:
    return {};
  case PairingFailureReason::Cancelled:
    return i18n::translate(Text::PairingStateRejected);
  case PairingFailureReason::CodeMismatch:
    return i18n::translate(Text::PairingCodeMismatch);
  case PairingFailureReason::Expired:
    return i18n::translate(Text::PairingCodeExpired);
  case PairingFailureReason::TooManyAttempts:
    return i18n::translate(Text::PairingTooManyAttempts);
  case PairingFailureReason::CertificateChanged:
    return i18n::translate(Text::PairingCertificateChanged);
  case PairingFailureReason::DirectConnectionRequired:
    return i18n::translate(Text::PairingNotDirect);
  case PairingFailureReason::TransportFailed:
  case PairingFailureReason::TrustStoreWriteFailed:
    return i18n::translate(Text::PairingStateFailed);
  }
  return i18n::translate(Text::PairingStateFailed);
}

bool PairingWizardModel::isTerminal(PairingState state)
{
  return state == PairingState::Completed || state == PairingState::Expired || state == PairingState::Rejected ||
         state == PairingState::Failed;
}

QString PairingWizardModel::stateText(PairingState state)
{
  switch (state) {
  case PairingState::Idle:
    return i18n::translate(Text::PairingStateReady);
  case PairingState::Requesting:
    return i18n::translate(Text::PairingStateRequesting);
  case PairingState::ExchangingTranscript:
    return i18n::translate(Text::PairingStateSecuring);
  case PairingState::AwaitingUserComparison:
    return i18n::translate(Text::PairingStateCompare);
  case PairingState::Confirming:
    return i18n::translate(Text::PairingStateConfirming);
  case PairingState::Completed:
    return i18n::translate(Text::PairingSuccess);
  case PairingState::Expired:
    return i18n::translate(Text::PairingCodeExpired);
  case PairingState::Rejected:
    return i18n::translate(Text::PairingStateRejected);
  case PairingState::Failed:
    return i18n::translate(Text::PairingStateFailed);
  }
  return i18n::translate(Text::PairingStateFailed);
}

PairingWizardModel::ActionError PairingWizardModel::actionError(PairingError error)
{
  switch (error) {
  case PairingError::None:
    return ActionError::None;
  case PairingError::ActiveSessionExists:
    return ActionError::ActiveSessionExists;
  case PairingError::InvalidFingerprint:
    return ActionError::IdentityNotReady;
  case PairingError::InvalidSas:
    return ActionError::CodeMismatch;
  case PairingError::SessionNotFound:
    return ActionError::SessionUnavailable;
  case PairingError::InvalidState:
    return ActionError::ActionUnavailable;
  case PairingError::Expired:
    return ActionError::CodeExpired;
  case PairingError::AttemptsExhausted:
    return ActionError::TooManyAttempts;
  }
  return ActionError::PairingFailed;
}

QString PairingWizardModel::actionErrorText(ActionError error)
{
  switch (error) {
  case ActionError::None:
    return {};
  case ActionError::CodeInvalid:
    return i18n::translate(Text::PairingCodeInvalid);
  case ActionError::ActiveSessionExists:
    return i18n::translate(Text::PairingAlreadyActive);
  case ActionError::IdentityNotReady:
    return i18n::translate(Text::PairingIdentityNotReady);
  case ActionError::CodeMismatch:
    return i18n::translate(Text::PairingCodeMismatch);
  case ActionError::SessionUnavailable:
    return i18n::translate(Text::PairingSessionUnavailable);
  case ActionError::ActionUnavailable:
    return i18n::translate(Text::PairingActionUnavailable);
  case ActionError::CodeExpired:
    return i18n::translate(Text::PairingCodeExpired);
  case ActionError::TooManyAttempts:
    return i18n::translate(Text::PairingTooManyAttempts);
  case ActionError::PairingFailed:
    return i18n::translate(Text::PairingStateFailed);
  }
  return i18n::translate(Text::PairingStateFailed);
}

bool PairingWizardModel::applyResult(const PairingActionResult &result)
{
  if (result.ok()) {
    m_actionError = ActionError::None;
    return true;
  }

  m_actionError = actionError(result.error);
  const auto message = errorText();
  Q_EMIT changed();
  Q_EMIT actionFailed(message);
  return false;
}

bool PairingWizardModel::applyResult(const PairingOperationResult &result)
{
  if (result.ok()) {
    m_actionError = ActionError::None;
    return true;
  }
  if (result.stateError != PairingError::None) {
    return applyResult({.error = result.stateError, .diagnostic = result.diagnostic});
  }

  PairingError error = PairingError::InvalidState;
  switch (result.error) {
  case PairingOperationError::None:
    error = PairingError::None;
    break;
  case PairingOperationError::ActiveSessionExists:
    error = PairingError::ActiveSessionExists;
    break;
  case PairingOperationError::SessionNotFound:
  case PairingOperationError::SessionMismatch:
    error = PairingError::SessionNotFound;
    break;
  case PairingOperationError::Expired:
    error = PairingError::Expired;
    break;
  case PairingOperationError::InvalidCode:
    error = PairingError::InvalidSas;
    break;
  default:
    break;
  }
  return applyResult({.error = error, .diagnostic = result.diagnostic});
}

void PairingWizardModel::updateSnapshot(const PairingSnapshot &snapshot)
{
  m_snapshot = snapshot;
  Q_EMIT changed();
}

} // namespace deskflow::relaydesk::model
