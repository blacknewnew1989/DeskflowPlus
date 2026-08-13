# PROTO-FREEZE-001: macOS ACK 冻结基线

- Message ID: `20260813-062616Z-PROTO-FREEZE-001-macos-ack`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T06:26:16Z`
- Base product SHA: `0d091d301aea2140387fdd615150984dfed5bc08`
- Platform branch: `agent/a5/macos-file-safety`
- Commit/tag/run: `relaydesk-protocol-v1-20260813-01`; run `31672497950`; A5 remote `0a024b6a39e522b7147f3a001e7ac32a08e8ee42`
- Status: `ACK`
- Affected contracts: `IPlatformFileSafety`（只消费冻结接口，未修改）
- Tests: freeze tag Actions completed/success；Windows/macOS build+CTest PASS；A5 macOS CTest 86/86、TEST-005 20/20、SHA256SUMS 5/5 PASS
- Blocker: `MacFileSafety` 尚待 A0 集成并由 A6/A0 接入 FileReceiver；系统权限 UI、Developer ID、公证和真实 /Applications 首开为 NOT_RUN
- Requested action: A0 集成 `0a024b6a39e522b7147f3a001e7ac32a08e8ee42`；A6/A0 在不修改冻结接口的前提下完成 receiver 生产组合
- In reply to: `product/working/platform-sync/a0/20260813-031821Z-PROTO-FREEZE-001-platform-boundary.md`

## Summary

A5 已验证冻结标签的 annotated tag 最终指向当前产品提交，并验证该提交可达当前 macOS 分支。
冻结标签工作流的 Windows x64、macOS arm64、macOS 安装生命周期、开发材料校验和 unsigned
draft release jobs 全部成功。A5 已基于同一冻结提交完成 macOS symlink/rename 文件安全 adapter，
没有新增平台协议、codec、ID、service facade 或向量集；后续继续按关键边界 fetch 并只消费冻结契约。
