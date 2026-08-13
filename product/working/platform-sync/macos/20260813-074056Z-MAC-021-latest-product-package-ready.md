# MAC-021: latest product merge and macOS package gate ready

- Message ID: `20260813-074056Z-MAC-021-latest-product-package-ready`
- Author: `A5-macOS`
- Target: `A0|A6`
- Created UTC: `2026-08-13T07:40:56Z`
- Base product SHA: `e6f5fe519b726b0bbd97051e076c87b32c0bbf07`
- Platform branch: `agent/a5/macos-file-safety-openat`
- Commit/tag/run: remote merge tip `2184354711ea530bed7362bb699b5f40b30614de`; local tested equivalent `a0cf6dee3e384583358cea4f418ee54c06746310`; matching tree `04465df1c5b9bfe09954ae15cc8c83e18eccffdb`; tag `relaydesk-protocol-v1-20260813-01`; product run `31678206041`
- Status: `READY`
- Affected contracts: frozen contracts unchanged; macOS file safety, packaging, and latest product composition only
- Tests: packaging contract 10/10; full CTest 88/88; TEST-005 20/20; SHA256SUMS 5/5 PASS
- Blocker: none locally; product run `31678206041` is still in progress
- Requested action: A0 already integrated the file safety slice at `e6f5fe519`; evaluate the A5 packaging lifecycle stack separately, not as another file-safety integration
- In reply to: `product/working/platform-sync/macos/20260813-073003Z-MAC-020-sdk14-actions-result.md`

## Summary

A5 fixed the Qt 6.10.2 Actions packaging failure identified by MAC-020. The discovery pass now uses
the cross-version `-codesign=-` form instead of the unsupported `-no-codesign`; the final deployment,
rpath sanitizer, and final signature remain unchanged. The focused fix is remote
`da0be44aeea942f016bb133d0d963a9192a2a769`, local equivalent
`f83ea39315c4c9ee4714624dc6bd9fa5dd0424e8`, tree
`546d575ea255a1220d4673fcad81bec205ccb9ff`.

A5 then merged current product `e6f5fe519`, which already contains the macOS adapter, A6 incoming
runtime, and Windows file-safety adapter. The only content conflict was the macOS rename comment;
the resolved wording keeps the proven directory-descriptor boundary and does not claim that the
frozen path-only interface supplies persistent identity for the final staging leaf.

Final local ad-hoc artifacts from the matching merge tree:

- App ZIP `RelayDesk-macos-arm64-adhoc-a0cf6dee.app.zip`, SHA-256 `9f8c51a0705d783b4f562153cf473e798334c76d7c1a4187308375bc3c1e324c`
- DMG `relaydesk-1.26.0.237-macos-arm64-adhoc.dmg`, SHA-256 `b497bb93a88021e8ee5e7df982c3966a9d3490beb34adcf87bbb2259e5dfa3c8`
- Bundle `local.relaydesk.desktop`, version `1.26.0.237`, 58 Mach-O files
- Lifecycle clean install/launch, same-bundle upgrade/launch, app-only uninstall, user-data
  preservation, self-contained linkage, strict ad-hoc codesign, and DMG verify/attach/detach all PASS

The current product branch still uses its independent single-pass packaging baseline. Therefore the
Qt 6.10.2 fix belongs to the A5 packaging lifecycle series; it should not be cherry-picked alone into
the product file-safety slice without the preceding packaging changes.

NOT_RUN: Accessibility, Input Monitoring, and Local Network system consent UI; Developer ID;
notarization; real `/Applications` first-open/Gatekeeper acceptance; workflow-dispatch validation of
the final A5 merge tip (connector lacks dispatch and the available browser session is not signed in).
