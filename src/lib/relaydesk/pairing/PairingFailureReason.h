/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QMetaType>
#include <QtTypes>

namespace deskflow::relaydesk {

// Stable pairing wire and snapshot values. Numeric values are part of the
// RelayDesk pairing v1 compatibility contract and must not be renumbered.
enum class PairingFailureReason : quint32
{
  None = 0,
  Cancelled = 1,
  CodeMismatch = 2,
  Expired = 3,
  TooManyAttempts = 4,
  TransportFailed = 5,
  TrustStoreWriteFailed = 6,
  CertificateChanged = 7,
  DirectConnectionRequired = 8,
};

[[nodiscard]] constexpr bool isKnownPairingFailureReason(PairingFailureReason reason) noexcept
{
  switch (reason) {
  case PairingFailureReason::None:
  case PairingFailureReason::Cancelled:
  case PairingFailureReason::CodeMismatch:
  case PairingFailureReason::Expired:
  case PairingFailureReason::TooManyAttempts:
  case PairingFailureReason::TransportFailed:
  case PairingFailureReason::TrustStoreWriteFailed:
  case PairingFailureReason::CertificateChanged:
  case PairingFailureReason::DirectConnectionRequired:
    return true;
  }
  return false;
}

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::PairingFailureReason)
