# MAC-020: SDK 14 Actions compile and test result

- Message ID: `20260813-073003Z-MAC-020-sdk14-actions-result`
- Author: `A5-macOS`
- Target: `A0|A6`
- Created UTC: `2026-08-13T07:30:03Z`
- Base product SHA: `da75f57c2e1bb873fdbe8b26fe3a102bd6929ea9`
- Platform branch: `agent/a5/macos-file-safety-openat`
- Commit/tag/run: integration tip `29ec51c444a51b31ff6cfe74d6a9eedbca11936b`; SDK fix equivalent `9b22fc8570c4a39a78f0493e3a1a947c4e71118c`; run `31677194012`; macOS job `94374613965`; artifact `9172175111`
- Status: `READY`
- Affected contracts: frozen `IPlatformFileSafety` consumed unchanged; macOS adapter only
- Tests: Xcode 16.4/macOS 15.5 SDK Build PASS; CTest 86/86 PASS; `RelayDeskPlatformFileSafetyContractTests` PASS; `RelayDeskMacFileSafetyTests` PASS
- Blocker: adapter none; run package step failed on pre-existing `macdeployqt -no-codesign` incompatibility with Actions Qt 6.10.2
- Requested action: A0 integrate `29ec51c44`; record run compile/test PASS and package FAIL separately; use product packaging baseline for release artifacts
- In reply to: `product/working/platform-sync/macos/20260813-072622Z-MAC-019-file-safety-openat-ready.md`

## Summary

The supported SDK compile regression is fixed: macOS Build completed successfully and the full suite
reported 100% (86/86) PASS in 13.87 seconds. `RelayDeskMacFileSafetyTests` was test 62 and the frozen
platform contract was test 60; both passed on the real macOS runner.

The macOS job is not an overall success and must not be reported as one. Its later package step failed
because the older A5 packaging branch calls `macdeployqt -no-codesign`, which the workflow's Qt 6.10.2
does not accept. That packaging code predates and is outside the file-safety adapter slice; the product
tag workflow already supplies the successful packaging baseline. The uploaded diagnostic artifact
contains the CTest evidence. The remaining Windows job was cancelled after macOS evidence was complete.
