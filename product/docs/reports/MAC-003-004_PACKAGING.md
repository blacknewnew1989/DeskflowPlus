# MAC-003 / MAC-004 Packaging Handoff

## Scope and commits

| Field | Value |
|---|---|
| Integration baseline | `cdb3b6cab8c1d8aa9223a7154797bfc696c69259` |
| MAC-003 bundle/DMG commit | `bca26fd3f46bab2a088f2ab628b77193687a0cb6` |
| MAC-004 signing/notarization commit | `b879d2e576020200380ed3edc85a39076c194f18` |
| Branch | `agent/a6/macos-packaging` |
| Date | 2026-08-13 |

## Package contract

- Product display name and bundle identifier remain centralized in
  `product/branding/RelayDeskBrand.cmake`.
- macOS icon filename/source and Local Network usage text now come from the
  same branding file. The current icon is explicitly the internal-build
  fallback; final trademarked artwork is still a release-input decision.
- Default packages are named with the `macos-arm64-adhoc` variant. Missing
  Developer ID or notarization credentials does not fail an internal build.
- `cmake --install` creates a separately staged, deployed `.app`; artifact
  collection archives this app rather than an undeployed build-tree bundle.
- The DMG and app ZIP signature state is recorded in
  `artifact-manifest.json` as `packageVariant`, `signed`, and `notarized`.
- The packaged README states that upgrade and uninstall do not delete
  settings, trust records, history, resumable partial state, logs, or received
  files.

## Optional signing and notarization

Default internal package:

```bash
./product/scripts/package-macos.sh --repo "$(git rev-parse --show-toplevel)"
```

Developer ID package without notarization:

```bash
RELAYDESK_MACOS_SIGNING_IDENTITY="Developer ID Application: ..." \
  ./product/scripts/package-macos.sh --repo "$(git rev-parse --show-toplevel)"
```

Developer ID package with notarization uses an existing Keychain profile:

```bash
RELAYDESK_MACOS_SIGNING_IDENTITY="Developer ID Application: ..." \
RELAYDESK_MACOS_NOTARY_PROFILE="relaydesk-notary" \
  ./product/scripts/package-macos.sh --repo "$(git rev-parse --show-toplevel)"
```

The package entry point deliberately does not accept an Apple ID password,
private key, or certificate payload. `notarytool` reads credentials from the
named Keychain profile. Profile names are redacted from captured notary output.
Signing is checked with `codesign --verify --deep --strict`; a successful
notarization must also pass `stapler validate` and Gatekeeper assessment.

## Executed validation

| ID | Platform | Actual | Result |
|---|---|---|---|
| MAC-PKG-STATIC-01 | Windows 11 | branding validator: 12 values / 8 consumers | PASS |
| MAC-PKG-STATIC-02 | Windows 11 | Python packaging tests: 7 passed | PASS |
| MAC-PKG-STATIC-03 | Windows 11 | Bash syntax for build/package scripts | PASS |
| MAC-PKG-STATIC-04 | Windows 11 | no-credential plan: `adhoc/not-requested` | PASS |
| MAC-PKG-STATIC-05 | Windows 11 | Developer ID plan: `signed/not-requested` | PASS |
| MAC-PKG-STATIC-06 | Windows 11 | Developer ID + profile plan: `signed/requested`; profile absent from output | PASS |
| MAC-PKG-STATIC-07 | Windows 11 | profile without signing identity rejected | PASS |
| MAC-PKG-CMAKE-01 | Windows 11 | generated ad-hoc install contains `-codesign=-` and no hardened runtime | PASS |
| MAC-PKG-CMAKE-02 | Windows 11 | generated Developer ID install contains identity and hardened runtime | PASS |
| MAC-PKG-CMAKE-03 | Windows 11 | signed CMake variant without identity rejected | PASS |

## Not run in this session

| ID | Expected | Actual | Result |
|---|---|---|---|
| MAC-003-RUNNER | Build arm64 `.app` and ad-hoc DMG on macOS 15 | current development host is Windows; run after A0 integration through the existing `relaydesk-build.yml` macOS job | NOT_RUN |
| MAC-003-CLEAN | Install/upgrade/uninstall on a clean Apple Silicon Mac and confirm retained data | no Apple Silicon host attached to this session | NOT_RUN |
| MAC-004-SIGN | Sign with a real Developer ID and verify authorities | no real signing identity supplied | NOT_RUN |
| MAC-004-NOTARY | Submit, staple, and Gatekeeper-verify with real Apple credentials | no Keychain notary profile supplied | NOT_RUN |

These `NOT_RUN` rows do not block ad-hoc internal packaging. They must not be
reported as PASS until the existing macOS Actions runner or a controlled clean
Mac produces the corresponding logs and artifacts at the integrated commit.
