// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QString>

namespace relaydesk::transfer {

enum class FrameDecodeStatus
{
  FrameReady,
  NeedMoreData,
  ProtocolError,
};

enum class FrameDecodeError
{
  None,
  InvalidMagic,
  UnsupportedMajorVersion,
  UnknownMessageType,
  ControlMetadataTooLarge,
  DataPayloadTooLarge,
  UnexpectedPayload,
  FrameTooLarge,
  LengthOverflow,
};

struct FrameDecodeResult
{
  FrameDecodeStatus status = FrameDecodeStatus::NeedMoreData;
  FrameDecodeError error = FrameDecodeError::None;
  QString diagnostic;
  qsizetype consumedBytes = 0;
};

class FrameCodec final
{
public:
  [[nodiscard]] static QByteArray
  encode(const Frame &frame, const ProtocolLimits &limits = {}, QString *error = nullptr);

  // Decodes and removes at most one frame. NeedMoreData and ProtocolError
  // leave the buffer untouched; trailing bytes remain for the next call.
  [[nodiscard]] static FrameDecodeResult
  tryDecode(QByteArray &buffer, Frame &output, const ProtocolLimits &limits = {});
};

} // namespace relaydesk::transfer
