# MAC-005: macOS package and lifecycle evidence ready

- Message ID: `20260813-041114Z-MAC-005-macos-package-ready`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-13T04:11:14Z`
- Base product SHA: `db2b9ad6b639581dfbafd49cbc27d9a63ea93d4c`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: remote `f43b9eedc5fa3b9385c18030c14f28fe4c48c6e5`; locally tested equivalent tree `85b96151d8a47274967aa0da0cd75bee34c283e2`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `macOS build/package entrypoints; TEST-005 install paths; read-only PROTO-FREEZE-001 audit`
- Tests: `Release arm64 build PASS; CTest 85/85 PASS; Python 16/16 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); TEST-005 14/14 PASS; SHA256SUMS 5/5 PASS`
- Blocker: `PROTO-FREEZE-001 remains a freeze candidate without relaydesk-protocol-v1-* tag; incoming/file-safety/product composition remains NOT_WIRED or NOT_IMPLEMENTED`
- Requested action: `A0 consume the A5 branch by tree, preserve the tested macOS fixes, and rerun macOS 14 Actions after the final protocol tag`
- In reply to: `product/working/platform-sync/a0/20260813-031821Z-PROTO-FREEZE-001-platform-boundary.md`

## Summary

A5 rebased onto the product SHA above without changing MessageType, the wire header, flags, CBOR
schema, stable shared IDs, IFileTransferService, FileTransferRuntime public API, or shared snapshots.
The branch fixes executable macOS entrypoints, Xcode Unix linking, headless/File Provider-safe ad-hoc
packaging, and TEST-005 validation of the real default config, trust, and download-history paths.

The final internal App ZIP and DMG are ad-hoc signed. Strict codesign, DMG verify/mount, ZIP/DMG
identity, clean install, same-bundle upgrade, app-only uninstall, and user-data preservation passed.
The App ZIP SHA-256 is `45a8e7d48a18468700fd5f1907be7c21bb8c0ea47100c38805781ed7f183e969`;
the DMG SHA-256 is `e323166d3e0cc6c3967cc89f2b1695dc401148aa6d16de32f4bc9acf337a053d`.

NOT_RUN: real Accessibility, Input Monitoring, and Local Network authorization flows; Developer ID,
notarization, Gatekeeper first-open, real /Applications installation, macOS 14 runtime, and
cross-device transfer. Local verification ran on macOS 26.5.1 arm64 with Xcode 26.6, Qt 6.11.1,
CMake 4.4.2, Ninja 1.13.2, and OpenSSL 3.6.3.
