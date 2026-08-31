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
    {Text::DevicesEmptyWaiting, QT_TRANSLATE_NOOP("RelayDesk", "devices.empty.waiting"),
     "Nearby devices will appear here"},
    {Text::DevicesLatency, QT_TRANSLATE_NOOP("RelayDesk", "devices.latency"), "%1 ms"},
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
    {Text::DevicesActionSendFolder, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.send_folder"), "Send folder"},
    {Text::DevicesActionRevokeTrust, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.revoke_trust"), "Revoke trust"},
    {Text::DevicesRevokeTrustTitle, QT_TRANSLATE_NOOP("RelayDesk", "devices.revoke_trust.title"), "Revoke trust?"},
    {Text::DevicesRevokeTrustConfirmation, QT_TRANSLATE_NOOP("RelayDesk", "devices.revoke_trust.confirmation"),
     "Remove trust for %1? Its file connection will close and automatic reconnect will stop."},
    {Text::DevicesActionMore, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.more"), "More"},
    {Text::DevicesActionAutoArrange, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.auto_arrange"), "Auto arrange"},
    {Text::DevicesActionConfigureInput, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.configure_input"),
     "Arrange input"},
    {Text::DevicesActionResetLayout, QT_TRANSLATE_NOOP("RelayDesk", "devices.action.reset_layout"), "Reset layout"},
    {Text::DevicesDropSendHere, QT_TRANSLATE_NOOP("RelayDesk", "devices.drop.send_here"), "Drop files here to send"},
    {Text::DevicesDropItems, QT_TRANSLATE_N_NOOP("RelayDesk", "devices.drop.items"), "%n item", "%n items"},
    {Text::DevicesSendSelectDevice, QT_TRANSLATE_NOOP("RelayDesk", "devices.send.select_device"),
     "Select a device first"},
    {Text::DevicesSendUnavailable, QT_TRANSLATE_NOOP("RelayDesk", "devices.send.unavailable"),
     "Files can be sent only to a trusted device that is online"},
    {Text::DevicesSendLocalOnly, QT_TRANSLATE_NOOP("RelayDesk", "devices.send.local_only"),
     "Choose files or folders stored on this device"},
    {Text::DevicesSendEmpty, QT_TRANSLATE_NOOP("RelayDesk", "devices.send.empty"),
     "Choose at least one file or folder"},
    {Text::DevicesSendUnreadable, QT_TRANSLATE_NOOP("RelayDesk", "devices.send.unreadable"),
     "One or more selected items cannot be read"},
    {Text::DevicesManualAddressManage, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.manage"), "Add address"},
    {Text::DevicesManualAddressTitle, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.title"),
     "Manual addresses"},
    {Text::DevicesManualAddressHost, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.host"), "Host"},
    {Text::DevicesManualAddressInputPort, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.input_port"),
     "Input port"},
    {Text::DevicesManualAddressFilePort, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.file_port"),
     "File port"},
    {Text::DevicesManualAddressAdd, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.add"), "Add"},
    {Text::DevicesManualAddressRemove, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.remove"), "Remove"},
    {Text::DevicesManualAddressSave, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.save"), "Save"},
    {Text::DevicesManualAddressInvalid, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.invalid"),
     "Enter a valid host and ports"},
    {Text::DevicesManualAddressSaveFailed, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.save_failed"),
     "Could not save manual addresses. Try again."},
    {Text::DevicesManualAddressEmpty, QT_TRANSLATE_NOOP("RelayDesk", "devices.manual_address.empty"),
     "No manual addresses"},
    {Text::PairingTitle, QT_TRANSLATE_NOOP("RelayDesk", "pairing.title"), "Pair device"},
    {Text::PairingCodePrompt, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.prompt"), "Enter the six-digit code"},
    {Text::PairingStateReady, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.ready"), "Ready to pair"},
    {Text::PairingStateRequesting, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.requesting"), "Requesting pairing"},
    {Text::PairingStateSecuring, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.securing"), "Securing the connection"},
    {Text::PairingStateCompare, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.compare"),
     "Compare the code on both devices"},
    {Text::PairingStateConfirming, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.confirming"), "Confirming pairing"},
    {Text::PairingStateRejected, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.rejected"), "Pairing canceled"},
    {Text::PairingStateFailed, QT_TRANSLATE_NOOP("RelayDesk", "pairing.state.failed"), "Pairing failed"},
    {Text::PairingActionStart, QT_TRANSLATE_NOOP("RelayDesk", "pairing.action.start"), "Pair"},
    {Text::PairingActionPairAgain, QT_TRANSLATE_NOOP("RelayDesk", "pairing.action.pair_again"), "Pair again"},
    {Text::PairingActionCodesMatch, QT_TRANSLATE_NOOP("RelayDesk", "pairing.action.codes_match"), "Codes match"},
    {Text::PairingActionSubmitCode, QT_TRANSLATE_NOOP("RelayDesk", "pairing.action.submit_code"), "Confirm code"},
    {Text::PairingActionCancel, QT_TRANSLATE_NOOP("RelayDesk", "pairing.action.cancel"), "Cancel"},
    {Text::PairingCodeInvalid, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.invalid"), "Enter exactly six digits"},
    {Text::PairingCodeMismatch, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.mismatch"),
     "The pairing code does not match"},
    {Text::PairingCodeExpired, QT_TRANSLATE_NOOP("RelayDesk", "pairing.code.expired"),
     "The pairing code expired. Generate a new code."},
    {Text::PairingAlreadyActive, QT_TRANSLATE_NOOP("RelayDesk", "pairing.error.already_active"),
     "Another pairing is already in progress"},
    {Text::PairingIdentityNotReady, QT_TRANSLATE_NOOP("RelayDesk", "pairing.error.identity_not_ready"),
     "The device identity is not ready. Try again."},
    {Text::PairingActionUnavailable, QT_TRANSLATE_NOOP("RelayDesk", "pairing.error.action_unavailable"),
     "This pairing action is not available now"},
    {Text::PairingSessionUnavailable, QT_TRANSLATE_NOOP("RelayDesk", "pairing.error.session_unavailable"),
     "The pairing session is no longer available"},
    {Text::PairingCertificateChanged, QT_TRANSLATE_NOOP("RelayDesk", "pairing.certificate_changed"),
     "The other device certificate changed. Automatic connection was stopped."},
    {Text::PairingTooManyAttempts, QT_TRANSLATE_NOOP("RelayDesk", "pairing.too_many_attempts"),
     "Too many incorrect attempts. Try again later."},
    {Text::PairingNotDirect, QT_TRANSLATE_NOOP("RelayDesk", "pairing.not_direct"),
     "The devices cannot connect directly. Try a manual address."},
    {Text::PairingSuccess, QT_TRANSLATE_NOOP("RelayDesk", "pairing.success"), "Device paired"},
    {Text::PairingFingerprintLabel, QT_TRANSLATE_NOOP("RelayDesk", "pairing.fingerprint.label"),
     "Certificate fingerprint"},
    {Text::PairingFingerprintUnavailable, QT_TRANSLATE_NOOP("RelayDesk", "pairing.fingerprint.unavailable"),
     "Fingerprint unavailable"},
    {Text::PairingAttemptsRemaining, QT_TRANSLATE_N_NOOP("RelayDesk", "pairing.attempts_remaining"),
     "%n attempt remaining", "%n attempts remaining"},
    {Text::PairingExpiresAt, QT_TRANSLATE_NOOP("RelayDesk", "pairing.expires_at"), "Expires: %1"},
    {Text::PermissionsBannerAttentionTitle, QT_TRANSLATE_NOOP("RelayDesk", "permissions.banner.attention_title"),
     "Permission needed"},
    {Text::PermissionsBannerUnknownTitle, QT_TRANSLATE_NOOP("RelayDesk", "permissions.banner.unknown_title"),
     "Permission status not checked"},
    {Text::PermissionsBannerReadyTitle, QT_TRANSLATE_NOOP("RelayDesk", "permissions.banner.ready_title"),
     "Permissions ready"},
    {Text::PermissionsBannerReadyMessage, QT_TRANSLATE_NOOP("RelayDesk", "permissions.banner.ready_message"),
     "All required system permissions are ready."},
    {Text::PermissionsDetailsTitle, QT_TRANSLATE_NOOP("RelayDesk", "permissions.details.title"), "Permissions"},
    {Text::PermissionsKindWindowsFirewall, QT_TRANSLATE_NOOP("RelayDesk", "permissions.kind.windows_firewall"),
     "Windows Firewall"},
    {Text::PermissionsKindWindowsPort, QT_TRANSLATE_NOOP("RelayDesk", "permissions.kind.windows_port"),
     "Local network port"},
    {Text::PermissionsKindMacLocalNetwork, QT_TRANSLATE_NOOP("RelayDesk", "permissions.kind.macos_local_network"),
     "Local Network"},
    {Text::PermissionsKindMacAccessibility, QT_TRANSLATE_NOOP("RelayDesk", "permissions.kind.macos_accessibility"),
     "Accessibility"},
    {Text::PermissionsKindMacInputMonitoring, QT_TRANSLATE_NOOP("RelayDesk", "permissions.kind.macos_input_monitoring"),
     "Input Monitoring"},
    {Text::PermissionsStatusUnknown, QT_TRANSLATE_NOOP("RelayDesk", "permissions.status.unknown"), "Not checked"},
    {Text::PermissionsStatusNotRequired, QT_TRANSLATE_NOOP("RelayDesk", "permissions.status.not_required"),
     "Not required"},
    {Text::PermissionsStatusGranted, QT_TRANSLATE_NOOP("RelayDesk", "permissions.status.granted"), "Allowed"},
    {Text::PermissionsStatusDenied, QT_TRANSLATE_NOOP("RelayDesk", "permissions.status.denied"), "Blocked"},
    {Text::PermissionsStatusNeedsAction, QT_TRANSLATE_NOOP("RelayDesk", "permissions.status.needs_action"),
     "Action needed"},
    {Text::PermissionsMessageUnknown, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.unknown"),
     "RelayDesk has not received a permission check yet."},
    {Text::PermissionsMessageProbeUnavailable, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.probe_unavailable"),
     "RelayDesk could not check this setting. Try again."},
    {Text::PermissionsMessageReview, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.review"),
     "Review this system setting to keep local device connections working."},
    {Text::PermissionsMessageWindowsFirewall, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.windows_firewall"),
     "Allow RelayDesk through Windows Firewall on private networks."},
    {Text::PermissionsMessageWindowsPort, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.windows_port"),
     "RelayDesk cannot listen on its local network port. Review firewall and port settings."},
    {Text::PermissionsMessageMacLocalNetwork, QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.macos_local_network"),
     "Allow Local Network access so RelayDesk can find nearby devices."},
    {Text::PermissionsMessageMacAccessibility,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.macos_accessibility"),
     "Allow Accessibility so RelayDesk can control keyboard and pointer input."},
    {Text::PermissionsMessageMacInputMonitoring,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.message.macos_input_monitoring"),
     "Allow Input Monitoring when macOS requires it for shared input."},
    {Text::PermissionsPurposeWindowsFirewall, QT_TRANSLATE_NOOP("RelayDesk", "permissions.purpose.windows_firewall"),
     "Allows trusted devices to reach RelayDesk on private networks."},
    {Text::PermissionsPurposeWindowsPort, QT_TRANSLATE_NOOP("RelayDesk", "permissions.purpose.windows_port"),
     "Keeps the RelayDesk listener available on the selected local port."},
    {Text::PermissionsPurposeMacLocalNetwork, QT_TRANSLATE_NOOP("RelayDesk", "permissions.purpose.macos_local_network"),
     "Find and connect to nearby devices on your local network."},
    {Text::PermissionsPurposeMacAccessibility,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.purpose.macos_accessibility"),
     "Control keyboard and pointer input on this Mac."},
    {Text::PermissionsPurposeMacInputMonitoring,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.purpose.macos_input_monitoring"),
     "Read global keyboard and pointer input to share with another device."},
    {Text::PermissionsAffectedNetwork, QT_TRANSLATE_NOOP("RelayDesk", "permissions.affected.network"),
     "Device discovery, incoming connections, and file transfer"},
    {Text::PermissionsAffectedMacLocalNetwork,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.affected.macos_local_network"),
     "Nearby discovery and direct local connections"},
    {Text::PermissionsAffectedMacAccessibility,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.affected.macos_accessibility"), "Input control on this Mac"},
    {Text::PermissionsAffectedMacInputMonitoring,
     QT_TRANSLATE_NOOP("RelayDesk", "permissions.affected.macos_input_monitoring"), "Sharing input from this Mac"},
    {Text::PermissionsActionOpenSettings, QT_TRANSLATE_NOOP("RelayDesk", "permissions.action.open_settings"),
     "Open settings"},
    {Text::PermissionsActionViewDetails, QT_TRANSLATE_NOOP("RelayDesk", "permissions.action.view_details"),
     "View permission details"},
    {Text::TransferTitle, QT_TRANSLATE_NOOP("RelayDesk", "transfer.title"), "Transfers"},
    {Text::TransferEmpty, QT_TRANSLATE_NOOP("RelayDesk", "transfer.empty"), "Transfers will appear here"},
    {Text::TransferDirectionSending, QT_TRANSLATE_NOOP("RelayDesk", "transfer.direction.sending"), "Sending"},
    {Text::TransferDirectionReceiving, QT_TRANSLATE_NOOP("RelayDesk", "transfer.direction.receiving"), "Receiving"},
    {Text::TransferStatePreparing, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.preparing"), "Preparing"},
    {Text::TransferStateAwaitingConfirmation, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.awaiting_confirmation"),
     "Waiting for confirmation"},
    {Text::TransferStateQueued, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.queued"), "Queued"},
    {Text::TransferStateTransferring, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.transferring"), "Transferring"},
    {Text::TransferStatePaused, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.paused"), "Paused"},
    {Text::TransferStateInterrupted, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.interrupted"),
     "Connection lost, waiting to resume"},
    {Text::TransferStateResuming, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.resuming"), "Resuming"},
    {Text::TransferStateVerifying, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.verifying"), "Verifying"},
    {Text::TransferStateSaving, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.saving"), "Saving"},
    {Text::TransferStateCompleted, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.completed"), "Completed"},
    {Text::TransferStateRejected, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.rejected"), "Rejected"},
    {Text::TransferStateCanceling, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.canceling"), "Canceling"},
    {Text::TransferStateCanceled, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.canceled"), "Canceled"},
    {Text::TransferStateFailed, QT_TRANSLATE_NOOP("RelayDesk", "transfer.state.failed"), "Failed"},
    {Text::TransferActionPause, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.pause"), "Pause"},
    {Text::TransferActionResume, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.resume"), "Resume"},
    {Text::TransferActionCancel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.cancel"), "Cancel"},
    {Text::TransferActionRetry, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.retry"), "Retry"},
    {Text::TransferActionOpenFolder, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.open_folder"), "Open folder"},
    {Text::TransferActionOpenFile, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.open_file"), "Open file"},
    {Text::TransferActionDetails, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.details"), "Details"},
    {Text::TransferActionClose, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.close"), "Close"},
    {Text::TransferFeedbackOpenFailed, QT_TRANSLATE_NOOP("RelayDesk", "transfer.feedback.open_failed"),
     "Could not open the completed item. Try again."},
    {Text::TransferFeedbackHistoryUnavailable,
     QT_TRANSLATE_NOOP("RelayDesk", "transfer.feedback.history_unavailable"),
     "Transfer history could not be updated. Try again."},
    {Text::TransferHistoryDetailsTitle, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.details_title"),
     "Transfer details"},
    {Text::TransferHistoryNameLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.name_label"), "Transfer"},
    {Text::TransferHistoryPeerLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.peer_label"), "Device"},
    {Text::TransferHistoryDirectionLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.direction_label"),
     "Direction"},
    {Text::TransferHistoryStatusLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.status_label"), "Status"},
    {Text::TransferHistoryItemsLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.items_label"), "Items"},
    {Text::TransferHistorySizeLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.size_label"), "Size"},
    {Text::TransferHistoryStartedLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.started_label"), "Started"},
    {Text::TransferHistoryFinishedLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.finished_label"), "Finished"},
    {Text::TransferHistoryDurationLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.duration_label"), "Duration"},
    {Text::TransferHistoryErrorLabel, QT_TRANSLATE_NOOP("RelayDesk", "transfer.history.error_label"), "Error"},
    {Text::TransferHistoryItems, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.history.items"), "%1 item", "%1 items"},
    {Text::TransferHistoryDurationSeconds, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.history.duration_seconds"),
     "%1 second", "%1 seconds"},
    {Text::TransferHistoryDurationMinutes, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.history.duration_minutes"),
     "%1 minute", "%1 minutes"},
    {Text::TransferHistoryDurationHours, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.history.duration_hours"), "%1 hour",
     "%1 hours"},
    {Text::TransferActionAccept, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.accept"), "Accept"},
    {Text::TransferActionReject, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.reject"), "Reject"},
    {Text::TransferActionDismiss, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.dismiss"), "Dismiss"},
    {Text::TransferActionChangeSettings, QT_TRANSLATE_NOOP("RelayDesk", "transfer.action.change_settings"),
     "Change settings"},
    {Text::TransferErrorDiskFull, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.disk_full"), "Not enough disk space"},
    {Text::TransferErrorUnsafePath, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.unsafe_path"),
     "The received path is not safe"},
    {Text::TransferErrorUnreadable, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.unreadable"),
     "One or more items cannot be read"},
    {Text::TransferErrorConnectionLost, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.connection_lost"),
     "Connection lost"},
    {Text::TransferErrorChecksumMismatch, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.checksum_mismatch"),
     "File verification failed"},
    {Text::TransferErrorUnknown, QT_TRANSLATE_NOOP("RelayDesk", "transfer.error.unknown"),
     "Transfer failed. Try again."},
    {Text::TransferProgressBytes, QT_TRANSLATE_NOOP("RelayDesk", "transfer.progress.bytes"), "%1 of %2"},
    {Text::TransferProgressItems, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.progress.items"), "%1 of %n item",
     "%1 of %n items"},
    {Text::TransferSpeed, QT_TRANSLATE_NOOP("RelayDesk", "transfer.speed"), "%1/s"},
    {Text::TransferSpeedUnknown, QT_TRANSLATE_NOOP("RelayDesk", "transfer.speed.unknown"), "Speed unavailable"},
    {Text::TransferEtaUnknown, QT_TRANSLATE_NOOP("RelayDesk", "transfer.eta.unknown"), "Calculating time remaining"},
    {Text::TransferEtaSeconds, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.eta.seconds"), "%n second remaining",
     "%n seconds remaining"},
    {Text::TransferEtaMinutes, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.eta.minutes"), "%n minute remaining",
     "%n minutes remaining"},
    {Text::TransferEtaHours, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.eta.hours"), "%n hour remaining",
     "%n hours remaining"},
    {Text::TransferEtaDays, QT_TRANSLATE_N_NOOP("RelayDesk", "transfer.eta.days"), "%n day remaining",
     "%n days remaining"},
    {Text::TransferEtaLong, QT_TRANSLATE_NOOP("RelayDesk", "transfer.eta.long"), "99 days or more remaining"},
    {Text::TransferAccessibleSummary, QT_TRANSLATE_NOOP("RelayDesk", "transfer.accessible.summary"),
     "%1, %2, %3, %4, %5"},
    {Text::TransferNotificationCompleted, QT_TRANSLATE_NOOP("RelayDesk", "transfer.notification.completed"),
     "Transfer completed"},
    {Text::TransferNotificationRejected, QT_TRANSLATE_NOOP("RelayDesk", "transfer.notification.rejected"),
     "Transfer rejected"},
    {Text::TransferNotificationCanceled, QT_TRANSLATE_NOOP("RelayDesk", "transfer.notification.canceled"),
     "Transfer canceled"},
    {Text::TransferNotificationFailed, QT_TRANSLATE_NOOP("RelayDesk", "transfer.notification.failed"),
     "Transfer failed"},
    {Text::TransferNotificationBody, QT_TRANSLATE_NOOP("RelayDesk", "transfer.notification.body"), "%1 · %2"},
    {Text::TransferIncomingWantsToSend, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.wants_to_send"),
     "%1 wants to send"},
    {Text::TransferIncomingSaveTo, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.save_to"), "Save to: %1"},
    {Text::TransferIncomingAutoRename, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.auto_rename"),
     "Conflict: auto rename"},
    {Text::TransferIncomingAsk, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.ask"),
     "Conflict: ask when a file already exists"},
    {Text::TransferIncomingAlwaysAccept, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.always_accept"),
     "Always accept files from this device"},
    {Text::TransferIncomingUnknownDevice, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.unknown_device"),
     "Unknown device"},
    {Text::TransferIncomingPairFirst, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.pair_first"),
     "Pair this device before receiving files"},
    {Text::TransferIncomingExpired, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.expired"),
     "This transfer request expired"},
    {Text::TransferIncomingDestinationUnavailable,
     QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.destination_unavailable"),
     "Choose a valid receive folder in settings"},
    {Text::TransferIncomingInvalid, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.invalid"),
     "The incoming transfer request is invalid"},
    {Text::TransferIncomingBusy, QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.busy"),
     "Another transfer request is waiting"},
    {Text::TransferIncomingDecisionUnavailable,
     QT_TRANSLATE_NOOP("RelayDesk", "transfer.incoming.decision_unavailable"),
     "This transfer request cannot be accepted right now"},
    {Text::TransferConflictTitle, QT_TRANSLATE_NOOP("RelayDesk", "transfer.conflict.title"), "File already exists"},
    {Text::TransferConflictOverwrite, QT_TRANSLATE_NOOP("RelayDesk", "transfer.conflict.overwrite"), "Replace"},
    {Text::TransferConflictAutoRename, QT_TRANSLATE_NOOP("RelayDesk", "transfer.conflict.auto_rename"), "Keep both"},
    {Text::TransferConflictSkip, QT_TRANSLATE_NOOP("RelayDesk", "transfer.conflict.skip"), "Skip"},
    {Text::TransferConflictCancelTransportFailed,
     QT_TRANSLATE_NOOP("RelayDesk", "transfer.conflict.cancel_transport_failed"),
     "Could not cancel this transfer. Check the connection and try again."},
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
    {Text::AboutDescription, QT_TRANSLATE_NOOP("RelayDesk", "about.description"),
     "Local network keyboard, mouse, clipboard, and file sharing"},
    {Text::AboutDiagnostics, QT_TRANSLATE_NOOP("RelayDesk", "about.diagnostics"), "Diagnostics"},
    {Text::SettingsTransferReceiveFolder, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.receive_folder"),
     "Receive folder"},
    {Text::SettingsTransferChooseFolder, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.choose_folder"),
     "Choose folder"},
    {Text::SettingsTransferIncomingPolicy, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.incoming_policy"),
     "Incoming files"},
    {Text::SettingsTransferAskEveryTime, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.ask_every_time"),
     "Ask every time"},
    {Text::SettingsTransferAutoAcceptTrusted, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.auto_accept_trusted"),
     "Automatically accept from trusted devices"},
    {Text::SettingsTransferConflictPolicy, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.conflict_policy"),
     "When a file already exists"},
    {Text::SettingsTransferSaveFailed, QT_TRANSLATE_NOOP("RelayDesk", "settings.transfer.save_failed"),
     "Could not save file-transfer settings: %1"},
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
