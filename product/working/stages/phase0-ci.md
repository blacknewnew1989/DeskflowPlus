# Phase 0 dual-platform build, test, and package record

Date: 2026-08-12 (Asia/Shanghai)  
Owner: A0/A7  
Stage tag: `relaydesk-phase0-20260812-01`  
Commit: `808a3307b07422e7ea8c60af46148ce68af13649`  
Workflow run: `31602699800`

## Result

The unique `.github/workflows/relaydesk-build.yml` completed successfully from the
annotated Phase 0 tag on Windows 2022 x64 and macOS 15 arm64. The development
materials diagnostic also passed. Tests remain diagnostic rather than a required
repository gate, but their actual results were inspected: Windows passed 27/27
CTest targets and macOS passed 28/28.

## Windows x64

- Toolchain: Windows 2022 runner, MSVC, Ninja, Qt 6.10.1, vcpkg
  `x64-windows-release`.
- Packages: portable 7Z, WiX MSI, source 7Z, and source ZIP.
- Artifact: `relaydesk-windows-x64-808a3307b07422e7ea8c60af46148ce68af13649`.
- Artifact ID: `9144025951`.
- Artifact ZIP size: 32,044,850 bytes.
- GitHub artifact SHA-256:
  `e97b274486a61909b89791bf85d576b534e532a3830929d1c0d5acfc672041dd`.
- CTest: PASS, 27/27.

## macOS arm64

- Toolchain: macOS 15 runner, Apple Silicon target, Ninja, Qt 6.10.1, macOS 14
  deployment target.
- Packages: ad-hoc/unsigned internal App ZIP, DragNDrop DMG, and source archives.
- Artifact: `relaydesk-macos-arm64-808a3307b07422e7ea8c60af46148ce68af13649`.
- Artifact ID: `9143920156`.
- Artifact ZIP size: 38,236,127 bytes.
- GitHub artifact SHA-256:
  `cc73b5d9226dc973348be64a9fa0470a7d88be00af84996b4b272aa87121bda5`.
- CTest: PASS, 28/28.

## Diagnosed CI issues fixed before sealing the stage

- Resolved the macOS SDK dynamically to avoid an empty `--sysroot` linker flag.
- Preserved Windows vcpkg paths by using the runner default configure shell.
- Serialized binary and source CPack targets to prevent `.ninja_log` copy races.
- Excluded generated build/tool directories from source packages.
- Collected only final CPack outputs, not temporary staging trees.
- Pointed CTest at the registered `build/src/unittests` tree.
- Closed the APFS writer before setting the fixture timestamp, then reopened the
  file `ReadWrite` because Windows requires a writable handle for `SetFileTime`.

## NOT_RUN / final device acceptance

- Windows-to-macOS and macOS-to-Windows real keyboard, mouse, scroll, and text
  clipboard operation: NOT_RUN; requires two installed physical systems.
- macOS Accessibility, Input Monitoring, and Local Network permission flows:
  NOT_RUN; these are final user authorization steps.
- Gatekeeper first-open behavior and real Apple/Windows code signing: NOT_RUN;
  current packages are explicitly internal unsigned/ad-hoc artifacts.

No source download, tool installation, Git operation, Actions operation, packaging,
or artifact production was delegated to the user.
