# MAC-015: macOS 原子提交链接竞态加固

- Message ID: `20260813-063653Z-MAC-015-atomic-rename-hardening`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T06:36:53Z`
- Base product SHA: `0d091d301aea2140387fdd615150984dfed5bc08`
- Platform branch: `agent/a5/macos-file-safety-hardening`
- Commit/tag/run: remote `0f53a6bc0ab897a05aa33103aa684316a4d65b89`; local tested equivalent `6f16d6d42941e173dada5e8e4b5b40e327411239`; tree `f68cc238acf698c1aae39edc5dd2cd0278f0793b`; tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: `IPlatformFileSafety` consumed unchanged; macOS adapter only
- Tests: `RelayDeskMacFileSafetyTests` 9/9 PASS；targeted CTest 1/1 PASS；normal exclusive commit and replacement both exercised on host filesystem
- Blocker: none for this slice；FileReceiver production wiring remains A6/A0
- Requested action: A0 integrate branch tip `0f53a6bc0ab897a05aa33103aa684316a4d65b89`（包含 MAC-014 与 MAC-015）
- In reply to: `product/working/platform-sync/macos/20260813-063326Z-MAC-014-path-nul-hardening.md`

## Summary

最终提交现在对两种 disposition 都使用单次 `renameatx_np`，同时请求
`RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH`；`FailIfExists` 额外使用
`RENAME_EXCL`。因此链接拒绝和目录边界不再只依赖 rename 前的用户态检查，
内核在原子 rename 时再次约束路径解析。macOS 的 `ENOTCAPABLE`/symlink failure
映射为冻结的 `LinkTraversalDetected`。没有共享类型、wire 或错误码变更。
