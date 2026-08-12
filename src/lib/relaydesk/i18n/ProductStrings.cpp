/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/i18n/ProductStrings.h"

#include <QCoreApplication>
#include <QLocale>

#include <array>
#include <cstddef>

namespace deskflow::relaydesk::i18n {
namespace {

constexpr auto kContext = "RelayDesk";

struct Entry
{
  Text id;
  const char *semanticKey;
  const char *english;
  const char *englishPlural = nullptr;
};

constexpr std::array<Entry, static_cast<std::size_t>(Text::Count)> kEntries{{
    {Text::DevicesTitle, QT_TRANSLATE_NOOP("RelayDesk", "devices.title"), "Devices"},
    {Text::DevicesCurrent, QT_TRANSLATE_NOOP("RelayDesk", "devices.current"), "This device"},
    {Text::DevicesStatusOnline, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.online"), "Online"},
    {Text::DevicesStatusOffline, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.offline"), "Offline"},
    {Text::DevicesStatusConnecting, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.connecting"), "Connecting"},
    {Text::DevicesStatusPermissionRequired, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.permission_required"),
     "Permission required"},
    {Text::DevicesStatusPaused, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.paused"), "Paused"},
    {Text::DevicesStatusError, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.error"), "Error"},
    {Text::DevicesStatusDiscovered, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.discovered"), "Discovered"},
    {Text::DevicesStatusPairing, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.pairing"), "Pairing"},
    {Text::DevicesStatusTrustViolation, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.trust_violation"),
     "Trust changed"},
    {Text::DevicesStatusTransferBusy, QT_TRANSLATE_NOOP("RelayDesk", "devices.status.transfer_busy"),
     "File transfer in progress"},
    {Text::DevicesActionSendFile, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.send_file"), "Send files"},
    {Text::DevicesActionMore, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.more"), "More"},
    {Text::DevicesActionAutoArrange, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.auto_arrange"), "Auto arrange"},
    {Text::DevicesActionResetLayout, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.reset_layout"), "Reset layout"},
    {Text::DevicesDropSendHere, QT_TRANSLATE_NOOP("RelayDesk", "devices.drop.send_here"), "Drop files here to send"},
    {Text::DevicesDropItems, QT_TRANSLATE_N_NOOP("RelayDesk", "devices.drop.items"), "%n item", "%n items"},
    {Text::PairingTitle, QT_TRANSLATE_NOOP("RelayDesk", "pairing.title"), "Pair device"},
    {Text::PairingCodePrompt, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.prompt"), "Enter the six-digit code"},
    {Text::PairingCodeExpired, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.expired"),
     "The pairing code expired. Generate a new code."},
    {Text::PairingCertificateChanged, QT_TRANSLATE_NOOP("RelayDesk", "pairing.certificate_changed"),
     "The other device certificate changed. Automatic connection was stopped."},
    {Text::PairingTooManyAttempts, QT_TRANSLATE_NOOP("RelayDesk", "pairing.too_many_attempts"),
     "Too many incorrect attempts. Try again later."},
    {Text::PairingNotDirect, QT_TRANSLATE_NOOP("RelayDesk", "pairing.not_direct"),
     "The devices cannot connect directly. Try a manual address."},
    {Text::PairingSuccess, QT_TRANSLATE_NOOP("RelayDesk", "pairing.success"), "Device paired"},
    {Text::TransferTitle, QT_TRANSLATE_NOOP("RelayDesk", "transfer.title"), "Transfers"},
    {Text::TransferStatePreparing, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.preparing"), "Preparing"},
    {Text::TransferStateAwaitingConfirmation,
     QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.awaiting_confirmation"), "Waiting for confirmation"},
    {Text::TransferStateQueued, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.queued"), "Queued"},
    {Text::TransferStateTransferring, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.transferring"), "Transferring"},
    {Text::TransferStatePaused, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.paused"), "Paused"},
    {Text::TransferStateInterrupted, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.interrupted"),
     "Connection lost, waiting to resume"},
    {Text::TransferStateVerifying, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.verifying"), "Verifying"},
    {Text::TransferStateSaving, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.saving"), "Saving"},
    {Text::TransferStateCompleted, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.completed"), "Completed"},
    {Text::TransferStateRejected, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.rejected"), "Rejected"},
    {Text::TransferStateCanceled, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.canceled"), "Canceled"},
    {Text::TransferStateFailed, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.failed"), "Failed"},
    {Text::TransferActionPause, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.pause"), "Pause"},
    {Text::TransferActionResume, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.resume"), "Resume"},
    {Text::TransferActionCancel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.cancel"), "Cancel"},
    {Text::TransferActionRetry, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.retry"), "Retry"},
    {Text::TransferActionOpenFolder, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.open_folder"), "Open folder"},
    {Text::TransferActionAccept, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.accept"), "Accept"},
    {Text::TransferActionReject, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.reject"), "Reject"},
    {Text::TransferActionChangeSettings, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.change_settings"),
     "Change settings"},
    {Text::TransferErrorDiskFull, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.disk_full"),
     "Not enough disk space"},
    {Text::TransferErrorUnsafePath, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.unsafe_path"),
     "The received path is not safe"},
    {Text::TransferErrorUnreadable, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.unreadable"),
     "One or more items cannot be read"},
    {Text::TransferErrorConnectionLost, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.connection_lost"),
     "Connection lost"},
    {Text::TransferErrorChecksumMismatch, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.checksum_mismatch"),
     "File verification failed"},
    {Text::TransferIncomingWantsToSend, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.wants_to_send"),
     "%1 wants to send"},
    {Text::TransferIncomingSaveTo, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.save_to"), "Save to: %1"},
    {Text::TransferIncomingAutoRename, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.auto_rename"),
     "Conflict: auto rename"},
    {Text::TransferIncomingAlwaysAccept, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.always_accept"),
     "Always accept files from this device"},
    {Text::SettingsTitle, QT_TRANSLATE_NOOP("RelayDesk", "settings.title"), "Settings"},
    {Text::SettingsGeneral, QT_TRANSLATE_NOOP("RelayDesk", "settings.general"), "General"},
    {Text::SettingsInput, QT_TRANSLATE_NOOP("RelayDesk", "settings.input"), "Keyboard and mouse"},
    {Text::SettingsClipboard, QT_TRANSLATE_NOOP("RelayDesk", "settings.clipboard"), "Clipboard"},
    {Text::SettingsFileTransfer, QT_TRANSLATE_NOOP("RelayDesk", "settings.file_transfer"), "File transfer"},
    {Text::SettingsNetwork, QT_TRANSLATE_NOOP("RelayDesk", "settings.network"), "Discovery and network"},
    {Text::SettingsTrustedDevices, QT_TRANSLATE_NOOP("RelayDesk", "settings.trusted_devices"), "Trusted devices"},
    {Text::SettingsStartup, QT_TRANSLATE_NOOP("RelayDesk", "settings.startup"), "Start with system"},
    {Text::SettingsAdvanced, QT_TRANSLATE_NOOP("RelayDesk", "settings.advanced"), "Advanced and logs"},
    {Text::AboutTitle, QT_TRANSLATE_NOOP("RelayDesk", "about.title"), "About RelayDesk"},
    {Text::AboutDiagnostics, QT_TRANSLATE_NOOP("RelayDesk", "about.diagnostics"), "Diagnostics"},
}};

constexpr bool entriesMatchIds()
{
  for (std::size_t i = 0; i < kEntries.size(); ++i) {
    if (static_cast<std::size_t>(kEntries[i].id) != i)
      return false;
  }
  return true;
}

static_assert(entriesMatchIds());

const Entry &entry(Text text)
{
  return kEntries[static_cast<std::size_t>(text)];
}

} // namespace

QString key(Text text)
{
  return QString::fromLatin1(entry(text).semanticKey);
}

QString translate(Text text)
{
  const auto &message = entry(text);
  const auto semanticKey = QString::fromLatin1(message.semanticKey);
  const auto translated = QCoreApplication::translate(kContext, message.semanticKey);
  return translated == semanticKey ? QString::fromUtf8(message.english) : translated;
}

QString translatePlural(Text text, int count)
{
  const auto &message = entry(text);
  const auto semanticKey = QString::fromLatin1(message.semanticKey);
  const auto translated = QCoreApplication::translate(kContext, message.semanticKey, nullptr, count);
  if (translated != semanticKey)
    return translated;

  const auto fallback = count == 1 || message.englishPlural == nullptr ? message.english : message.englishPlural;
  return QString::fromUtf8(fallback).replace(QStringLiteral("%n"), QLocale().toString(count));
}

bool isPlural(Text text)
{
  return entry(text).englishPlural != nullptr;
}

QStringList allKeys()
{
  QStringList keys;
  keys.reserve(static_cast<qsizetype>(kEntries.size()));
  for (const auto &message : kEntries)
    keys.append(QString::fromLatin1(message.semanticKey));
  return keys;
}

} // namespace deskflow::relaydesk::i18n
