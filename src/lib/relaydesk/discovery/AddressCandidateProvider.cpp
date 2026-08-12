/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/AddressCandidateProvider.h"

#include <QAbstractSocket>
#include <QHostInfo>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace deskflow::relaydesk {

struct AddressCandidateProvider::ResolutionState
{
  QList<AddressCandidate> priorityCandidates;
  QList<QList<AddressCandidate>> manualCandidates;
  QList<QString> unresolvedByManualIndex;
  QList<QString> diagnosticByManualIndex;
  qsizetype pendingHosts = 0;
  bool completionScheduled = false;
};

namespace {
bool isUsableAddress(const QHostAddress &address)
{
  return !address.isNull() && !address.isMulticast() && !address.isBroadcast() &&
         address != QHostAddress::AnyIPv4 && address != QHostAddress::AnyIPv6 &&
         address.protocol() != QAbstractSocket::UnknownNetworkLayerProtocol;
}

QString candidateKey(const AddressCandidate &candidate)
{
  return QStringLiteral("%1|%2|%3")
      .arg(candidate.address.toString())
      .arg(candidate.inputPort)
      .arg(candidate.filePort);
}

void appendCandidate(QList<AddressCandidate> &candidates, QSet<QString> &seen, AddressCandidate candidate)
{
  if (!isUsableAddress(candidate.address)) {
    return;
  }
  const auto key = candidateKey(candidate);
  if (!seen.contains(key)) {
    seen.insert(key);
    candidates.append(std::move(candidate));
  }
}

QList<QHostAddress> normalizedResolvedAddresses(QList<QHostAddress> addresses)
{
  QList<QHostAddress> result;
  for (const auto &address : std::as_const(addresses)) {
    if (isUsableAddress(address) && !result.contains(address)) {
      result.append(address);
    }
  }
  std::sort(result.begin(), result.end(), [](const QHostAddress &left, const QHostAddress &right) {
    const auto protocolRank = [](const QHostAddress &address) {
      return address.protocol() == QAbstractSocket::IPv4Protocol ? 0 : 1;
    };
    const auto leftRank = protocolRank(left);
    const auto rightRank = protocolRank(right);
    return leftRank != rightRank ? leftRank < rightRank : left.toString() < right.toString();
  });
  return result;
}
} // namespace

AddressCandidateProvider::AddressCandidateProvider(HostResolver resolver, QObject *parent)
    : QObject(parent), m_resolver(std::move(resolver))
{
  if (!m_resolver) {
    m_resolver = [this](const QString &host, HostResolutionCallback callback) {
      QHostInfo::lookupHost(host, this, [callback = std::move(callback)](const QHostInfo &info) mutable {
        if (info.error() != QHostInfo::NoError) {
          callback({}, info.errorString());
          return;
        }
        callback(info.addresses(), {});
      });
    };
  }
}

void AddressCandidateProvider::resolveCandidates(AddressCandidateRequest request)
{
  const auto generation = ++m_generation;
  if (request.inputPort == 0 || request.filePort == 0) {
    QTimer::singleShot(0, this, [this, generation]() {
      if (generation == m_generation) {
        Q_EMIT resolutionFailed(QStringLiteral("Candidate ports must be in the range 1..65535"));
      }
    });
    return;
  }

  auto state = std::make_shared<ResolutionState>();
  state->manualCandidates.resize(request.settings.manualAddresses.size());
  state->unresolvedByManualIndex.resize(request.settings.manualAddresses.size());
  state->diagnosticByManualIndex.resize(request.settings.manualAddresses.size());
  QSet<QString> prioritySeen;
  for (const auto &address : std::as_const(request.recentSuccessfulAddresses)) {
    appendCandidate(
        state->priorityCandidates, prioritySeen,
        AddressCandidate{
            .address = address,
            .inputPort = request.inputPort,
            .filePort = request.filePort,
            .source = AddressCandidateSource::RecentSuccessful,
        }
    );
  }
  if (request.settings.enabled) {
    for (const auto &address : std::as_const(request.discoveredAddresses)) {
      appendCandidate(
          state->priorityCandidates, prioritySeen,
          AddressCandidate{
              .address = address,
              .inputPort = request.inputPort,
              .filePort = request.filePort,
              .source = AddressCandidateSource::Discovered,
          }
      );
    }
  }

  struct PendingHost
  {
    qsizetype index = 0;
    ManualAddress address;
  };
  QList<PendingHost> pending;
  for (qsizetype index = 0; index < request.settings.manualAddresses.size(); ++index) {
    const auto &entry = request.settings.manualAddresses.at(index);
    QString parseDiagnostic;
    const auto normalized = parseManualAddress(entry.host, entry.inputPort, entry.filePort, &parseDiagnostic);
    if (!normalized.has_value()) {
      state->unresolvedByManualIndex[index] = entry.host;
      state->diagnosticByManualIndex[index] = QStringLiteral("%1: %2").arg(entry.host, parseDiagnostic);
      continue;
    }

    QHostAddress literal;
    if (literal.setAddress(normalized->host)) {
      state->manualCandidates[index].append({
          .address = literal,
          .inputPort = normalized->inputPort,
          .filePort = normalized->filePort,
          .source = AddressCandidateSource::Manual,
          .originHost = normalized->host,
      });
      continue;
    }
    pending.append({.index = index, .address = *normalized});
  }

  state->pendingHosts = pending.size();
  if (pending.isEmpty()) {
    scheduleCompletion(generation, state);
    return;
  }
  for (const auto &host : std::as_const(pending)) {
    resolveHost(generation, state, host.index, host.address);
  }
}

