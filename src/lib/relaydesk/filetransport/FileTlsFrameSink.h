/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/transfer/TransferSender.h"

namespace deskflow::relaydesk {

class FileTlsConnection;

// Thin transport-side adapter. It never pulls from TransferSender and thus
// never reads or hashes a file in a QSslSocket callback.
class FileTlsFrameSink final : public ::relaydesk::transfer::TransferFrameSink
{
public:
  explicit FileTlsFrameSink(FileTlsConnection &connection);

  [[nodiscard]] quint64 queuedBytes() const noexcept override;
  [[nodiscard]] ::relaydesk::transfer::SenderFrameSinkResult submit(const ::relaydesk::transfer::Frame &frame) override;

private:
  FileTlsConnection &m_connection;
};

} // namespace deskflow::relaydesk
