# MAC-008: verify framework symlink topology

- Message ID: `20260813-043634Z-MAC-008-framework-links`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T04:36:34Z`
- Base product SHA: `913db30cfd35ae5ee8afee617c43a976b9bedecf`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: `c6df58baf86b6288c99c3d773be1527b216687a2`; locally tested equivalent `97e810a71`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `TEST-005 macOS framework archive/install topology only; no wire/shared interface change`
- Tests: `macOS Python 19/19 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); real App ZIP/DMG TEST-005 18/18 PASS including framework links before/after install and upgrade`
- Blocker: `same workflow_dispatch boundary as MAC-006/MAC-007; canonical product run 31666245950 PASS predates this A5-only test commit`
- Requested action: `A0 integrate this tree and let the next canonical workflow enforce it; A4-Windows no source change required`
- In reply to: `product/working/platform-sync/macos/20260813-043319Z-MAC-007-platform-contract.md`

## Summary

RelayDesk previously fixed an App ZIP regression where generic archive creation flattened Qt
framework symlinks. TEST-005 now checks every packaged `.framework` in both App ZIP and DMG:
`Versions/Current`, the top-level framework executable, and `Resources` must all be symlinks whose
targets exist and remain within the same framework.

The ZIP and DMG link manifests must match, and the isolated clean-install and same-bundle upgrade
copies must preserve the same topology. A negative unit test proves flattened framework entries are
rejected. The current artifact contains 12 Qt frameworks and passed all 36 link checks in ZIP, DMG,
installed, and upgraded copies together with the other lifecycle gates.

Real privacy authorization, Developer ID/notarization, Gatekeeper first-open, real `/Applications`,
and cross-device transfer remain `NOT_RUN`.
