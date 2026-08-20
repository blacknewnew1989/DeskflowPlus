/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QString>
#include <QtTypes>

#include <optional>

namespace deskflow::relaydesk {

inline constexpr auto kDiscoveryProtocol = "relaydesk-discovery";
inline constexpr qint64 kDiscoveryProtocolVersion = 1;
inline constexpr qsizetype kMaximumDiscoveryDatagramBytes = 4096;
inline constexpr qsizetype kMaximumDiscoveryPayloadBytes = 3584;

enum class DiscoveryMessageType : qint64
{
  Advertisement = 1,
  Probe = 2,
};

enum class DiscoveryCodecError
{
  None,
  EmptyDatagram,
  DatagramTooLarge,
  InvalidCbor,
  InvalidEnvelope,
  UnsupportedProtocol,
  UnsupportedVersion,
  UnknownMessageType,
  PayloadTooLarge,
  InvalidDeviceInfo,
};

struct DiscoveryDatagram
{
  DiscoveryMessageType type = DiscoveryMessageType::Advertisement;
  std::optional<DeviceInfo> device;

  bool operator==(const DiscoveryDatagram &) const = default;
};

struct DiscoveryDecodeResult
{
  std::optional<DiscoveryDatagram> datagram;
  DiscoveryCodecError error = DiscoveryCodecError::None;
  QString diagnostic;

  [[nodiscard]] bool isSuccess() const
  {
    return datagram.has_value();
  }
};

class DiscoveryCodec final
{
public:
  [[nodiscard]] static QByteArray encodeAdvertisement(const DeviceInfo &device, QString *errorMessage = nullptr);
  [[nodiscard]] static QByteArray encodeProbe();
  [[nodiscard]] static DiscoveryDecodeResult decode(QByteArrayView datagram);
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::DiscoveryCodecError)
