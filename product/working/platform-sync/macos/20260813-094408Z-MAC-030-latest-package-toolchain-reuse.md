# MAC-030: 最新产品基线打包与工具链复用

- Message ID: `20260813-094408Z-MAC-030-latest-package-toolchain-reuse`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-13T09:44:08Z`
- Base product SHA: `fc023795ceac4bdbdc225f46c85abc9e56c1117f`
- Frozen protocol tag: `relaydesk-protocol-v1-20260813-01` (`0d091d301aea2140387fdd615150984dfed5bc08`)
- Platform branch: `agent/a5/macos-translation-build-hygiene`
- Commit/tag/run: local verified head `6aaccaba187b363c01727401d3523b15a6d280dc`; remote connector head `664bed3e507a1bd65038a2f3f6beb5433d52eb23`; identical tree `e194341fc3dabde2fee45fb567720be612863eca`
- Status: `READY`
- Affected contracts: `none`
- Blocker: `none`
- Requested action: merge the platform branch into `product/relaydesk-v1`
- In reply to: `MAC-029`

## Summary

`package-macos.sh` now reuses an already prepared `.relaydesk-toolchain-macos.env` and invokes `setup-macos.sh` only when the snapshot is absent. This prevents a canonical package rerun from unnecessarily starting Homebrew update/install while preserving first-run setup behavior. The branch also retains MAC-029 translation-source hygiene and merges the latest COMP-005 product state through `fc023795c`; no wire protocol, `IFileTransferService` or shared platform contract changed.

## Verification

- Host/toolchain: Apple Silicon; macOS 26.5.1; Xcode 26.6; Apple clang 21.0.0; CMake 4.3.0; Ninja 1.13.2; Qt 6.11.1; OpenSSL 3.6.3.
- Release configure/build: PASS; bundle version `1.26.0.238`; 10 RelayDesk/Deskflow `.qm` catalogs staged; tracked `.ts` sources unchanged.
- macOS packaging/install Python tests: 20/20 PASS.
- CTest: canonical all-tests run completed 89 targets with 88 PASS and only `OSXKeyStateTests::fakePollCharWithModifier` FAIL on the host keyboard state; exact unchanged code plus the later documentation-only product merge passed the other 88/88 with that host-sensitive target explicitly excluded. Protocol vectors, strong IDs, file safety, incoming resume, TLS transport and runtime composition targets all PASS.
- `validate-package.py`: PASS; 49 required files, 6 JSON files, 60 protocol vectors.
- SHA-256 manifest: 5/5 PASS.
- TEST-005 isolated lifecycle: 16/16 PASS, including self-contained linkage, ZIP/DMG strict ad-hoc codesign, `hdiutil verify`/mount, clean install and launch, same-bundle upgrade and launch, App-only uninstall and user-data preservation.

## Artifacts

- App ZIP: `RelayDesk-macos-arm64-adhoc-6aaccaba.app.zip`, SHA-256 `b75d74923b174da12a930b0cf3f3bc54fd1ae23e87d9f9ec9c2df15bc37d3391`.
- DMG: `relaydesk-1.26.0.238-macos-arm64-adhoc.dmg`, SHA-256 `9ca42e869adfbefe941f9b980a8661ec1b7a1de42f8fa0dbd80f90c34dd9e041`.
- Local evidence: `dist/macos/6aaccaba187b363c01727401d3523b15a6d280dc/TEST-005-macos-report.json` and companion command log.
- Signing/notarization: ad-hoc App seal PASS; Developer ID and notarization `NOT_RUN`.

## Final-user boundary

Accessibility, Input Monitoring and Local Network consent UI remain `NOT_RUN`; real `/Applications` and real user HOME were not touched. Final system permission grants, first-open experience and user acceptance remain the user's responsibility. The unrelated dirty translation files in the original shared root worktree were preserved without restore, cleanup or commit.
