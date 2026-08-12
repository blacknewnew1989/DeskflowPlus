// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace relaydesk::filetransfer {

inline constexpr char kMagic[4] = {'R', 'D', 'F', 'T'};
inline constexpr quint16 kProtocolVersion = 1;
inline constexpr qsizetype kFixedHeaderBytes = 32;

enum class MessageType : quint16 {
    Hello = 0x0001,
    AuthResult = 0x0002,
    Capabilities = 0x0003,
    Heartbeat = 0x0004,
    HeartbeatAck = 0x0005,

    TransferOffer = 0x0100,
    TransferAccept = 0x0101,
    TransferReject = 0x0102,
    ManifestPage = 0x0103,
    ManifestComplete = 0x0104,

    FileBegin = 0x0200,
    FileChunk = 0x0201,
    FileCheckpoint = 0x0202,
    FileEnd = 0x0203,
    FileResult = 0x0204,

    TransferPause = 0x0300,
    TransferResume = 0x0301,
    TransferCancel = 0x0302,
    TransferComplete = 0x0303,
    TransferResult = 0x0304,

    ResumeQuery = 0x0400,
    ResumeResponse = 0x0401,

    Error = 0x7FFE,
    Goodbye = 0x7FFF
};

enum FrameFlag : quint32 {
    AckRequired = 0x00000001U,
    Response = 0x00000002U,
    Final = 0x00000004U,
    Retryable = 0x00000008U,
    CompressedMetadata = 0x00000010U
};

struct ProtocolLimits {
    quint32 maxMetadataBytes = 1U * 1024U * 1024U;
    quint64 maxPayloadBytes = 4ULL * 1024ULL * 1024ULL;
    quint64 maxBufferedFrameBytes =
        static_cast<quint64>(kFixedHeaderBytes) +
        static_cast<quint64>(maxMetadataBytes) +
        maxPayloadBytes;
};

struct Frame {
    quint16 version = kProtocolVersion;
    MessageType type = MessageType::Heartbeat;
    quint32 flags = 0;
    quint64 streamId = 0;
    QByteArray metadata;
    QByteArray payload;

    [[nodiscard]] bool operator==(const Frame&) const = default;
};

[[nodiscard]] inline bool isKnownMessageType(const MessageType type) noexcept
{
    switch (type) {
    case MessageType::Hello:
    case MessageType::AuthResult:
    case MessageType::Capabilities:
    case MessageType::Heartbeat:
    case MessageType::HeartbeatAck:
    case MessageType::TransferOffer:
    case MessageType::TransferAccept:
    case MessageType::TransferReject:
    case MessageType::ManifestPage:
    case MessageType::ManifestComplete:
    case MessageType::FileBegin:
    case MessageType::FileChunk:
    case MessageType::FileCheckpoint:
    case MessageType::FileEnd:
    case MessageType::FileResult:
    case MessageType::TransferPause:
    case MessageType::TransferResume:
    case MessageType::TransferCancel:
    case MessageType::TransferComplete:
    case MessageType::TransferResult:
    case MessageType::ResumeQuery:
    case MessageType::ResumeResponse:
    case MessageType::Error:
    case MessageType::Goodbye:
        return true;
    }
    return false;
}

} // namespace relaydesk::filetransfer
