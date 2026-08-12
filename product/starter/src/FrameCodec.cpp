// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/filetransfer/FrameCodec.h"

#include <QtEndian>

#include <cstring>
#include <limits>
#include <utility>

namespace relaydesk::filetransfer {
namespace {

template <typename T>
void writeBigEndian(char* destination, const T value)
{
    qToBigEndian<T>(value, reinterpret_cast<uchar*>(destination));
}

template <typename T>
T readBigEndian(const char* source)
{
    return qFromBigEndian<T>(reinterpret_cast<const uchar*>(source));
}

DecodeResult protocolError(const DecodeError error, QString diagnostic)
{
    return {
        .status = DecodeStatus::ProtocolError,
        .error = error,
        .diagnostic = std::move(diagnostic),
        .consumedBytes = 0,
    };
}

bool checkedFrameBytes(
    const quint32 metadataBytes,
    const quint64 payloadBytes,
    quint64& totalBytes)
{
    constexpr auto fixed = static_cast<quint64>(kFixedHeaderBytes);
    if (payloadBytes > std::numeric_limits<quint64>::max() - fixed) {
        return false;
    }

    const quint64 withPayload = fixed + payloadBytes;
    if (static_cast<quint64>(metadataBytes) >
        std::numeric_limits<quint64>::max() - withPayload) {
        return false;
    }

    totalBytes = withPayload + static_cast<quint64>(metadataBytes);
    return true;
}

} // namespace

QByteArray FrameCodec::encode(
    const Frame& frame,
    const ProtocolLimits& limits,
    QString* error)
{
    const auto setError = [error](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
    };

    if (frame.version != kProtocolVersion) {
        setError(QStringLiteral("Unsupported protocol version"));
        return {};
    }

    if (!isKnownMessageType(frame.type)) {
        setError(QStringLiteral("Unknown message type"));
        return {};
    }

    if (frame.metadata.size() < 0 ||
        static_cast<quint64>(frame.metadata.size()) > limits.maxMetadataBytes) {
        setError(QStringLiteral("Metadata exceeds protocol limit"));
        return {};
    }

    if (frame.payload.size() < 0 ||
        static_cast<quint64>(frame.payload.size()) > limits.maxPayloadBytes) {
        setError(QStringLiteral("Payload exceeds protocol limit"));
        return {};
    }

    quint64 total64 = 0;
    if (!checkedFrameBytes(
            static_cast<quint32>(frame.metadata.size()),
            static_cast<quint64>(frame.payload.size()),
            total64) ||
        total64 > limits.maxBufferedFrameBytes ||
        total64 > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
        setError(QStringLiteral("Frame size overflows local address space or limit"));
        return {};
    }

    QByteArray encoded;
    encoded.resize(static_cast<qsizetype>(total64));
    char* cursor = encoded.data();

    std::memcpy(cursor, kMagic, sizeof(kMagic));
    writeBigEndian<quint16>(cursor + 4, frame.version);
    writeBigEndian<quint16>(cursor + 6, static_cast<quint16>(frame.type));
    writeBigEndian<quint32>(cursor + 8, frame.flags);
    writeBigEndian<quint32>(
        cursor + 12, static_cast<quint32>(frame.metadata.size()));
    writeBigEndian<quint64>(
        cursor + 16, static_cast<quint64>(frame.payload.size()));
    writeBigEndian<quint64>(cursor + 24, frame.streamId);

    if (!frame.metadata.isEmpty()) {
        std::memcpy(
            cursor + kFixedHeaderBytes,
            frame.metadata.constData(),
            static_cast<size_t>(frame.metadata.size()));
    }

    if (!frame.payload.isEmpty()) {
        std::memcpy(
            cursor + kFixedHeaderBytes + frame.metadata.size(),
            frame.payload.constData(),
            static_cast<size_t>(frame.payload.size()));
    }

    return encoded;
}

DecodeResult FrameCodec::tryDecode(
    QByteArray& buffer,
    Frame& output,
    const ProtocolLimits& limits)
{
    if (buffer.size() < kFixedHeaderBytes) {
        return {
            .status = DecodeStatus::NeedMoreData,
            .error = DecodeError::None,
            .diagnostic = {},
            .consumedBytes = 0,
        };
    }

    const char* header = buffer.constData();

    if (std::memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        return protocolError(
            DecodeError::InvalidMagic, QStringLiteral("Invalid RDFT magic"));
    }

    const quint16 version = readBigEndian<quint16>(header + 4);
    if (version != kProtocolVersion) {
        return protocolError(
            DecodeError::UnsupportedVersion,
            QStringLiteral("Unsupported RDFT major version %1").arg(version));
    }

    const auto type = static_cast<MessageType>(
        readBigEndian<quint16>(header + 6));
    if (!isKnownMessageType(type)) {
        return protocolError(
            DecodeError::UnknownMessageType,
            QStringLiteral("Unknown RDFT message type"));
    }

    const quint32 flags = readBigEndian<quint32>(header + 8);
    const quint32 metadataBytes = readBigEndian<quint32>(header + 12);
    const quint64 payloadBytes = readBigEndian<quint64>(header + 16);
    const quint64 streamId = readBigEndian<quint64>(header + 24);

    if (metadataBytes > limits.maxMetadataBytes) {
        return protocolError(
            DecodeError::MetadataTooLarge,
            QStringLiteral("Metadata length exceeds local limit"));
    }

    if (payloadBytes > limits.maxPayloadBytes) {
        return protocolError(
            DecodeError::PayloadTooLarge,
            QStringLiteral("Payload length exceeds local limit"));
    }

    quint64 total64 = 0;
    if (!checkedFrameBytes(metadataBytes, payloadBytes, total64)) {
        return protocolError(
            DecodeError::LengthOverflow,
            QStringLiteral("Frame length arithmetic overflow"));
    }

    if (total64 > limits.maxBufferedFrameBytes) {
        return protocolError(
            DecodeError::FrameTooLarge,
            QStringLiteral("Frame exceeds local buffered-frame limit"));
    }

    if (total64 > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
        return protocolError(
            DecodeError::LengthOverflow,
            QStringLiteral("Frame cannot be represented by qsizetype"));
    }

    const auto total = static_cast<qsizetype>(total64);
    if (buffer.size() < total) {
        return {
            .status = DecodeStatus::NeedMoreData,
            .error = DecodeError::None,
            .diagnostic = {},
            .consumedBytes = 0,
        };
    }

    const auto metadataSize = static_cast<qsizetype>(metadataBytes);
    const auto payloadSize = static_cast<qsizetype>(payloadBytes);

    Frame decoded;
    decoded.version = version;
    decoded.type = type;
    decoded.flags = flags;
    decoded.streamId = streamId;
    decoded.metadata = QByteArray(
        header + kFixedHeaderBytes, metadataSize);
    decoded.payload = QByteArray(
        header + kFixedHeaderBytes + metadataSize, payloadSize);

    output = std::move(decoded);
    buffer.remove(0, total);

    return {
        .status = DecodeStatus::FrameReady,
        .error = DecodeError::None,
        .diagnostic = {},
        .consumedBytes = total,
    };
}

} // namespace relaydesk::filetransfer
