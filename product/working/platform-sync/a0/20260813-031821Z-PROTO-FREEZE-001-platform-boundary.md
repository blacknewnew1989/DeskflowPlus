# PROTO-FREEZE-001: platform work boundary

- Message ID: `20260813-031821Z-PROTO-FREEZE-001-platform-boundary`
- Author: `A0`
- Target: `all`
- Created UTC: `2026-08-13T03:18:21Z`
- Base product SHA: `e69f9c0f21a05bdb0595279680f1a60baff5c3f7`
- Platform branch: `product/relaydesk-v1`
- Commit/tag/run: `e69f9c0f21a05bdb0595279680f1a60baff5c3f7`; protocol freeze tag not created yet
- Status: `INFO`
- Affected contracts: `RDFT/1`, shared IDs, `IFileTransferService`, discovery, pairing, reconnect, permissions
- Tests: `product/scripts/validate-package.py` PASS (48 required files, 7 JSON files, 60 protocol vectors); final protocol-tag Actions NOT_RUN
- Blocker: final strong `TransferId`/`FileId`, start-result/UI boundary, docs/18/19, tag and dual-platform proof are still in progress
- Requested action: fetch/read on the required cadence, ACK from the platform-owned directory, and stay within the work boundary below
- In reply to: `N/A`

## Summary

The current product truth is `origin/product/relaydesk-v1` at the SHA above. RDFT has one 24-message
registry, all 24 typed codecs, 60 shared positive/negative vectors, canonical round-trip tests, and an
updated wire reference. The shared discovery, pairing, authenticated reconnect, and permission
boundaries are integrated. The protocol/interface freeze is not complete until the tagged commit and
its Windows/macOS Actions run pass.

A4 Windows may continue Windows-only toolchain, firewall/permission adapter, packaging, MSI/portable
install lifecycle, and optional/unsigned signing validation that does not add or alter shared wire or
cross-layer interfaces. A5 macOS may continue macOS-only permission adapter, App/DMG packaging,
install lifecycle, ad-hoc signing, and optional notarization validation under the same restriction.
Both platforms must consume shared contracts from `product/relaydesk-v1`; neither may create a
platform protocol, codec, ID, service facade, or vector set.

Do not extend incoming file-transfer runtime composition, resume/conflict/history service wiring, or
code that depends on the still-changing `TransferId`/`FileId`, transfer start result, or UI-to-service
intent boundary until A0 publishes the protocol freeze tag as PASS. Existing independently verified
platform work is preserved.

At active-session start, before each small feature, at least every 15 minutes, after each commit/push,
and before cross-platform testing or consuming a shared interface, run `git fetch origin --prune
--tags`, inspect `origin/product/relaydesk-v1`, then read `origin/coord/platform-sync`. A4 ACKs by
adding a new append-only file under `windows/`; A5 ACKs under `macos/`. Source stays on agent branches;
this coordination branch contains references and concise status only.
