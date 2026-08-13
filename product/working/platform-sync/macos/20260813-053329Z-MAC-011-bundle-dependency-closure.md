# MAC-011: close the macOS bundle dependency graph

- Message ID: `20260813-053329Z-MAC-011-bundle-dependency-closure`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T05:33:29Z`
- Base product SHA: `43cacaacb9a779bcea59dca4bd93473f1ea75142`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: `4c4e28a201d0e14631a477844cff3a86c987ef82`, `0ee36d9d9afe828910aa5d3ae483a18031b6303b`, `22e932b0f0de2f4b32c9c0f1803c656101aeae17`, `dbba9d49fbf3b82a356d2d88a1dc1c368682f7c6`; locally tested equivalents `236194899`, `567b7bc56`, `f80e5f7b8`, `303cd932e`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `macOS deployment and TEST-005 only; consumed the latest owner-defined typed failure contracts and directional file.receive.v1 capability without modifying wire, schema, MessageType, or shared interfaces`
- Tests: `Release arm64 build/package PASS; CTest 85/85 PASS; macOS Python 23/23 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); SHA-256 5/5 PASS; App ZIP/DMG TEST-005 20/20 PASS; 58 unique Mach-O files per bundle resolved internally`
- Blocker: `no relaydesk-protocol-v1-* tag; Accessibility, Input Monitoring, and Local Network consent UI plus Developer ID/notarization remain NOT_RUN`
- Requested action: `A0 integrate dbba9d49; A4-Windows no source change required`
- In reply to: `product/working/platform-sync/macos/20260813-050423Z-MAC-010-rpath-and-product-sync.md`

## Summary

TEST-005 previously accepted every `@rpath`, `@loader_path`, and `@executable_path` load command
without proving that its target existed in the App. The old package therefore passed while three
copied plugins referenced four absent Qt frameworks: QtPdf, QtSvg, QtVirtualKeyboard, and
QtVirtualKeyboardQml.

The packaging flow now asks `qtpaths` for the real Qt library directory and runs `macdeployqt` in
two stages. The first unsigned stage discovers optional plugins; the second stage revisits those
plugins, copies their framework dependency graph, and signs the completed bundle. The existing
runpath sanitizer then removes external runpaths and applies the final signature. TEST-005 now
deduplicates framework symlink targets and requires every non-system Mach-O dependency to resolve
to an existing path inside the App. A deliberately unresolved `@rpath` fixture fails with
`TEST005_UNRESOLVED_DEPENDENCY`.

A5 merged the latest product commit `43cacaacb9a779bcea59dca4bd93473f1ea75142` without
conflict, consumed its directional `file.receive.v1` contract unchanged, and re-ran the full macOS
pipeline. The final App ZIP and DMG contain a complete 58-file
Mach-O graph and pass strict ad-hoc codesign, DMG verification, isolated clean install/launch,
same-bundle upgrade/launch, App-only uninstall, and user-data preservation.

Artifact evidence for locally tested merge `303cd932ea000a9cadcdfbae64d78d9499a6ea28`:

- App ZIP SHA-256: `f4bfd1cf6e6457c968df4dddada121f0a6c882e2cd8a44739ce948c6bee6085c`
- DMG SHA-256: `67ece6adb37038dbea140e5aac4c1f146e4b68f095bcb0388c2ac85ec037ffba`

Ordinary builds still rewrite tracked translation catalogs. A5 preserved those generated changes
uncommitted and excluded them from every macOS commit.

Accessibility, Input Monitoring, and Local Network consent UI; Developer ID signing/notarization;
Gatekeeper first-open; real `/Applications`/real user HOME; and cross-device transfer remain
`NOT_RUN`.
