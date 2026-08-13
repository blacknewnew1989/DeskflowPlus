// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QString>

#include <array>
#include <span>

namespace relaydesk::transfer {

enum class ProtocolMessageClassification
{
  Implemented,
  Reserved,
};

enum class ProtocolCodecFamily
{
  Session,
  Capability,
  Control,
  Manifest,
  File,
  TransferCommand,
  TransferCompletion,
  Resume,
};

enum class ProtocolStreamRule
{
  Zero,
  NonZero,
};

enum class ProtocolContentRule
{
  Required,
  Forbidden,
};

struct ProtocolMessageDescriptor
{
  MessageType type;
  const char *name;
  ProtocolMessageClassification classification;
  ProtocolCodecFamily codecFamily;
  const char *schema;
  const char *testTarget;
  ProtocolStreamRule streamRule;
  ProtocolContentRule metadataRule;
  ProtocolContentRule payloadRule;
  std::array<quint32, 4> validFlagSets;
  quint8 validFlagSetCount = 0;
};

enum class ProtocolEnvelopeError
{
  None,
  UnknownMessageType,
  ReservedMessageType,
  InvalidFlags,
  InvalidStreamId,
  MissingMetadata,
  UnexpectedMetadata,
  MissingPayload,
  UnexpectedPayload,
};

struct ProtocolEnvelopeValidationResult
{
  ProtocolEnvelopeError error = ProtocolEnvelopeError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ProtocolEnvelopeError::None;
  }
};

[[nodiscard]] std::span<const ProtocolMessageDescriptor> protocolMessageDescriptors() noexcept;
[[nodiscard]] const ProtocolMessageDescriptor *protocolMessageDescriptor(MessageType type) noexcept;
[[nodiscard]] ProtocolEnvelopeValidationResult validateProtocolEnvelope(
    MessageType type, quint32 flags, quint64 streamId, quint64 metadataBytes, quint64 payloadBytes
);

} // namespace relaydesk::transfer
