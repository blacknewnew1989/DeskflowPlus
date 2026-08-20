// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#pragma once

#include "relaydesk/transfer/Protocol.h"

#include <QString>

class QSettings;

namespace relaydesk::transfer {

inline constexpr int kTransferSettingsSchemaVersion = 1;

enum class IncomingTransferPolicy
{
  Ask,
  AutoAcceptTrusted,
};

struct TransferSettings
{
  QString receiveRoot;
  IncomingTransferPolicy incomingPolicy = IncomingTransferPolicy::Ask;
  ConflictPolicy defaultConflictPolicy = ConflictPolicy::AutoRename;

  [[nodiscard]] bool autoAcceptTrusted() const noexcept
  {
    return incomingPolicy == IncomingTransferPolicy::AutoAcceptTrusted;
  }

  [[nodiscard]] bool operator==(const TransferSettings &) const = default;
};

struct TransferSettingsLoadResult
{
  bool ok = false;
  TransferSettings settings;
  QString diagnostic;
};

[[nodiscard]] QString defaultReceiveRoot();
[[nodiscard]] bool validateReceiveRoot(QString &receiveRoot, QString *diagnostic = nullptr);

class TransferSettingsStore final
{
public:
  explicit TransferSettingsStore(QSettings &settings);

  [[nodiscard]] TransferSettingsLoadResult load();
  [[nodiscard]] bool save(TransferSettings settings, QString *diagnostic = nullptr);

  [[nodiscard]] static QString schemaVersionKey();
  [[nodiscard]] static QString receiveRootKey();
  [[nodiscard]] static QString incomingPolicyKey();
  [[nodiscard]] static QString defaultConflictPolicyKey();

private:
  [[nodiscard]] bool saveValidated(TransferSettings settings, QString *diagnostic);

  QSettings &m_settings;
};

} // namespace relaydesk::transfer

Q_DECLARE_METATYPE(relaydesk::transfer::IncomingTransferPolicy)
Q_DECLARE_METATYPE(relaydesk::transfer::TransferSettings)
