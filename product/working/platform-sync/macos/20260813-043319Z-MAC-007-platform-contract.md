# MAC-007: lock macOS artifact platform contract

- Message ID: `20260813-043319Z-MAC-007-platform-contract`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T04:33:19Z`
- Base product SHA: `913db30cfd35ae5ee8afee617c43a976b9bedecf`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: `cea9026ecc19e329698952ccf8da6cab7f98b7f9`; locally tested equivalent `753646abf`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `TEST-005 macOS artifact architecture/deployment-target validation only; no shared or wire changes`
- Tests: `macOS Python 18/18 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); real App ZIP/DMG TEST-005 16/16 checks PASS including arm64 and minos 14.0 for GUI/core`
- Blocker: `A5 branch workflow_dispatch remains NOT_RUN for the connector/browser boundary reported in MAC-006; canonical product run 31666245950 is PASS but predates this A5-only test commit`
- Requested action: `A0 integrate this tree and let the next canonical product/tag workflow enforce the new macOS 14 artifact checks; A4-Windows no source change required`
- In reply to: `product/working/platform-sync/macos/20260813-042929Z-MAC-006-bonjour-contract.md`

## Summary

TEST-005 now uses `/usr/bin/lipo -archs` and `xcrun vtool -show-build` on both the GUI and
`deskflow-core` executables from the App ZIP and DMG. The P0 `macos-arm64` package must be exactly
`arm64`, use platform `MACOS`, and carry `LC_BUILD_VERSION minos 14.0`.

Two negative unit tests prove that x86_64 and an incorrect minimum macOS version are rejected. The
current ad-hoc App ZIP and DMG passed both new platform checks as well as strict codesign, DMG
verification/mount, clean install, launch, same-bundle upgrade, app-only uninstall, user-data
preservation, detach, and cleanup.

Real privacy authorization, Developer ID/notarization, Gatekeeper first-open, real `/Applications`,
and cross-device transfer remain `NOT_RUN`.
