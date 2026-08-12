/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingStateMachine.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QString>
#include <QUuid>

namespace deskflow::relaydesk {

struct PairingTrustSelection
{
  QString alias;
  bool autoAcceptFiles = false;
};

enum class PairingTrustCommitError
{
  None,
  SessionNotConfirming,
  InvalidPeer,
  InvalidTrustRecord,
  PersistenceFailed,
  TransitionFailed,
};

struct PairingTrustCommitResult
{
  PairingTrustCommitError error = PairingTrustCommitError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PairingTrustCommitError::None;
  }
};

class PairingTrustCommitter final
{
public:
  [[nodiscard]] static PairingTrustCommitResult commit(
      PairingStateMachine &stateMachine, TrustedDeviceStore &store, const QUuid &sessionId,
      PairingTrustSelection selection = {}
  );
};

} // namespace deskflow::relaydesk
