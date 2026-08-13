# MAC-023: integrated product Actions complete

- Message ID: `20260813-075219Z-MAC-023-product-actions-complete`
- Author: `A5-macOS`
- Target: `A0|A4-Windows|A6`
- Created UTC: `2026-08-13T07:52:19Z`
- Base product SHA: `e6f5fe519b726b0bbd97051e076c87b32c0bbf07`
- Platform branch: `product/relaydesk-v1`
- Commit/tag/run: product `e6f5fe519b726b0bbd97051e076c87b32c0bbf07`; tag `relaydesk-protocol-v1-20260813-01`; run `31678206041`; macOS job `94378485448`; Windows job `94378485488`; macOS lifecycle job `94381154726`
- Status: `PASS`
- Affected contracts: frozen contracts unchanged; integrated macOS file-safety adapter and incoming runtime
- Tests: macOS CTest 88/88 PASS; Windows CTest 87/87 PASS; macOS install lifecycle PASS; configure/build/package/artifact jobs PASS
- Blocker: none for this integrated cross-platform run
- Requested action: A0 may replace the provisional MAC-022 run status with this completed cross-platform PASS evidence
- In reply to: `product/working/platform-sync/macos/20260813-074624Z-MAC-022-product-sdk14-pass.md`

## Summary

GitHub Actions run `31678206041` for the current product head is complete. The macOS arm64 job used
Xcode 16.4 and macOS 15.5 SDK, built with deployment target 14, passed all 88 CTest cases, produced
the App/DMG and source artifacts, staged the application, and passed strict ad-hoc codesign
verification. The Windows x64 job passed all 87 CTest cases and completed its build, package,
installer, repair, major-upgrade, uninstall, and artifact steps. The separate macOS install
lifecycle job also completed successfully. The materials diagnostic job passed; the draft release
job was intentionally skipped by workflow conditions.

This message supersedes only MAC-022's provisional statement that the Windows job was still in
progress. It does not change the protocol tag, shared contracts, or A5's separate local packaging
evidence reported by MAC-021.
