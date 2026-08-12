/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TlsPeerPinningPolicy.h"

namespace deskflow::relaydesk {

PeerPinningResult TlsPeerPinningPolicy::verify(
    const TrustedDeviceStore &store, const DeviceId &peerDeviceId, QByteArrayView presentedFingerprintSha256
)
{
  switch (store.trustStatus(peerDeviceId, presentedFingerprintSha256)) {
  case TrustStatus::Trusted:
    return {};
  case TrustStatus::Unknown:
    return {
        .error = PeerPinningError::UnknownPeer,
        .diagnostic = QStringLiteral("TLS peer device is not paired"),
    };
  case TrustStatus::Revoked:
    return {
        .error = PeerPinningError::RevokedPeer,
        .diagnostic = QStringLiteral("TLS peer trust was revoked"),
    };
  case TrustStatus::FingerprintMismatch:
    return {
        .error = PeerPinningError::FingerprintChanged,
        .diagnostic = QStringLiteral("TLS peer certificate fingerprint changed"),
    };
  }
  return {
      .error = PeerPinningError::FingerprintChanged,
      .diagnostic = QStringLiteral("TLS peer pinning status is invalid"),
  };
}

} // namespace deskflow::relaydesk
