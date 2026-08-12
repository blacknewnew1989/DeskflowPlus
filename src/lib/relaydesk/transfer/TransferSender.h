// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/TransferTypes.h"

#include <QString>

#include <memory>
#include <optional>

namespace relaydesk::transfer {

inline constexpr quint32 kDefaultSenderChunkBytes = 1U * 1024U * 1024U;
inline constexpr quint32 kMaxSenderChunkBytes = 4U * 1024U * 1024U;

struct TransferSenderRequest
{
  TransferId transferId;
  PreparedManifestEntry source;
  quint64 streamId = 0;
  quint32 chunkBytes = kDefaultSenderChunkBytes;
};

enum class TransferSenderError
{
  None,
  InvalidRequest,
  SourceOpenFailed,
  SourceReadFailed,
  SourceChanged,
  ProtocolError,
  AlreadyFinished,
};

enum class SenderFrameStatus
{
  FrameReady,
  Finished,
  Failed,
};

struct SenderFrameResult
{
  std::optional<Frame> frame;
  SenderFrameStatus status = SenderFrameStatus::Failed;
  TransferSenderError error = TransferSenderError::None;
  QString diagnostic;

  [[nodiscard]] bool ready() const noexcept
  {
    return status == SenderFrameStatus::FrameReady && frame.has_value() && error == TransferSenderError::None;
  }
};

class TransferSender final
{
public:
  explicit TransferSender(TransferSenderRequest request);
  ~TransferSender();

  TransferSender(const TransferSender &) = delete;
  TransferSender &operator=(const TransferSender &) = delete;

  // Pure worker-side pull API. Every call performs at most one bounded QFile
  // read and produces at most one frame. Network callbacks only consume the
  // returned frame and never hash or read disk.
  [[nodiscard]] SenderFrameResult nextFrame();

  [[nodiscard]] quint64 bytesProduced() const noexcept;
  [[nodiscard]] quint64 nextSequence() const noexcept;
  [[nodiscard]] bool finished() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace relaydesk::transfer
