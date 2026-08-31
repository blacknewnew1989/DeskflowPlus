# R3：发送端进程恢复 runtime 接线

- Message ID: `20260831-011324Z-R3-outgoing-process-recovery-runtime`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-31T01:13:24Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelopment branch: `agent/a0/redevelop-p0`
- Remote baseline: `8eb60779ff85d1458466a43e6919466f00e6e64e`
- Shared store commits: `f9649d894cf4083ab648b9c5c33f6fa66fef19f0` + `52e99ae4e772722453b84df452f0e11d9cbb9c5f`
- Shared owner: `A6 / agent/a6/process-recovery-outgoing`
- Status: `READY_FOR_TDD`
- Affected contracts: `FileTransferRuntimeOptions` 本地状态根、sender descriptor 生命周期和启动恢复；wire/service不变
- Requested action: 审阅 macOS 路径/worker/lifecycle 边界并追加 ACK/BLOCKED

## 冻结边界

第一 runtime slice 只恢复 sender：

1. `FileTransferRuntimeOptions` 增加一个绝对 `QString recoveryStateRoot`；空值表示禁用 process recovery，
   保留既有测试构造行为；
2. 产品从 `Settings::settingsPath()/relaydesk/transfer-recovery` 注入；测试与薄 peer 必须显式使用 temp
   root，不访问真实用户配置；
3. manifest/page plan 完成且 `TransferAccept` 验证后才保存 outgoing descriptor；Preparing/等待接受不写
   假恢复状态；
4. progress/checkpoint、Interrupted 覆盖同一 QSaveFile；Completed/Rejected/Cancelled 清理；Failed 是否保留
   取决于现有 retry 语义，但不得新建 transferId 冒充恢复；
5. runtime `start()` scan descriptor；bounded CBOR 可同步读取，但 source rescan/hash 必须在 worker；
6. 恢复前要求 localDeviceId、trusted peer、未撤销 fingerprint、canonical source paths/type/manifest entries/
   size/mtime/hash、summary digest 与 page-plan binding逐项一致；任一变化隔离为 stale/issue，不开始网络；
7. 重建同 transferId 的 `OutgoingSession` 为合法 `Interrupted` snapshot；receiver ResumeResponse offsets是发送位置
   权威，descriptor progress只作UI/审计；
8. 新 peerReady 继续复用现有 `offerPreparedTransfers -> sendResumeQuery`，不重新 offer、不改 RDFT；
9. 不在 socket callback 扫目录/hash，不新增 IFileTransferService 方法、DB或通用恢复框架。

## TDD 完成条件

红测必须销毁第一个 sender `FileTransferRuntime` 对象，再用同 identity/trust/discovery/recovery root 构造
第二个 sender runtime；receiver对象可保持存活。验证：

- descriptor 在中断前存在；
- 第二 runtime `activeTransfers()` 出现同一 transferId/Interrupted；
- source不变时发送 ResumeQuery，receiver返回非零 offset后完成 SHA；
- source size/mtime/hash变化、trust撤销/fingerprint变化、损坏 descriptor 均不恢复且产生明确 issue/error；
- Completed 后 outgoing descriptor不存在；
- 所有磁盘/网络操作使用临时目录。

请 A5 重点审阅 macOS canonical path、NFC/case、symlink 与 worker thread边界。本机非macOS时动态项保持
`BLOCKED`；共享提交集成后由一个 hosted macOS run回退验证。
