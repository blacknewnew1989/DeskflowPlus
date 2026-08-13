/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/IPlatformFileSafety.h"
#include "relaydesk/transfer/ConflictResolver.h"
#include "relaydesk/transfer/FileReceiver.h"

#include <optional>

class QThread;

namespace deskflow::relaydesk {

// One-file receive lifecycle that must be constructed and consumed on the
// same disk worker. Platform root/link inspection and the final typed atomic
// commit are composed here; success is published only after the injected
// platform adapter has moved the verified .part file to its reserved target.
class IncomingFileReceiverWorker final
{
public:
  explicit IncomingFileReceiverWorker(IPlatformFileSafety &fileSafety);

  IncomingFileReceiverWorker(const IncomingFileReceiverWorker &) = delete;
  IncomingFileReceiverWorker &operator=(const IncomingFileReceiverWorker &) = delete;

  [[nodiscard]] ::relaydesk::transfer::FileReceiverResult
  begin(const ::relaydesk::transfer::FileReceiveRequest &request);
  [[nodiscard]] ::relaydesk::transfer::FileReceiverResult append(
      const ::relaydesk::transfer::FileChunkMessage &chunk, QByteArrayView payload
  );
  [[nodiscard]] ::relaydesk::transfer::FileReceiverResult
  finish(const ::relaydesk::transfer::FileEndMessage &end);
  [[nodiscard]] ::relaydesk::transfer::FileReceiverSnapshot snapshot() const;

  [[nodiscard]] static constexpr bool platformCommitWired() noexcept
  {
    return true;
  }

private:
  [[nodiscard]] bool isOwningThread() const noexcept;
  [[nodiscard]] ::relaydesk::transfer::FileReceiverResult wrongThread() const;
  [[nodiscard]] ::relaydesk::transfer::FileReceiverResult
  safetyFailure(const FileSafetyResult &result) const;

  IPlatformFileSafety &m_fileSafety;
  QThread *m_ownerThread = nullptr;
  ::relaydesk::transfer::FileReceiver m_receiver;
  ::relaydesk::transfer::ConflictResolver m_conflicts;
  std::optional<::relaydesk::transfer::UseTarget> m_target;
  QString m_receiveRoot;
};

} // namespace deskflow::relaydesk
