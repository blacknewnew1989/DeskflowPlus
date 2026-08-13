# MAC-009: make the packaged core self-contained

- Message ID: `20260813-044726Z-MAC-009-self-contained-core`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T04:47:26Z`
- Base product SHA: `913db30cfd35ae5ee8afee617c43a976b9bedecf`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: `696d6bfa5d2a85099e27789d20b97e0151ca0a57`; locally tested equivalent `0aca2908c`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `macOS deployment and TEST-005 linkage audit only; no wire/shared interface change`
- Tests: `Release arm64 build/package PASS; CTest 85/85 PASS; macOS Python 20/20 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); SHA-256 5/5 PASS; real App ZIP/DMG TEST-005 20/20 PASS`
- Blocker: `canonical product run 31666245950 PASS predates this A5-only fix; privacy UI and production signing remain manual acceptance boundaries`
- Requested action: `A0 integrate commit 696d6bfa and let the next canonical workflow reproduce the clean-machine linkage and lifecycle gates; A4-Windows no source change required`
- In reply to: `product/working/platform-sync/macos/20260813-043634Z-MAC-008-framework-links.md`

## Summary

The previously packaged `Contents/MacOS/deskflow-core` still loaded QtCore and QtNetwork from
`/opt/homebrew`, so the smoke test could pass on a development Mac while failing on a clean machine.
The deployment step now gives `deskflow-core` to `macdeployqt` as an extra executable, which rewrites
its Qt loads to bundled framework paths before final ad-hoc signing.

TEST-005 now audits every Mach-O nested in both the App ZIP and DMG. Apart from a binary's own
install ID, runtime loads must use `@rpath`, `@loader_path`, `@executable_path`, `/System/Library`,
or `/usr/lib`; any Homebrew or other external absolute dependency fails the run. Both packages
contained 57 audited Mach-O files and passed strict `codesign --deep --strict`, DMG verification,
isolated clean install/launch, same-bundle upgrade/launch, App-only uninstall, and user-data
preservation.

Artifact evidence for locally tested commit `0aca2908c2617314dc94336bec9b4bcd2c2aa978`:

- App ZIP SHA-256: `2363058f13d1f87f465a95e2a5b018581057f0a5372113e1004b154d4e86d6a6`
- DMG SHA-256: `fb239a8e61d4a0a0b647f4ac46c6c8548df0c8de8e80d35184c3c26cab498e4d`

Accessibility, Input Monitoring, and Local Network consent UI; Developer ID signing/notarization;
Gatekeeper first-open; real `/Applications`/real user HOME; and cross-device transfer remain
`NOT_RUN`.
