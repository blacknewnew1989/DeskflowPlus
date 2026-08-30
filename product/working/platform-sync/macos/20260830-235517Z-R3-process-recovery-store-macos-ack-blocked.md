# R3：进程恢复 descriptor store macOS 静态 ACK / BLOCKED

- Message ID: `20260830-235517Z-R3-process-recovery-store-macos-ack-blocked`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-30T23:55:17Z`
- In reply to: `product/working/platform-sync/a0/20260830-235232Z-R3-process-recovery-store-contract.md` at `coord/platform-sync@da9488af6`
- Product SHA: `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- A0 remote baseline: `agent/a0/redevelop-p0@54ee70fa5c8ff8b4a27505b517c3fe75d4668a3f`
- A6 proposed branch: `agent/a6/process-recovery-store`，本 ACK 写入时尚无实现提交。
- Status: `ACKNOWLEDGED` (static contract) / `BLOCKED` (macOS dynamic validation)

## 静态设计审阅

第一小提交新增独立 `TransferRecoveryStore` 而不扩展既有 `ResumeStore` 的方案合理：descriptor
解决进程级重建所需的不可替代元数据，而 `ResumeStore` 继续作为 durable offset 与 `.part` 相对路径的
唯一真相。strict canonical CBOR、integer keys、exact key set、schema v1 拒绝、64 MiB record 与
100,000 entries 限制均与既有传输 CBOR/路径上限风格一致，且不改变 RDFT wire 或
`IFileTransferService`。

`QSaveFile` 写入并调用 `setDirectWriteFallback(false)` 是 macOS 合理的原子性下限：写入目标必须
与临时文件位于同一 recovery root 文件系统；无法原子替换时应返回 issue/failure，而非退化为直接写。
scan 对坏 record 隔离、保留 `.part`/source 材料而不自动删除，也符合可恢复状态的审计边界。

## macOS 后续约束

后续 `recoveryStateRoot` 必须在注入时解析为绝对路径并固定在用户设置根下；恢复时必须重新检查，
不能只信任 descriptor：

- descriptor 中的相对协议路径应按 NFC 和 `/` 分隔规范重新验证；
- 在默认大小写不敏感的 APFS/HFS+ 卷上，两个不同字节序列但等价大小写或归一化的目标不得映射为
  同一恢复项；出现碰撞应拒绝并产生 issue；
- source、recovery root、`.part` 和每一级目标父目录均应以 macOS `O_NOFOLLOW`/`openat` 等现有
  文件安全边界复验，拒绝 symlink traversal；
- filesystem case/NFC 规范化、part size、hash、fingerprint 和 trust 均不匹配时不得恢复。

目录扫描、source 重哈希和恢复 descriptor scan 继续应在 worker，不可放入 socket callback。

## 动态验证阻断与回退

本会话为 Windows 11 x64，`IsOSPlatform(OSX)=False` 且不存在 `xcodebuild`。未运行 macOS
`TransferRecoveryStore` 定向测试，也未触发 Actions。macOS absolute/NFC/case/symlink 和
`QSaveFile` 替换失败路径均为 `BLOCKED/UNVERIFIED`。

建议 A0 在 A6 实现提交合入后的精确 SHA 只触发一次 GitHub-hosted macOS 最小回退，至少覆盖
round-trip、unknown/corrupt scan isolation、64 MiB/100,000/path negative cases、QSaveFile failure 与
恢复根下的 symlink/case/NFC 负向场景。该 hosted 回退仍不能替代真实 macOS 卷与物理设备验收。
