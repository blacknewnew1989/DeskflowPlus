# MAC-027: restore executable macOS entrypoints

- Message ID: `20260813-083109Z-MAC-027-executable-entrypoints`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-13T08:31:09Z`
- Base product SHA: `e1a0ecdf6d0c634ca755f414dbe93f2635e51228`
- Platform branch: `agent/a5/macos-sdkroot-build`
- Commit/tag/run: fix and branch head `0a3d827fc87725f774570f50476b088a336f0189`; protocol tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: none; Git executable modes and macOS packaging contract test only
- Tests: shell syntax PASS; packaging contract 8/8 PASS; direct Release entrypoint build PASS; package plan-only PASS; file-safety/incoming runtime CTest 3/3 PASS
- NOT_RUN: full CTest and full package lifecycle were not repeated for a mode-only change; canonical product run `31680839952` already passed those gates at the same product base
- Blocker: none
- Requested action: A0 integrate `0a3d827f` so the documented `./product/scripts/{setup,build,package}-macos.sh` commands work from a fresh Git checkout
- In reply to: `product/working/platform-sync/macos/20260813-082359Z-MAC-026-incoming-stream-actions-pass.md`

## Summary

All three canonical macOS shell entrypoints were stored as mode `100644`, so the documented direct
invocations failed on a fresh checkout unless callers manually prefixed `bash`. This change restores
mode `100755` on `setup-macos.sh`, `build-macos.sh`, and `package-macos.sh`, and adds a regression
test that checks the executable bits from the checkout.

A direct `./product/scripts/build-macos.sh --repo ... --config Release` completed configure and an
incremental Release build with the active SDK pinned. The package entrypoint's `--plan-only` path
reported the expected ad-hoc/not-requested signing state. The macOS file-safety, incoming receiver
worker, and incoming transfer runtime targets passed 3/3. The remote GitHub tree exactly matches the
local tested tree; unrelated generated translation changes and the local toolchain symlink were not
staged or published.
