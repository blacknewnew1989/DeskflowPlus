// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/filetransfer/Protocol.h"

#include <QByteArray>
#include <QString>

namespace relaydesk::filetransfer {

enum class DecodeStatus {
    FrameReady,
    NeedMoreData,
    ProtocolError
};

enum class DecodeError {
    None,
    InvalidMagic,
    UnsupportedVersion,
    UnknownMessageType,
    MetadataTooLarge,
    PayloadTooLarge,
    FrameTooLarge,
    LengthOverflow
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMoreData;
    DecodeError error = DecodeError::None;
    QString diagnostic;
    qsizetype consumedBytes = 0;
};

class FrameCodec final {
public:
    [[nodiscard]] static QByteArray encode(
        const Frame& frame,
        const ProtocolLimits& limits = {},
        QString* error = nullptr);

    // Decodes and removes at most one frame from buffer.
    // NeedMoreData leaves buffer untouched.
    // ProtocolError leaves buffer untouched so the caller can log/close.
    [[nodiscard]] static DecodeResult tryDecode(
        QByteArray& buffer,
        Frame& output,
        const ProtocolLimits& limits = {});
};

} // namespace relaydesk::filetransfer
