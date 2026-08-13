/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/pairing/PairingMessageCodec.h"
#include "relaydesk/pairing/PairingStateMachine.h"
#include "relaydesk/pairing/PairingTrustCommitter.h"

#include <QMetaType>
#include <QString>

namespace deskflow::relaydesk {

enum class PairingOperationError
{
  None,
  InvalidLocalDevice,
  InvalidPeer,
  InvalidEndpoint,
  ActiveSessionExists,
  SessionNotFound,
  SessionMismatch,
  PeerMismatch,
  EndpointMismatch,
  Expired,
  InvalidCode,
  UnexpectedMessage,
  DuplicateMessage,
  DecodeFailed,
  InvalidState,
  SendFailed,
  PersistenceFailed,
  RevokeFailed,
};

struct PairingOperationResult
{
  PairingOperationError error = PairingOperationError::None;
  PairingMessageError messageError = PairingMessageError::None;
  PairingError stateError = PairingError::None;
  PairingTrustCommitError trustError = PairingTrustCommitError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PairingOperationError::None;
  }
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PairingOperationError)
Q_DECLARE_METATYPE(deskflow::relaydesk::PairingOperationResult)
