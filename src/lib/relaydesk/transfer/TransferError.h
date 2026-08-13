// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QMetaType>
#include <QtTypes>

namespace relaydesk::transfer {

// Stable service, snapshot, and history failure values. These values are not
// RDFT ERROR frame codes; they describe the durable result of a transfer.
enum class TransferErrorCode : quint32
{
  None = 0,
  ManifestBuildFailed = 1001,
  OfferFailed = 1002,
  SenderFailed = 1003,
  PeerRejected = 1004,
  PeerFileFailed = 1005,
  DiskFull = 1006,
  UnsafePath = 1007,
  SourceUnreadable = 1008,
  ConnectionLost = 1009,
  HashMismatch = 1010,
  InternalError = 1011,
};

[[nodiscard]] constexpr bool isKnownTransferErrorCode(TransferErrorCode code) noexcept
{
  switch (code) {
  case TransferErrorCode::ManifestBuildFailed:
  case TransferErrorCode::OfferFailed:
  case TransferErrorCode::SenderFailed:
  case TransferErrorCode::PeerRejected:
  case TransferErrorCode::PeerFileFailed:
  case TransferErrorCode::DiskFull:
  case TransferErrorCode::UnsafePath:
  case TransferErrorCode::SourceUnreadable:
  case TransferErrorCode::ConnectionLost:
  case TransferErrorCode::HashMismatch:
  case TransferErrorCode::InternalError:
    return true;
  case TransferErrorCode::None:
    return false;
  }
  return false;
}

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::TransferErrorCode)
