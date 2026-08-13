// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferId.h"

#include <QRegularExpression>

#include <utility>

namespace relaydesk::transfer {
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

TransferId::TransferId(QUuid value) : m_value(std::move(value))
{
}

TransferId TransferId::generate()
{
  return TransferId(QUuid::createUuid());
}

std::optional<TransferId> TransferId::fromBytes(QByteArrayView bytes)
{
  if (bytes.size() != 16) {
    return std::nullopt;
  }
  const auto value = QUuid::fromRfc4122(bytes.toByteArray());
  return value.isNull() ? std::nullopt : std::optional<TransferId>{TransferId(value)};
}

std::optional<TransferId> TransferId::fromString(const QString &text)
{
  if (!canonicalUuidPattern().match(text).hasMatch()) {
    return std::nullopt;
  }
  const QUuid value(text);
  return value.isNull() ? std::nullopt : std::optional<TransferId>{TransferId(value)};
}

QByteArray TransferId::toBytes() const
{
  return m_value.toRfc4122();
}

QString TransferId::toString() const
{
  return m_value.toString(QUuid::WithoutBraces);
}

const QUuid &TransferId::value() const
{
  return m_value;
}

size_t qHash(const TransferId &id, size_t seed) noexcept
{
  return ::qHash(id.value(), seed);
}

} // namespace relaydesk::transfer
