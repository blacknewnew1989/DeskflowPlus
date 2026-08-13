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

class FileId final
{
public:
  static FileId generate();
  static std::optional<FileId> fromBytes(QByteArrayView bytes);
  static std::optional<FileId> fromString(const QString &text);

  [[nodiscard]] QByteArray toBytes() const;
  [[nodiscard]] QString toString() const;
  [[nodiscard]] const QUuid &value() const;

  bool operator==(const FileId &) const = default;

private:
  explicit FileId(QUuid value);

  QUuid m_value;
};

size_t qHash(const FileId &id, size_t seed = 0) noexcept;

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::FileId)
