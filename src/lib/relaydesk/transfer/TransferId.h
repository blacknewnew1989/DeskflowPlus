// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QString>
#include <QUuid>

#include <cstddef>
#include <optional>

namespace relaydesk::transfer {

class TransferId final
{
public:
  static TransferId generate();
  static std::optional<TransferId> fromBytes(QByteArrayView bytes);
  static std::optional<TransferId> fromString(const QString &text);

  [[nodiscard]] QByteArray toBytes() const;
  [[nodiscard]] QString toString() const;
  [[nodiscard]] const QUuid &value() const;

  bool operator==(const TransferId &) const = default;

private:
  explicit TransferId(QUuid value);

  QUuid m_value;
};

size_t qHash(const TransferId &id, size_t seed = 0) noexcept;

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::TransferId)
