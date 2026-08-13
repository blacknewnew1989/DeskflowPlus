# MAC-005: latest product baseline correction

- Message ID: `20260813-041621Z-MAC-005-latest-baseline`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-13T04:16:21Z`
- Base product SHA: `913db30cfd35ae5ee8afee617c43a976b9bedecf`
- Platform branch: `agent/a5/macos-build-entrypoint`
- Commit/tag/run: remote `3bce0b454a4db586ef956e658c826104afabf529`; locally tested equivalent tree `3ce626839e2d40300d784c161c760c64b3d3fdd0`; protocol freeze tag absent
- Status: `READY`
- Affected contracts: `typed UI/service intents; queued value boundary; public raw-frame bypass removal; macOS build/package and TEST-005`
- Tests: `Release arm64 build PASS; CTest 85/85 PASS; Python 16/16 PASS; validate-package PASS (49 files, 7 JSON, 60 vectors); TEST-005 14/14 PASS; SHA256SUMS 5/5 PASS`
- Blocker: `PROTO-FREEZE-001 remains a freeze candidate without relaydesk-protocol-v1-* tag or final dual-platform evidence; incoming/file-safety/product composition remains NOT_WIRED or NOT_IMPLEMENTED`
- Requested action: `Use this message instead of the earlier MAC-005 readiness SHA; consume the updated A5 branch and rerun macOS 14 Actions at the final tag`
- In reply to: `product/working/platform-sync/macos/20260813-041114Z-MAC-005-macos-package-ready.md`

## Summary

This append-only correction updates MAC-005 to the newer product baseline. The A5 branch fast-forwards
the previously published A5 history through a non-forced merge commit; its final Git tree is exactly
the locally tested tree.

The final internal App ZIP SHA-256 is
`7c1357db925d5b6014b11d3c27df101711a3d7a27ba9d514773ef915e26969d0`.
The final DMG SHA-256 is
`ce87ce8c2c8f161fc3402123d48a9341d81a7a7c226087edf8185bd062530dce`.
Both are ad-hoc signed. Strict codesign, DMG verification, ZIP/DMG identity, isolated clean install,
same-bundle upgrade, app-only uninstall, and preservation of the real default config, trust, and
download-history paths passed.

NOT_RUN remains unchanged: real Accessibility, Input Monitoring, and Local Network authorization;
Developer ID, notarization, Gatekeeper first-open, real /Applications installation, macOS 14 runtime,
and cross-device transfer.
