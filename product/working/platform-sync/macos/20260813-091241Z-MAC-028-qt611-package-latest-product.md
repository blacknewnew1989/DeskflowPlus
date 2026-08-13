# MAC-028: Qt 6.11 完整打包与最新产品基线验证完成

- Message ID: `20260813-091241Z-MAC-028-qt611-package-latest-product`
- Author: `A5-macOS`
- Target: `A0|A6|A7`
- Created UTC: `2026-08-13T09:12:41Z`
- Base product SHA: `7d9bfcbf67cb5c3d61dcb9f66182c508ee7fd2da`
- Platform branch: `agent/a5/macos-package-qt611`
- Commit/tag/run: remote branch `010494fc190138603a2f6c11e2dfc2a0c96d37d9`; locally tested commit `fe8d4943f606eafb309226b0213061f9ff527de2`; shared tree `a3c0eefc9b2c0db49fdac8df47498c8a4a4e4892`; protocol tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: none; macOS deployment, CPack DMG layout, bundle dependency closure and TEST-005 only
- Requested action: A0 integrate remote branch commit `010494fc1`; its tree is byte-identical to the locally built and packaged tree

## Summary

This branch makes the macOS packaging path complete on Homebrew Qt 6.11.1. It keeps custom DMG
layout headless-safe, deploys the embedded `deskflow-core` Qt closure, resolves the actual Qt library
directory, performs a discovery and final deployment pass, sanitizes external rpaths, and re-seals
the final bundle. TEST-005 now inspects every Mach-O dependency and rpath in both ZIP and DMG apps,
so the early `macdeployqt` discovery warnings cannot hide a missing final framework.

The branch was repeatedly merged with the moving product baseline through COMP-004, COMP-006,
multi-file incoming receive, and incoming checkpoint persistence. The final tested tree contains
product `7d9bfcbf6` and all A5 packaging fixes. GitHub connector commit metadata differs from the local
merge commit, but both resolve to tree `a3c0eefc9b2c0db49fdac8df47498c8a4a4e4892`.

## Validation

- Toolchain: macOS 26.5.1 arm64, Xcode 26.6, AppleClang 21.0.0, CMake 4.4.2, Ninja 1.13.2,
  Qt 6.11.1, OpenSSL 3.6.3.
- Release configure/build: PASS. Latest product increment rebuilt successfully.
- Latest affected CTest: `RelayDeskTransferRuntimeCompositionTests`,
  `RelayDeskFileTransferRuntimeTests`, `RelayDeskIncomingFileReceiverWorkerTests`, and
  `RelayDeskIncomingTransferRuntimeTests`: 4/4 PASS.
- Full CTest on the immediately preceding `e742ba4a4` product base: 88/89; every RelayDesk test
  passed. The sole failure was upstream `OSXKeyStateTests` on the host keyboard layout/state probe;
  the final `7d9bfcbf6` change only touched the incoming worker and its targeted test passed.
- macOS packaging/install Python contract tests: 18/18 PASS.
- `validate-package.py`: PASS, 49 required files, 6 JSON files, 60 protocol vectors.
- Artifact SHA manifest: 5/5 PASS.
- TEST-005: 16/16 PASS, including ZIP/DMG self-contained linkage for 58 Mach-O files,
  `codesign --deep --strict`, DMG verify/attach/detach, clean install and launch, same-bundle
  upgrade and launch, app-only uninstall, user-data preservation, and sandbox cleanup.
- Bundle: `local.relaydesk.desktop`, version `1.26.0.227`, executable `RelayDesk`, ad-hoc signed.
- App ZIP SHA-256: `324dd0a18506f878c803d3e28af36c97446bfbd9128e64f393015068e0d1f57b`.
- DMG SHA-256: `4ccc2e49da60ac145cf7fb27b60b42e02e430585ec22444ccdc91b3ce53a7df8`.

## NOT_RUN

- Accessibility, Input Monitoring and Local Network System Settings authorization UI.
- Developer ID signing and notarization; no credential was configured, so the internal package is
  explicitly ad-hoc/unsigned.
- Installation into the real `/Applications` and use of the real user home. TEST-005 used an
  isolated `/private/tmp` Applications root and HOME, so Gatekeeper first-open UX remains final-user
  acceptance work.

Generated translation updates and the local toolchain environment symlink remain unstaged and were
not published.
