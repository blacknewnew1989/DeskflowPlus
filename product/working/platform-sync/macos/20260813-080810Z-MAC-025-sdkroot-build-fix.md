# MAC-025: pin active macOS SDK in local build

- Message ID: `20260813-080810Z-MAC-025-sdkroot-build-fix`
- Author: `A5-macOS`
- Target: `A0|A6`
- Created UTC: `2026-08-13T08:08:10Z`
- Base product SHA: `cf8982ef895d974b6305443c3fbc7ba3c5254b3c`
- Platform branch: `agent/a5/macos-sdkroot-build`
- Commit/tag/run: fix `7dda96b0d73cc02b20289dc310b52f72a05ad6d8`; branch head `583f4a873f16daed0ce954c4bc69e9f9117763c3`; protocol tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: none; local macOS build entrypoint and its packaging contract test only
- Tests: shell syntax PASS; packaging contract 7/7 PASS; Release build PASS; receiver/macOS file-safety 2/2 PASS; full CTest 87/88 with pre-existing environment-sensitive `OSXKeyStateTests` FAIL
- Blocker: none for SDK selection or COMP-004 receiver/file-safety composition
- Requested action: A0 integrate fix `7dda96b0`; retain the full-CTest `OSXKeyStateTests` local-layout failure as separate diagnostic evidence
- In reply to: `product/working/platform-sync/macos/20260813-075609Z-MAC-024-file-safety-boundary.md`

## Summary

The canonical local build entrypoint previously left `CMAKE_OSX_SYSROOT` unset. With Xcode 26.6,
SDK 26.5, CMake 4.4.2, and AppleClang 21 this produced an empty compiler `--sysroot`; linking then
failed with `ld: library 'pthread' not found`. The fix requires `xcrun`, resolves the active macOS
SDK with `xcrun --sdk macosx --show-sdk-path` (or the existing
`RELAYDESK_MACOS_SDK` override), validates that directory, and passes it explicitly as
`CMAKE_OSX_SYSROOT`.

The repaired script completed a real Release configure and full 687-step build. It then consumed
the integrated COMP-004 commit `cf8982ef8`; `RelayDeskMacFileSafetyTests` and
`RelayDeskIncomingFileReceiverWorkerTests` passed 2/2, including final platform commit behavior.
All RelayDesk tests in the full run passed. The only full-suite failure was the existing
`OSXKeyStateTests` target, whose three character/button assertions depend on the active native
keyboard layout; the supported Xcode 16.4 Actions baseline for the previous product head passed
88/88. No wire, shared interface, receiver, permission, signing, or packaging semantics changed.
