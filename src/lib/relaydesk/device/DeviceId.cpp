/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/device/DeviceId.h"

#include <QRegularExpression>

#include <utility>

namespace deskflow::relaydesk {

namespace {
const QRegularExpression &canonicalUuidPattern()
{
  static const QRegularExpression pattern(
      QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
      QRegularExpression::CaseInsensitiveOption
  );
  return pattern;
}
} // namespace

DeviceId::DeviceId(QUuid value) : m_value(std::move(value))
{
}

DeviceId DeviceId::generate()
{
  return DeviceId(QUuid::createUuid());
}

std::optional<DeviceId> DeviceId::fromBytes(QByteArrayView bytes)
{
  if (bytes.size() != 16) {
    return std::nullopt;
  }

  const auto value = QUuid::fromRfc4122(bytes.toByteArray());
  if (value.isNull()) {
    return std::nullopt;
  }

  return DeviceId(value);
}

std::optional<DeviceId> DeviceId::fromString(const QString &text)
{
  if (!canonicalUuidPattern().match(text).hasMatch()) {
    return std::nullopt;
  }

  const QUuid value(text);
  if (value.isNull()) {
    return std::nullopt;
  }

  return DeviceId(value);
}

QByteArray DeviceId::toBytes() const
{
  return m_value.toRfc4122();
}

QString DeviceId::toString() const
{
  return m_value.toString(QUuid::WithoutBraces);
}

const QUuid &DeviceId::value() const
{
  return m_value;
}

size_t qHash(const DeviceId &id, size_t seed) noexcept
{
  return ::qHash(id.value(), seed);
}

} // namespace deskflow::relaydesk
