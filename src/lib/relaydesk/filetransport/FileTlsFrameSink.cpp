/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/filetransport/FileTlsFrameSink.h"

#include "relaydesk/filetransport/FileTlsTransport.h"

#include <QThread>

#include <limits>
#include <utility>

namespace deskflow::relaydesk {

FileTlsFrameSink::FileTlsFrameSink(FileTlsConnection &connection) : m_connection(connection)
{
}

quint64 FileTlsFrameSink::queuedBytes() const noexcept
{
  if (QThread::currentThread() != m_connection.thread()) {
    return std::numeric_limits<quint64>::max();
  }
  return m_connection.queuedWriteBytes();
}

::relaydesk::transfer::SenderFrameSinkResult FileTlsFrameSink::submit(const ::relaydesk::transfer::Frame &frame)
{
  if (QThread::currentThread() != m_connection.thread()) {
    return {
        .status = ::relaydesk::transfer::SenderFrameSinkStatus::Failed,
        .diagnostic = QStringLiteral("file TLS frame sink must submit on the connection owning thread"),
    };
  }
  QString diagnostic;
  const FileTlsError error = m_connection.sendFrame(frame, &diagnostic);
  if (error == FileTlsError::None) {
    return {.status = ::relaydesk::transfer::SenderFrameSinkStatus::Accepted};
  }
  if (error == FileTlsError::WriteLimitExceeded) {
    return {
        .status = ::relaydesk::transfer::SenderFrameSinkStatus::Backpressured,
        .diagnostic = std::move(diagnostic),
    };
  }
  return {
      .status = ::relaydesk::transfer::SenderFrameSinkStatus::Failed,
      .diagnostic = std::move(diagnostic),
  };
}

} // namespace deskflow::relaydesk