void AddressCandidateProvider::resolveHost(
    quint64 generation, const std::shared_ptr<ResolutionState> &state, qsizetype manualIndex,
    const ManualAddress &manualAddress
)
{
  QPointer<AddressCandidateProvider> guard(this);
  m_resolver(
      manualAddress.host,
      [guard, generation, state, manualIndex, manualAddress](QList<QHostAddress> addresses, QString diagnostic) {
        if (guard != nullptr) {
          guard->finishHost(
              generation, state, manualIndex, manualAddress, std::move(addresses), std::move(diagnostic)
          );
        }
      }
  );
}

void AddressCandidateProvider::finishHost(
    quint64 generation, const std::shared_ptr<ResolutionState> &state, qsizetype manualIndex,
    const ManualAddress &manualAddress, QList<QHostAddress> addresses, QString diagnostic
)
{
  if (generation != m_generation || state->pendingHosts <= 0) {
    return;
  }

  const auto normalizedAddresses = normalizedResolvedAddresses(std::move(addresses));
  if (normalizedAddresses.isEmpty()) {
    state->unresolvedByManualIndex[manualIndex] = manualAddress.host;
    state->diagnosticByManualIndex[manualIndex] =
        QStringLiteral("%1: %2")
            .arg(
                manualAddress.host,
                diagnostic.isEmpty() ? QStringLiteral("hostname resolved to no usable addresses") : diagnostic
            );
  } else {
    for (const auto &address : normalizedAddresses) {
      state->manualCandidates[manualIndex].append({
          .address = address,
          .inputPort = manualAddress.inputPort,
          .filePort = manualAddress.filePort,
          .source = AddressCandidateSource::Manual,
          .originHost = manualAddress.host,
      });
    }
  }

  --state->pendingHosts;
  if (state->pendingHosts == 0) {
    scheduleCompletion(generation, state);
  }
}

void AddressCandidateProvider::scheduleCompletion(
    quint64 generation, const std::shared_ptr<ResolutionState> &state
)
{
  if (state->completionScheduled) {
    return;
  }
  state->completionScheduled = true;
  QTimer::singleShot(0, this, [this, generation, state]() {
    if (generation != m_generation) {
      return;
    }
    AddressCandidateResult result{
        .candidates = state->priorityCandidates,
    };
    for (qsizetype index = 0; index < state->unresolvedByManualIndex.size(); ++index) {
      if (!state->unresolvedByManualIndex.at(index).isEmpty()) {
        result.unresolvedHosts.append(state->unresolvedByManualIndex.at(index));
        result.diagnostics.append(state->diagnosticByManualIndex.at(index));
      }
    }
    QSet<QString> seen;
    for (const auto &candidate : std::as_const(result.candidates)) {
      seen.insert(candidateKey(candidate));
    }
    for (const auto &manualGroup : std::as_const(state->manualCandidates)) {
      for (const auto &candidate : manualGroup) {
        appendCandidate(result.candidates, seen, candidate);
      }
    }
    Q_EMIT candidatesResolved(std::move(result));
  });
}

} // namespace deskflow::relaydesk
