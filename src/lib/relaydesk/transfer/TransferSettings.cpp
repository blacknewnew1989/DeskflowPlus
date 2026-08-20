// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSettings.h"

#include <QDir>
#include <QMetaType>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <array>
#include <optional>
#include <utility>

namespace relaydesk::transfer {
namespace {
constexpr auto kSettingsPrefix = "relaydesk/transfer/";
constexpr auto kSchemaVersionName = "schemaVersion";
constexpr auto kReceiveRootName = "receiveRoot";
constexpr auto kIncomingPolicyName = "incomingPolicy";
constexpr auto kDefaultConflictPolicyName = "defaultConflictPolicy";

void setDiagnostic(QString *diagnostic, const QString &message)
{
  if (diagnostic != nullptr)
    *diagnostic = message;
}

QString key(const char *name)
{
  return QString::fromLatin1(kSettingsPrefix) + QString::fromLatin1(name);
}

QString incomingPolicyName(IncomingTransferPolicy policy)
{
  switch (policy) {
  case IncomingTransferPolicy::Ask:
    return QStringLiteral("ask");
  case IncomingTransferPolicy::AutoAcceptTrusted:
    return QStringLiteral("autoAcceptTrusted");
  }
  return {};
}

std::optional<IncomingTransferPolicy> parseIncomingPolicy(const QVariant &value)
{
  if (value.metaType().id() != QMetaType::QString)
    return std::nullopt;
  if (value.toString() == QStringLiteral("ask"))
    return IncomingTransferPolicy::Ask;
  if (value.toString() == QStringLiteral("autoAcceptTrusted"))
    return IncomingTransferPolicy::AutoAcceptTrusted;
  return std::nullopt;
}

QString conflictPolicyName(ConflictPolicy policy)
{
  switch (policy) {
  case ConflictPolicy::AutoRename:
    return QStringLiteral("autoRename");
  case ConflictPolicy::Overwrite:
    return QStringLiteral("overwrite");
  case ConflictPolicy::Skip:
    return QStringLiteral("skip");
  case ConflictPolicy::Ask:
    return QStringLiteral("ask");
  }
  return {};
}

std::optional<ConflictPolicy> parseConflictPolicy(const QVariant &value)
{
  if (value.metaType().id() != QMetaType::QString)
    return std::nullopt;
  const QString name = value.toString();
  if (name == QStringLiteral("autoRename"))
    return ConflictPolicy::AutoRename;
  if (name == QStringLiteral("overwrite"))
    return ConflictPolicy::Overwrite;
  if (name == QStringLiteral("skip"))
    return ConflictPolicy::Skip;
  if (name == QStringLiteral("ask"))
    return ConflictPolicy::Ask;
  return std::nullopt;
}
} // namespace

QString defaultReceiveRoot()
{
  QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (downloads.isEmpty())
    downloads = QDir::homePath() + QStringLiteral("/Downloads");
  return QDir::cleanPath(QDir(downloads).absoluteFilePath(QStringLiteral("RelayDesk")));
}

bool validateReceiveRoot(QString &receiveRoot, QString *diagnostic)
{
  if (diagnostic != nullptr)
    diagnostic->clear();
  receiveRoot = receiveRoot.trimmed();
  if (receiveRoot.isEmpty() || !QDir::isAbsolutePath(receiveRoot)) {
    setDiagnostic(diagnostic, QStringLiteral("Receive root must be an absolute, non-empty path"));
    return false;
  }
  receiveRoot = QDir::cleanPath(QDir(receiveRoot).absolutePath());
  if (receiveRoot.isEmpty() || QDir(receiveRoot).isRoot()) {
    setDiagnostic(diagnostic, QStringLiteral("Receive root must not be a filesystem root"));
    return false;
  }
  return true;
}

TransferSettingsStore::TransferSettingsStore(QSettings &settings) : m_settings(settings)
{
  m_settings.setAtomicSyncRequired(true);
}

TransferSettingsLoadResult TransferSettingsStore::load()
{
  const auto schema = m_settings.value(schemaVersionKey());
  if (!schema.isValid()) {
    if (m_settings.contains(receiveRootKey()) || m_settings.contains(incomingPolicyKey()) ||
        m_settings.contains(defaultConflictPolicyKey())) {
      return {.ok = false, .diagnostic = QStringLiteral("Transfer settings are partially written")};
    }
    return {.ok = true, .settings = {.receiveRoot = defaultReceiveRoot()}};
  }
  bool schemaOk = false;
  if (schema.metaType().id() != QMetaType::Int || schema.toInt(&schemaOk) != kTransferSettingsSchemaVersion ||
      !schemaOk) {
    return {.ok = false, .diagnostic = QStringLiteral("Unsupported transfer settings schema version")};
  }
  const auto root = m_settings.value(receiveRootKey());
  const auto incomingPolicy = parseIncomingPolicy(m_settings.value(incomingPolicyKey()));
  const auto conflictPolicy = parseConflictPolicy(m_settings.value(defaultConflictPolicyKey()));
  if (root.metaType().id() != QMetaType::QString || !incomingPolicy.has_value() || !conflictPolicy.has_value())
    return {.ok = false, .diagnostic = QStringLiteral("Transfer settings contain invalid fields")};
  TransferSettings settings{.receiveRoot = root.toString(), .incomingPolicy = *incomingPolicy,
                            .defaultConflictPolicy = *conflictPolicy};
  QString diagnostic;
  if (!validateReceiveRoot(settings.receiveRoot, &diagnostic))
    return {.ok = false, .diagnostic = std::move(diagnostic)};
  return {.ok = true, .settings = std::move(settings)};
}

bool TransferSettingsStore::save(TransferSettings settings, QString *diagnostic)
{
  if (diagnostic != nullptr)
    diagnostic->clear();
  if (!load().ok) {
    setDiagnostic(diagnostic, QStringLiteral("Refusing to overwrite unreadable RelayDesk transfer settings"));
    return false;
  }
  return saveValidated(std::move(settings), diagnostic);
}

bool TransferSettingsStore::saveValidated(TransferSettings settings, QString *diagnostic)
{
  if (!validateReceiveRoot(settings.receiveRoot, diagnostic) || incomingPolicyName(settings.incomingPolicy).isEmpty() ||
      conflictPolicyName(settings.defaultConflictPolicy).isEmpty()) {
    if (diagnostic != nullptr && diagnostic->isEmpty())
      *diagnostic = QStringLiteral("Transfer settings contain an unknown policy");
    return false;
  }
  struct PreviousValue
  {
    QString key;
    bool existed;
    QVariant value;
  };
  const std::array previous{
      PreviousValue{schemaVersionKey(), m_settings.contains(schemaVersionKey()), m_settings.value(schemaVersionKey())},
      PreviousValue{receiveRootKey(), m_settings.contains(receiveRootKey()), m_settings.value(receiveRootKey())},
      PreviousValue{incomingPolicyKey(), m_settings.contains(incomingPolicyKey()), m_settings.value(incomingPolicyKey())},
      PreviousValue{
          defaultConflictPolicyKey(), m_settings.contains(defaultConflictPolicyKey()),
          m_settings.value(defaultConflictPolicyKey())
      },
  };
  m_settings.setValue(schemaVersionKey(), kTransferSettingsSchemaVersion);
  m_settings.setValue(receiveRootKey(), settings.receiveRoot);
  m_settings.setValue(incomingPolicyKey(), incomingPolicyName(settings.incomingPolicy));
  m_settings.setValue(defaultConflictPolicyKey(), conflictPolicyName(settings.defaultConflictPolicy));
  m_settings.sync();
  if (m_settings.status() != QSettings::NoError) {
    for (const auto &entry : previous) {
      if (entry.existed)
        m_settings.setValue(entry.key, entry.value);
      else
        m_settings.remove(entry.key);
    }
    setDiagnostic(diagnostic, QStringLiteral("Unable to persist RelayDesk transfer settings"));
    return false;
  }
  return true;
}

QString TransferSettingsStore::schemaVersionKey() { return key(kSchemaVersionName); }
QString TransferSettingsStore::receiveRootKey() { return key(kReceiveRootName); }
QString TransferSettingsStore::incomingPolicyKey() { return key(kIncomingPolicyName); }
QString TransferSettingsStore::defaultConflictPolicyKey() { return key(kDefaultConflictPolicyName); }

} // namespace relaydesk::transfer
