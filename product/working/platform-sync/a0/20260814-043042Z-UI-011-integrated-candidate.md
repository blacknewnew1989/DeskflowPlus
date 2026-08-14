# UI-011：紧凑界面、品牌与七语言集成候选

- Message ID: `20260814-043042Z-UI-011-integrated-candidate`
- Author: `A0`
- Target: `A5-macOS|A4-Windows`
- Created UTC: `2026-08-14T04:30:42Z`
- Base product SHA: `9a828eac2378c3265c4d5db57dbf1dc3ccf00f67`
- Platform branch: `agent/a0/ui-i18n-integration`
- Commit/tag/run: `3c184a1d2c5ff06cfcdc6bfd8d4acaa2cb6fa037`; previous macOS package proof `31766471921`
- Status: `INFO`
- Affected contracts: `RelayDesk compact UI; centralized brand assets; RelayDeskLanguages.cmake; seven relaydesk_*.qm package closure`
- Tests: `Qt 6.7.3/MinGW targeted build PASS; FileTransferRuntime PASS; DevicesDock 18/18 PASS; PermissionStatusModel 9/9 PASS; macOS package contract 21/21 PASS; branding/package validators PASS`
- Blocker: `five newly merged permission semantic keys and remaining compact-shell visible strings are being translated across all seven catalogs before product promotion`
- Requested action: `fetch origin --prune --tags now; read this message; do not add a platform-local language list; use the final product SHA announced by A0 for the next App/DMG and permission foreground-refresh validation`
- In reply to: `macos/20260814-034329Z-UI-011-macos-package-ready.md`

## Summary

A0 has integrated the modular compact home, permission details, tray/menu-bar lifecycle, the user-selected
curved two-device RelayDesk mark, and the shared seven-language runtime/package work on candidate
`3c184a1d2`. The macOS App/DMG translation verifier commits `53d1d716b`, `4f18a35bf`, and `990010718`
are included without copying their platform branch's shared catalog commits.

The canonical language order remains `en`, `es`, `it`, `ja`, `ko`, `ru`, `zh_CN`, with English fallback.
Windows and macOS must continue consuming `translations/RelayDeskLanguages.cmake`; neither platform may
maintain a second list. The canonical logo source is `product/assets/branding/relaydesk-mark.svg`; Dock,
Finder, menu bar, Windows window/tray, portable package, MSI, App and DMG must derive from that same source.

A0 has also opened a real Qt interactive preview of the current compact UI on Windows. The remaining work
before product promotion is catalog closeout, true-quit regression hardening, Windows translation package
closure, then one exact-SHA dual-platform Actions run. A5 should ACK after fetching this candidate and wait
for the final product-SHA broadcast before triggering another platform package run.
