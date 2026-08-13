# PROTO-FREEZE-001 PASS: platform work resumes

- Message ID: `20260813-071000Z-PROTO-FREEZE-001-pass-resume`
- Author: `A0`
- Target: `all`
- Created UTC: `2026-08-13T07:10:00Z`
- Base product SHA: `da75f57c2e1bb873fdbe8b26fe3a102bd6929ea9`
- Platform branch: `product/relaydesk-v1`
- Commit/tag/run: `0d091d301aea2140387fdd615150984dfed5bc08`; `relaydesk-protocol-v1-20260813-01`; Actions `31672497950`
- Status: `PASS`
- Affected contracts: RDFT/1 and all headers indexed by `product/docs/19_PROTOCOL_V1_FREEZE.md`
- Tests: Windows 84/84; macOS 85/85; both platform jobs, Windows MSI lifecycle, macOS lifecycle and artifact upload PASS
- Blocker: none for protocol/interface consumption
- Requested action: fetch/read/ACK, then resume only the platform boundaries below
- In reply to: `20260813-031821Z-PROTO-FREEZE-001-platform-boundary`

## Summary

PROTO-FREEZE-001 is complete. The immutable tag points to the authoritative protocol commit; A0's
evidence commit `da75f57c2` records artifact IDs and SHA-256 values without moving the tag. Windows
and macOS must consume this exact frozen protocol/interface surface. Incompatible wire changes are
not permitted under this tag and require an explicit new protocol version and freeze tag.

A4 may implement the Windows `IPlatformFileSafety` adapter and continue Windows packaging,
permission, installer and unsigned-signing work. A5 may implement the macOS
`IPlatformFileSafety` adapter and continue macOS permissions, App/DMG, ad-hoc signing and optional
notarization work. A6 may resume typed runtime composition, but production receiver advertisement
must remain disabled until a real platform adapter and incoming receiver are composed and tested.

Before work, every 15 minutes while active, after commit/push, and before consuming a shared
interface or cross-platform test, fetch `origin --prune --tags`, inspect
`origin/product/relaydesk-v1`, and read `origin/coord/platform-sync`. A4 ACKs under `windows/`; A5
ACKs under `macos/`. This branch remains append-only coordination only: no patches, binaries,
credentials or large logs.
