// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace relaydesk::transfer {

inline constexpr quint32 kMaximumNegotiablePayloadBytes = 64U * 1024U * 1024U;
inline constexpr quint32 kMaximumNegotiableManifestEntries = 100'000U;
inline constexpr quint16 kMaximumNegotiableConcurrency = 64U;

struct CapabilitiesMessage
{
  QStringList features;
  quint32 preferredChunkBytes = 1U * 1024U * 1024U;
  quint32 maxPayloadBytes = 4U * 1024U * 1024U;
  quint16 maxConcurrentTransfers = 2;
  quint16 maxConcurrentFiles = 2;
  quint32 maxManifestEntries = kMaximumNegotiableManifestEntries;
  QList<ConflictPolicy> conflictPolicies = {ConflictPolicy::AutoRename, ConflictPolicy::Ask};

  [[nodiscard]] bool operator==(const CapabilitiesMessage &) const = default;
};

enum class CapabilityCodecError
{
  None,
  UnsupportedMessageType,
  MalformedCbor,
  InvalidFields,
  InvalidFeatures,
  InvalidLimits,
  InvalidConflictPolicies,
};

struct CapabilitiesDecodeResult
{
  std::optional<CapabilitiesMessage> message;
  CapabilityCodecError error = CapabilityCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return message.has_value() && error == CapabilityCodecError::None;
  }
};

enum class CapabilityNegotiationError
{
  None,
  NoCommonProtocolVersion,
  InvalidLocalCapabilities,
  InvalidPeerCapabilities,
  MissingRequiredFeature,
  NoCommonConflictPolicy,
};

struct NegotiatedCapabilities
{
  quint16 protocolMajorVersion = 0;
  QStringList features;
  quint32 chunkBytes = 0;
  quint32 maxPayloadBytes = 0;
  quint16 maxConcurrentTransfers = 0;
  quint16 maxConcurrentFiles = 0;
  quint32 maxManifestEntries = 0;
  QList<ConflictPolicy> conflictPolicies;

  [[nodiscard]] bool operator==(const NegotiatedCapabilities &) const = default;
};

struct CapabilityNegotiationResult
{
  std::optional<NegotiatedCapabilities> capabilities;
  CapabilityNegotiationError error = CapabilityNegotiationError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return capabilities.has_value() && error == CapabilityNegotiationError::None;
  }
};

class CapabilityCodec final
{
public:
  [[nodiscard]] static QByteArray encode(const CapabilitiesMessage &message, QString *error = nullptr);
  [[nodiscard]] static CapabilitiesDecodeResult decode(MessageType type, QByteArrayView metadata);
};

class CapabilityNegotiator final
{
public:
  [[nodiscard]] static CapabilityNegotiationResult negotiate(
      const QList<quint16> &localVersions, const CapabilitiesMessage &local, const QList<quint16> &peerVersions,
      const CapabilitiesMessage &peer
  );
};

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::NegotiatedCapabilities)
