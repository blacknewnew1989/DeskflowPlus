// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/FileId.h"

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

FileId::FileId(QUuid value) : m_value(std::move(value))
{
}

FileId FileId::generate()
{
  return FileId(QUuid::createUuid());
}

std::optional<FileId> FileId::fromBytes(QByteArrayView bytes)
{
  if (bytes.size() != 16) {
    return std::nullopt;
  }
  const auto value = QUuid::fromRfc4122(bytes.toByteArray());
  return value.isNull() ? std::nullopt : std::optional<FileId>{FileId(value)};
}

std::optional<FileId> FileId::fromString(const QString &text)
{
  if (!canonicalUuidPattern().match(text).hasMatch()) {
    return std::nullopt;
  }
  const QUuid value(text);
  return value.isNull() ? std::nullopt : std::optional<FileId>{FileId(value)};
}

QByteArray FileId::toBytes() const
{
  return m_value.toRfc4122();
}

QString FileId::toString() const
{
  return m_value.toString(QUuid::WithoutBraces);
}

const QUuid &FileId::value() const
{
  return m_value;
}

size_t qHash(const FileId &id, size_t seed) noexcept
{
  return ::qHash(id.value(), seed);
}

} // namespace relaydesk::transfer
