// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/ProtocolMessageRegistry.h"

#include <algorithm>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr std::array<ProtocolMessageDescriptor, kProtocolMessageTypeCount> kDescriptors = {{
#define RDFT_MESSAGE(name, value, classification, codec, schema, test, stream, metadata, payload, count, f0, f1, f2, f3) \
  {MessageType::name, #name, ProtocolMessageClassification::classification, ProtocolCodecFamily::codec, schema, test,    \
   ProtocolStreamRule::stream, ProtocolContentRule::metadata, ProtocolContentRule::payload, {{f0, f1, f2, f3}}, count},
#include "relaydesk/transfer/ProtocolMessageRegistry.def"
#undef RDFT_MESSAGE
}};

ProtocolEnvelopeValidationResult failure(ProtocolEnvelopeError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

} // namespace

std::span<const ProtocolMessageDescriptor> protocolMessageDescriptors() noexcept
{
  return kDescriptors;
}

const ProtocolMessageDescriptor *protocolMessageDescriptor(MessageType type) noexcept
{
  const auto found = std::find_if(kDescriptors.cbegin(), kDescriptors.cend(), [type](const auto &descriptor) {
    return descriptor.type == type;
  });
  return found == kDescriptors.cend() ? nullptr : &*found;
}

bool isKnownMessageType(MessageType type) noexcept
{
  return protocolMessageDescriptor(type) != nullptr;
}

ProtocolEnvelopeValidationResult validateProtocolEnvelope(
    MessageType type, quint32 flags, quint64 streamId, quint64 metadataBytes, quint64 payloadBytes
)
{
  const auto *descriptor = protocolMessageDescriptor(type);
  if (descriptor == nullptr) {
    return failure(ProtocolEnvelopeError::UnknownMessageType, QStringLiteral("unknown RDFT message type"));
  }
  if (descriptor->classification == ProtocolMessageClassification::Reserved) {
    return failure(
        ProtocolEnvelopeError::ReservedMessageType, QStringLiteral("reserved RDFT message type is not sendable")
    );
  }
  const auto validFlagsEnd = descriptor->validFlagSets.cbegin() + descriptor->validFlagSetCount;
  if (std::find(descriptor->validFlagSets.cbegin(), validFlagsEnd, flags) == validFlagsEnd) {
    return failure(
        ProtocolEnvelopeError::InvalidFlags,
        QStringLiteral("RDFT %1 flags are invalid").arg(QString::fromLatin1(descriptor->name))
    );
  }
  const bool streamValid = descriptor->streamRule == ProtocolStreamRule::Zero ? streamId == 0 : streamId != 0;
  if (!streamValid) {
    return failure(
        ProtocolEnvelopeError::InvalidStreamId,
        QStringLiteral("RDFT %1 stream ID is invalid").arg(QString::fromLatin1(descriptor->name))
    );
  }
  if (descriptor->metadataRule == ProtocolContentRule::Required && metadataBytes == 0) {
    return failure(
        ProtocolEnvelopeError::MissingMetadata,
        QStringLiteral("RDFT %1 requires metadata").arg(QString::fromLatin1(descriptor->name))
    );
  }
  if (descriptor->metadataRule == ProtocolContentRule::Forbidden && metadataBytes != 0) {
    return failure(
        ProtocolEnvelopeError::UnexpectedMetadata,
        QStringLiteral("RDFT %1 forbids metadata").arg(QString::fromLatin1(descriptor->name))
    );
  }
  if (descriptor->payloadRule == ProtocolContentRule::Required && payloadBytes == 0) {
    return failure(
        ProtocolEnvelopeError::MissingPayload,
        QStringLiteral("RDFT %1 requires a payload").arg(QString::fromLatin1(descriptor->name))
    );
  }
  if (descriptor->payloadRule == ProtocolContentRule::Forbidden && payloadBytes != 0) {
    return failure(
        ProtocolEnvelopeError::UnexpectedPayload,
        QStringLiteral("RDFT %1 forbids a payload").arg(QString::fromLatin1(descriptor->name))
    );
  }
  return {};
}

} // namespace relaydesk::transfer
