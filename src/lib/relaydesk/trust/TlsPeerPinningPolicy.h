/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QString>

namespace deskflow::relaydesk {

enum class PeerPinningError
{
  None,
  UnknownPeer,
  RevokedPeer,
  FingerprintChanged,
  DeviceIdMismatch,
  UnauthenticatedPeer,
};

struct PeerPinningResult
{
  PeerPinningError error = PeerPinningError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PeerPinningError::None;
  }
};

class TlsPeerPinningPolicy final
{
public:
  // Pairing owns trust creation. A normal TLS connection can only consume an
  // existing, non-revoked deviceId -> exact SHA-256 fingerprint mapping.
  [[nodiscard]] static PeerPinningResult
  verify(const TrustedDeviceStore &store, const DeviceId &peerDeviceId, QByteArrayView presentedFingerprintSha256);
};

} // namespace deskflow::relaydesk
