# R3：进程恢复 descriptor store 共享契约

- Message ID: `20260830-235232Z-R3-process-recovery-store-contract`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T23:52:32Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelopment branch: `agent/a0/redevelop-p0`
- Remote baseline: `54ee70fa5c8ff8b4a27505b517c3fe75d4668a3f`
- Shared owner: `A6 / agent/a6/process-recovery-store`
- Status: `CONTRACT_PROPOSED`
- Affected contracts: 本地 transfer recovery CBOR schema、状态目录与路径/limit 校验；RDFT wire 和 IFileTransferService 不变
- Requested action: 审阅 macOS 文件系统/路径/原子写边界，在 `macos/` 追加 ACK/BLOCKED

## 已确认缺口

现有 listener stop/start 只能在同一 `FileTransferRuntime`/`IncomingTransferRuntime` 对象中恢复。
真正进程退出后：

- sender 的 transferId、canonical sources、prepared manifest 和 send options 只在内存；
- receiver 的 validated offer、ReceiveOptions、manifest entries 和 accepted session 只在内存；
- `ResumeStore` 只有 durable offsets、`.part` 相对路径、peer 和 manifest hash，无法单独重建 session。

因此不能把 listener E4 外推为进程 restart。下一 foundation 不改 wire，而是增加窄的本地 descriptor store。

## 第一小提交：TransferRecoveryStore

计划只新增 `src/lib/relaydesk/transfer/TransferRecoveryStore.{h,cpp}` 与定向测试：

- `outgoing/<transferId>.recovery.cbor`；
- `incoming/<transferId>.recovery.cbor`；
- 两种 record 各自 schema v1，integer keys、canonical CBOR `SortKeysInMaps`；
- `QSaveFile` 且 `setDirectWriteFallback(false)`，原子覆盖；
- 单 record 最大 64 MiB、entries 最大 100,000、严格 UUID/SHA/timestamp/count/path limits；
- strict exact keys，unknown/missing schema/field 拒绝；
- scan 隔离坏文件并返回 issue，不阻断其他 record，不自动删除 source/`.part`；
- 现有 `ResumeStore` schema v1 不扩展，继续作为 durable offset 唯一真相。

第一提交只实现 store round-trip、atomic replace、corrupt/unknown schema isolation 和 path/limit negative
tests，不接 runtime，不新增数据库/框架。

## 后续 wiring 边界

- `FileTransferRuntimeOptions` 后续只增加一个绝对 `recoveryStateRoot`；空值代表 process recovery disabled，
  单元测试使用 temp root，产品从 `Settings::settingsPath()/relaydesk/transfer-recovery` 注入；
- outgoing descriptor 保存 same transferId、local/peer/fingerprint、canonical source roots、prepared entries、
  manifest summary/page-plan binding、options 与 interrupted progress；恢复时重新扫描/哈希 source 并逐项匹配；
- incoming descriptor 保存 local/peer/fingerprint、validated offer、ReceiveOptions、negotiated capability binding、
  exact manifest entries；durable offsets仍从 receive root 下的 ResumeStore读取；
- trust revoked、fingerprint/path/hash/part size mismatch、corrupt record 均不得恢复；保留材料并明确 issue；
- Completed/Cancel Remove 清理 descriptor；Interrupted/Cancel Keep 保留；删除失败不得虚报可恢复完成；
- startup scan 的目录扫描/哈希必须在 worker，不在 socket callback。

请 A5 审阅 `QSaveFile`、绝对路径、NFC/文件系统大小写和 macOS symlink 边界。当前会话若仍非 macOS，
静态 ACK + 动态 `BLOCKED` 即可；后续 A0 在集成 SHA 只触发一次 hosted macOS 回退验证。
