/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/pairing/PairingStateMachine.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimeZone>

#include <utility>

namespace deskflow::relaydesk {
namespace {

constexpr qsizetype kFingerprintBytes = 32;
const auto kSixDigitPattern = QRegularExpression(QStringLiteral("^[0-9]{6}$"));

bool isTerminal(PairingState state)
{
  return state == PairingState::Completed || state == PairingState::Expired || state == PairingState::Rejected ||
         state == PairingState::Failed;
}

PairingActionResult failure(PairingError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

QString formatSas(quint32 value)
{
  return QStringLiteral("%1").arg(value, 6, 10, QLatin1Char('0'));
}

} // namespace

PairingStateMachine::PairingStateMachine(
    PairingOptions options, Clock clock, SasGenerator sasGenerator, QObject *parent
)
    : QObject(parent),
      m_options(options),
      m_clock(std::move(clock)),
      m_sasGenerator(std::move(sasGenerator))
{
  if (!m_clock) {
    m_clock = []() { return QDateTime::currentDateTimeUtc(); };
  }
  if (!m_sasGenerator) {
    m_sasGenerator = []() { return QRandomGenerator::system()->bounded(1'000'000U); };
  }
}

PairingActionResult PairingStateMachine::begin(
    DeviceSnapshot peer, QByteArray peerFingerprintSha256, const std::optional<QString> &receivedSas
)
{
  (void)expireIfNeeded();
  if (m_snapshot.has_value() && !isTerminal(m_snapshot->state)) {
    return failure(PairingError::ActiveSessionExists, QStringLiteral("a pairing session is already active"));
  }
  if (peerFingerprintSha256.size() != kFingerprintBytes) {
    return failure(PairingError::InvalidFingerprint, QStringLiteral("pairing peer fingerprint must be SHA-256"));
  }
  if (m_options.validity <= std::chrono::seconds::zero() || m_options.attempts <= 0) {
    return failure(PairingError::InvalidState, QStringLiteral("pairing options are invalid"));
  }

  QString sas;
  if (receivedSas.has_value()) {
    if (!kSixDigitPattern.match(*receivedSas).hasMatch()) {
      return failure(PairingError::InvalidSas, QStringLiteral("received pairing code must contain six digits"));
    }
    sas = *receivedSas;
  } else {
    const quint32 generated = m_sasGenerator();
    if (generated >= 1'000'000U) {
      return failure(PairingError::InvalidSas, QStringLiteral("pairing code generator returned an invalid value"));
    }
    sas = formatSas(generated);
  }

  const QDateTime now = m_clock().toUTC();
  if (!now.isValid()) {
    return failure(PairingError::InvalidState, QStringLiteral("pairing clock returned an invalid time"));
  }
  m_peerFingerprintSha256 = std::move(peerFingerprintSha256);
  m_snapshot = PairingSnapshot{
      .pairingSessionId = QUuid::createUuid(),
      .peer = std::move(peer),
      .state = PairingState::Requesting,
      .sixDigitSas = std::move(sas),
      .expiresAtUtc = now.addSecs(m_options.validity.count()),
      .attemptsRemaining = m_options.attempts,
  };
  publish();
  return {};
}

PairingActionResult PairingStateMachine::markTransportReady(const QUuid &sessionId)
{
  return transition(sessionId, PairingState::Requesting, PairingState::ExchangingTranscript);
}

PairingActionResult PairingStateMachine::markTranscriptExchanged(const QUuid &sessionId)
{
  return transition(sessionId, PairingState::ExchangingTranscript, PairingState::AwaitingUserComparison);
}

PairingActionResult PairingStateMachine::confirmMatchingSas(const QUuid &sessionId)
{
  return transition(sessionId, PairingState::AwaitingUserComparison, PairingState::Confirming);
}

PairingActionResult PairingStateMachine::submitDisplayedSas(const QUuid &sessionId, const QString &sixDigits)
{
  const auto prepared = prepareAction(sessionId);
  if (!prepared.ok()) {
    return prepared;
  }
  if (m_snapshot->state != PairingState::AwaitingUserComparison) {
    return failure(PairingError::InvalidState, QStringLiteral("pairing is not waiting for code comparison"));
  }
  if (!kSixDigitPattern.match(sixDigits).hasMatch()) {
    return failure(PairingError::InvalidSas, QStringLiteral("pairing code must contain six digits"));
  }

  // The low-entropy SAS is compared only in this local state machine. It is
  // never exposed as a transport key or long-term credential.
  if (sixDigits != m_snapshot->sixDigitSas) {
    --m_snapshot->attemptsRemaining;
    if (m_snapshot->attemptsRemaining == 0) {
      m_snapshot->state = PairingState::Rejected;
      m_snapshot->errorMessageKey = QStringLiteral("pairing.too_many_attempts");
      publish();
      return failure(PairingError::AttemptsExhausted, QStringLiteral("pairing code attempts were exhausted"));
    }
    publish();
    return failure(PairingError::InvalidSas, QStringLiteral("pairing code did not match"));
  }

  m_snapshot->state = PairingState::Confirming;
  publish();
  return {};
}

PairingActionResult PairingStateMachine::complete(const QUuid &sessionId)
{
  return transition(sessionId, PairingState::Confirming, PairingState::Completed);
}

PairingActionResult PairingStateMachine::cancel(const QUuid &sessionId)
{
  const auto prepared = prepareAction(sessionId);
  if (!prepared.ok()) {
    return prepared;
  }
  if (isTerminal(m_snapshot->state)) {
    return failure(PairingError::InvalidState, QStringLiteral("terminal pairing state cannot be cancelled"));
  }
  m_snapshot->state = PairingState::Rejected;
  publish();
  return {};
}

PairingActionResult PairingStateMachine::fail(const QUuid &sessionId, QString errorMessageKey)
{
  const auto prepared = prepareAction(sessionId);
  if (!prepared.ok()) {
    return prepared;
  }
  if (isTerminal(m_snapshot->state) || errorMessageKey.trimmed().isEmpty()) {
    return failure(PairingError::InvalidState, QStringLiteral("pairing failure transition is invalid"));
  }
  m_snapshot->state = PairingState::Failed;
  m_snapshot->errorMessageKey = std::move(errorMessageKey);
  publish();
  return {};
}

bool PairingStateMachine::expireIfNeeded()
{
  if (!m_snapshot.has_value() || isTerminal(m_snapshot->state) || m_clock().toUTC() < m_snapshot->expiresAtUtc) {
    return false;
  }
  m_snapshot->state = PairingState::Expired;
  m_snapshot->errorMessageKey = QStringLiteral("pairing.code.expired");
  publish();
  return true;
}

std::optional<PairingSnapshot> PairingStateMachine::snapshot() const
{
  return m_snapshot;
}

std::optional<QByteArray> PairingStateMachine::pendingFingerprint(const QUuid &sessionId) const
{
  if (!m_snapshot.has_value() || m_snapshot->pairingSessionId != sessionId ||
      m_snapshot->state != PairingState::Confirming) {
    return std::nullopt;
  }
  return m_peerFingerprintSha256;
}

std::optional<QByteArray> PairingStateMachine::confirmedFingerprint(const QUuid &sessionId) const
{
  if (!m_snapshot.has_value() || m_snapshot->pairingSessionId != sessionId ||
      m_snapshot->state != PairingState::Completed) {
    return std::nullopt;
  }
  return m_peerFingerprintSha256;
}

PairingActionResult PairingStateMachine::prepareAction(const QUuid &sessionId)
{
  if (!m_snapshot.has_value() || sessionId.isNull() || m_snapshot->pairingSessionId != sessionId) {
    return failure(PairingError::SessionNotFound, QStringLiteral("pairing session was not found"));
  }
  if (expireIfNeeded()) {
    return failure(PairingError::Expired, QStringLiteral("pairing session expired"));
  }
  return {};
}

PairingActionResult PairingStateMachine::transition(const QUuid &sessionId, PairingState required, PairingState next)
{
  const auto prepared = prepareAction(sessionId);
  if (!prepared.ok()) {
    return prepared;
  }
  if (m_snapshot->state != required) {
    return failure(PairingError::InvalidState, QStringLiteral("pairing state transition is not allowed"));
  }
  m_snapshot->state = next;
  publish();
  return {};
}

void PairingStateMachine::publish()
{
  Q_EMIT pairingChanged(*m_snapshot);
}

} // namespace deskflow::relaydesk
