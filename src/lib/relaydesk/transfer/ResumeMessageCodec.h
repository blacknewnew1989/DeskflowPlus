// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/ResumeStore.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>
#include <variant>

namespace relaydesk::transfer {

inline constexpr quint64 kMaximumResumeResponseFiles = 100'000;
inline constexpr quint64 kMaximumResumeMetadataBytes = 1U * 1024U * 1024U;

struct ResumeQueryMessage
{
  TransferId transferId;
  QByteArray manifestSha256;

  [[nodiscard]] bool operator==(const ResumeQueryMessage &) const = default;
};

struct ResumeFileOffset
{
  FileId fileId;
  quint64 durableOffset = 0;

  [[nodiscard]] bool operator==(const ResumeFileOffset &) const = default;
};

struct ResumeResponseMessage
{
  TransferId transferId;
  QByteArray manifestSha256;
  // Strictly ascending by RFC 4122 fileId bytes.
  QList<ResumeFileOffset> files;

  [[nodiscard]] bool operator==(const ResumeResponseMessage &) const = default;
};

using ResumeControlMessage = std::variant<ResumeQueryMessage, ResumeResponseMessage>;

enum class ResumeMessageCodecError
{
  None,
  UnsupportedVersion,
  UnsupportedMessageType,
  TooLarge,
  MalformedCbor,
  InvalidFields,
  InvalidTransferId,
  InvalidManifestHash,
  TooManyFiles,
  InvalidFileId,
  InvalidOffset,
  DuplicateFileId,
  InvalidFileOrder,
};

struct ResumeMessageDecodeResult
{
  std::optional<ResumeControlMessage> message;
  ResumeMessageCodecError error = ResumeMessageCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == ResumeMessageCodecError::None;
  }
};

class ResumeMessageCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const ResumeControlMessage &message, QString *error = nullptr);
  [[nodiscard]] static ResumeMessageDecodeResult
  decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata);
};

enum class ResumeNegotiationError
{
  None,
  InvalidQuery,
  StateNotFound,
  StoredStateInvalid,
  DirectionMismatch,
  ManifestMismatch,
  ResponseMismatch,
  UnknownFile,
  DuplicateFile,
  OffsetOutOfRange,
  ResponseTooLarge,
};

struct ResumeResponseBuildResult
{
  std::optional<ResumeResponseMessage> response;
  ResumeNegotiationError error = ResumeNegotiationError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return response.has_value() && error == ResumeNegotiationError::None;
  }
};

struct ResumePlan
{
  TransferId transferId;
  QList<ResumeFileOffset> files;

  [[nodiscard]] bool operator==(const ResumePlan &) const = default;
};

struct ResumePlanResult
{
  std::optional<ResumePlan> plan;
  ResumeNegotiationError error = ResumeNegotiationError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return plan.has_value() && error == ResumeNegotiationError::None;
  }
};

class ResumeNegotiator final
{
public:
  // Receiver side: only a valid receiving state with the exact manifest hash
  // can produce offsets. Missing or corrupt state never implies blind resume.
  [[nodiscard]] static ResumeResponseBuildResult
  buildResponse(const ResumeStore &store, const ResumeQueryMessage &query);

  // Sender side: bind the response to the original query and manifest, and
  // reject offsets beyond the prepared file sizes.
  [[nodiscard]] static ResumePlanResult validateResponse(
      const ResumeQueryMessage &query, const ResumeResponseMessage &response,
      const QList<ManifestEntry> &manifestEntries
  );
};

} // namespace relaydesk::transfer
