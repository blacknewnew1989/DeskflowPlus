/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QString>
#include <QUuid>

#include <cstddef>
#include <optional>

namespace deskflow::relaydesk {

class DeviceId final
{
public:
  static DeviceId generate();
  static std::optional<DeviceId> fromBytes(QByteArrayView bytes);
  static std::optional<DeviceId> fromString(const QString &text);

  [[nodiscard]] QByteArray toBytes() const;
  [[nodiscard]] QString toString() const;
  [[nodiscard]] const QUuid &value() const;

  bool operator==(const DeviceId &) const = default;

private:
  explicit DeviceId(QUuid value);

  QUuid m_value;
};

size_t qHash(const DeviceId &id, size_t seed = 0) noexcept;

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::DeviceId)
