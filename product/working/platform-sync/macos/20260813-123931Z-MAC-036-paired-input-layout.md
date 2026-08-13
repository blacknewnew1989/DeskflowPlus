# MAC-036: 配对设备自动进入键鼠屏幕布局

- Message ID: `20260813-123931Z-MAC-036-paired-input-layout`
- Author: `A5-macOS`
- Target: `A0|A4-Windows`
- Created UTC: `2026-08-13T12:39:31Z`
- Base product SHA: `993c6a6e6a9dc5807839c2cc3f84d130c1903e8d`
- Platform branch: `agent/a5/macos-translation-build-hygiene`
- Commit/tag/run: remote commit `8ec748682f02e4916a5005a92c9c26a66c8a54a3`; local equivalent commit `89b2f725c6de882856944ad6b76d49ed9ee64091`; identical final tree `02bb0be139a9cf52d87745d9c145b50d774954cf`; freeze tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: `none`; frozen wire/shared contracts unchanged
- Tests: `macOS arm64 build PASS；CTest 90/90 PASS；pairing/device/layout targeted CTest 6/6 PASS；package validation 49 files/60 vectors PASS；macOS packaging Python 21/21 PASS；TEST-005 lifecycle 16/16 PASS`
- Blocker: `none for integration；Windows physical pairing/input acceptance requires A4`
- Requested action: `A0 integrate remote commit 8ec748682；A4 build the same tree on Windows and verify that a trusted input-capable peer is inserted into ServerConfig exactly once after pairing or rediscovery, then run a physical Windows↔macOS mouse/keyboard acceptance`
- In reply to: `user physical acceptance: paired Windows device was absent from the server configuration and no mouse/keyboard connection was possible`

## Summary

The root cause was a missing composition bridge: RelayDesk pairing committed the peer to
`TrustedDeviceStore` and updated `DeviceHomeModel`, but never inserted the peer's Deskflow computer
name into `ServerConfig`. Consequently the pairing UI could report success while the Deskflow server
layout remained empty and rejected any practical input-sharing setup.

The fix adds a shared GUI helper that consumes the existing `DeviceSnapshot` without changing any
frozen interface or wire bytes. A trusted peer advertising the input capability is inserted through
the existing `ServerConfig::addClient()` path after pairing completion and after trusted-device
rediscovery. The update is idempotent, persists the existing layout, restarts an already-running
server, validates the exact advertised Deskflow computer name, preserves external configurations,
and refuses to overfill the layout.

## Windows verification request

1. Build remote commit `8ec748682f02e4916a5005a92c9c26a66c8a54a3` or A0's integrated descendant.
2. Pair a Windows and macOS device; do not manually add the peer in the Deskflow screen editor.
3. Confirm the trusted peer's advertised `displayName` appears exactly once in the server layout
   immediately after pairing and still appears after both applications restart and rediscover.
4. Configure one machine as server and the other as client, connect over TCP 24800, and verify
   bidirectional focus transition plus keyboard/mouse input according to the selected server role.
5. Confirm an active external server config is not rewritten and an invalid/spaced computer name is
   rejected with a diagnostic rather than silently creating an unusable layout entry.

The pairing code itself remains symmetric and the two displayed verification codes may differ by
device role; the acceptance criterion is that each side confirms its locally displayed code and both
finish in the trusted state. This layout fix is independent of the pairing-code presentation.
