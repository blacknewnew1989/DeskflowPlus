/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/discovery/DiscoverySettings.h"

#include <QHostAddress>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtTypes>

#include <functional>
#include <memory>

namespace deskflow::relaydesk {

enum class AddressCandidateSource
{
  RecentSuccessful,
  Discovered,
  Manual,
};

struct AddressCandidate
{
  QHostAddress address;
  quint16 inputPort = kDefaultManualInputPort;
  quint16 filePort = kDefaultManualFilePort;
  AddressCandidateSource source = AddressCandidateSource::Manual;
  QString originHost;

  bool operator==(const AddressCandidate &) const = default;
};

struct AddressCandidateRequest
{
  DiscoverySettings settings;
  QList<QHostAddress> recentSuccessfulAddresses;
  QList<QHostAddress> discoveredAddresses;
  quint16 inputPort = kDefaultManualInputPort;
  quint16 filePort = kDefaultManualFilePort;
};

struct AddressCandidateResult
{
  QList<AddressCandidate> candidates;
  QStringList unresolvedHosts;
  QStringList diagnostics;
};

class AddressCandidateProvider final : public QObject
{
  Q_OBJECT

public:
  using HostResolutionCallback = std::function<void(QList<QHostAddress>, QString)>;
  using HostResolver = std::function<void(const QString &, HostResolutionCallback)>;

  explicit AddressCandidateProvider(HostResolver resolver = {}, QObject *parent = nullptr);

  void resolveCandidates(AddressCandidateRequest request);

Q_SIGNALS:
  void candidatesResolved(AddressCandidateResult result);
  void resolutionFailed(QString diagnostic);

private:
  struct ResolutionState;

  void resolveHost(
      quint64 generation, const std::shared_ptr<ResolutionState> &state, qsizetype manualIndex,
      const ManualAddress &manualAddress
  );
  void finishHost(
      quint64 generation, const std::shared_ptr<ResolutionState> &state, qsizetype manualIndex,
      const ManualAddress &manualAddress, QList<QHostAddress> addresses, QString diagnostic
  );
  void scheduleCompletion(quint64 generation, const std::shared_ptr<ResolutionState> &state);

  HostResolver m_resolver;
  quint64 m_generation = 0;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::AddressCandidateSource)
Q_DECLARE_METATYPE(deskflow::relaydesk::AddressCandidate)
Q_DECLARE_METATYPE(deskflow::relaydesk::AddressCandidateResult)
