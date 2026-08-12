// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>
#include <variant>

namespace relaydesk::transfer {

struct FileBeginMessage
{
  TransferId transferId;
  FileId fileId;
  quint64 size = 0;
  quint64 startOffset = 0;
  quint32 chunkBytes = 0;
  QByteArray expectedSha256;

  [[nodiscard]] bool operator==(const FileBeginMessage &) const = default;
};

struct FileChunkMessage
{
  TransferId transferId;
  FileId fileId;
  quint64 offset = 0;
  quint64 sequence = 0;

  [[nodiscard]] bool operator==(const FileChunkMessage &) const = default;
};

struct FileCheckpointMessage
{
  TransferId transferId;
  FileId fileId;
  quint64 durableOffset = 0;

  [[nodiscard]] bool operator==(const FileCheckpointMessage &) const = default;
};

struct FileEndMessage
{
  TransferId transferId;
  FileId fileId;
  quint64 size = 0;
  QByteArray sha256;

  [[nodiscard]] bool operator==(const FileEndMessage &) const = default;
};

struct FileResultMessage
{
  TransferId transferId;
  FileId fileId;
  FileResultCode code = FileResultCode::Ok;
  QString diagnostic;

  [[nodiscard]] bool operator==(const FileResultMessage &) const = default;
};

using FileControlMessage =
    std::variant<FileBeginMessage, FileChunkMessage, FileCheckpointMessage, FileEndMessage, FileResultMessage>;

enum class FileMessageCodecError
{
  None,
  UnsupportedMessageType,
  MalformedCbor,
  InvalidFields,
  InvalidTransferId,
  InvalidFileId,
  InvalidInteger,
  InvalidChunkSize,
  InvalidHash,
  InvalidResult,
};

struct FileMessageDecodeResult
{
  std::optional<FileControlMessage> message;
  FileMessageCodecError error = FileMessageCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == FileMessageCodecError::None;
  }
};

class FileMessageCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const FileControlMessage &message, QString *error = nullptr);
  [[nodiscard]] static FileMessageDecodeResult decode(MessageType type, QByteArrayView metadata);
};

[[nodiscard]] MessageType fileMessageType(const FileControlMessage &message) noexcept;

} // namespace relaydesk::transfer
