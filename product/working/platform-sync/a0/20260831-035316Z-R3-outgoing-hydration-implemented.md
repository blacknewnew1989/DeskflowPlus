# R3：发送端恢复会话重建已实现

- Message ID: `20260831-035316Z-R3-outgoing-hydration-implemented`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-31T03:53:16Z`
- Product branch: `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelopment baseline: `agent/a0/redevelop-p0@8eb60779ff85d1458466a43e6919466f00e6e64e`
- Owner branch: `agent/a6/process-recovery-outgoing`
- Persistence commit: `6e0059e57401efa42a67f3b0ac6402c36445d50f`
- Hydration commit: `23fcaff0c3b90372a8df976a0c72f4573f335037`
- Previous A0 message commit: `coord/platform-sync@e972792fbd44915dc9ccb520023cf5d1f3697a32`
- Status: `READY_FOR_MACOS_ACK`
- Wire/service changes: none

## 已实现边界

1. 产品组合从 `Settings::settingsPath()/relaydesk/transfer-recovery` 注入恢复根；测试继续使用临时目录。
2. runtime `start()` 在现有 bounded worker pool 扫描 outgoing descriptor，并顺序执行 source rescan/hash；
   socket/GUI 线程不读 source、不 hash、不解码全量 descriptor。
3. `TransferRecoveryStoreLimits::maximumStates` 默认 32；`QDirIterator` 流式枚举，在第 33 个文件
   `next/load/decode` 前停止，只追加一个 `TooManyStates` issue。
4. hydration 在 owner 线程再次验证 local device ID、trust/revocation/fingerprint；worker 重建结果必须与
   stored entries、summary 和 page-plan binding 精确一致。
5. 合法 state 使用原 transfer ID，经既有状态机进入 `Interrupted`，插入 `activeTransfers()` 后发布
   `transferAdded`；不重新 offer、不生成新 ID。
6. peer 已 ready 时直接排队 `offerPreparedTransfers()`；尚未 ready 时主动使用现有 `connectPeer()`；
   late discovery 通过 registry `deviceAdded/deviceChanged` 只重试 hydrated Interrupted session。
7. `stop()` 递增 hydration epoch、清 pending并允许 restart 重扫；旧 scan/build callback不能在 stopped
   runtime 插入 session或连接。
8. `ResumeResponse` 的 receiver durable offsets覆盖 descriptor progress并重建合法 Resuming控制状态；
   descriptor progress不作为实际发送 offset。
9. source size/hash变化、trust撤销、fingerprint变化和损坏 descriptor 均不进入 active transfer，并产生
   明确错误。

## Windows 定向证据

- `RelayDeskFileTransferRuntimeTests`: `44 passed, 0 failed, 0 skipped`。
- hydration 数据矩阵覆盖 listener restart、runtime reconstruction、late discovery、peer-ready-first、
  stop-during-hydration、source size/hash变化、trust撤销、fingerprint变化和 descriptor损坏。
- durable offset：receiver `ResumeStore` 非零 offset与 sender2 `ResumeResponse` 后最后一条 Resuming bytes
  精确相等；最终文件内容一致并清理 descriptor。
- `RelayDeskTransferRecoveryStoreTests`: `11 passed, 0 failed, 1 skipped`；Windows 当前 token不能创建 symlink，
  该项准确 SKIP。
- `RelayDeskAutoReconnectRuntimeTests`: `5 passed, 0 failed, 0 skipped`。
- Windows Debug `gui` target: 154-step build PASS；production `MainWindow` 注入已编译。
- 两路独立只读验收最终均为 GO，未发现本 diff 新增 P0/P1。

以上仍只证明 Windows Debug 同机真实 TLS/文件系统与对象重建链路，不证明真实 OS process kill/relaunch、
macOS 动态文件系统行为或物理 Win-macOS。

## 请求 A5 动作

请基于 owner commit `23fcaff0c3b90372a8df976a0c72f4573f335037` 审阅并在
`product/working/platform-sync/macos/` 追加 ACK/BLOCKED：

- `QDirIterator`、`QSaveFile`、remove和 settings profile路径在 macOS 的语义；
- canonical path、NFC/case与 symlink 跳过行为；
- worker/QFutureWatcher、stop epoch和 late-discovery listener 生命周期；
- Apple Silicon 编译及现有相关 CTest结果；无法本机运行时保持 `BLOCKED`。

ACK 不是当前 A0 集成门禁；完成共享合入后只触发一次正常双平台 hosted run，不为本小提交单独重复构建。
