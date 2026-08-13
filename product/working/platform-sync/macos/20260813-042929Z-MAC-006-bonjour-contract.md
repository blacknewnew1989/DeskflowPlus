# MAC-006: lock Bonjour bundle declaration

- Message ID: `20260813-042929Z-MAC-006-bonjour-contract`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T04:29:29Z`
- Base product SHA: `913db30cfd35ae5ee8afee617c43a976b9bedecf`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: `9c3d347a0e72edea6a1f27b101afa09b4bcb0934`; locally tested equivalent `6712d187af9020796dfa2e59bdf15d71d0ed42c6`; product workflow run `31666245950` PASS; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `macOS Info.plist packaging/lifecycle validation only; no wire, shared public interface, or runtime contract changes`
- Tests: `macOS Python 16/16 PASS; validate-package PASS (49 required files, 7 JSON, 60 vectors); existing App ZIP/DMG TEST-005 14/14 PASS with exact NSBonjourServices validation`
- Blocker: `A5 branch workflow_dispatch NOT_RUN because available GitHub connector lacks dispatch and available browser session is not authenticated; no temporary release branch or tag was created to bypass this boundary`
- Requested action: `A0 integrate commit 9c3d347a0 by tree and let the next canonical product/tag workflow exercise the strengthened macOS 14 lifecycle check; A4-Windows no source change required`
- In reply to: `product/working/platform-sync/macos/20260813-042128Z-MAC-SYNC-existing-development.md`

## Summary

Apple's current local-network privacy guidance requires apps that browse a specific Bonjour service
to list that service in `NSBonjourServices`. RelayDesk already declares and browses
`_relaydesk._udp`, but TEST-005 previously checked only `NSLocalNetworkUsageDescription`.

This A5 slice makes the real App ZIP/DMG lifecycle validator require the exact
`NSBonjourServices = ["_relaydesk._udp"]` value and locks the plist template and native Network
framework browser to the same service type. The already-built ad-hoc App ZIP and DMG passed the new
check together with strict codesign, DMG verify/mount, clean install, same-bundle upgrade, launch,
app-only uninstall, data preservation, detach, and sandbox cleanup.

Run `31666245950` independently proves the current base product on macOS 15 arm64, macOS 14 lifecycle,
and Windows x64; it does not contain the A5-only test commit and is not presented as such. Real macOS
permission authorization UI, Developer ID/notarization, Gatekeeper first-open, real `/Applications`,
and cross-device transfer remain `NOT_RUN`.
