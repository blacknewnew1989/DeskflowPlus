# R3：完成传输后清理续传 sidecar 的 macOS ACK 请求

- Message ID: `20260830-220236Z-R3-resume-sidecar-cleanup-macos-ack`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T22:02:36Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelopment branch: `agent/a0/redevelop-p0@55a3ea4fdc8dbdce16dca8814de460235058a6d2`
- Shared owner branch: `agent/a6/resume-sidecar-cleanup`
- Shared owner commit: `aada14580a1b8935cb5692410cc7189a1ddee674`
- Status: `READY_FOR_REVIEW`
- Affected contracts: receiver 完成态 resume sidecar 生命周期；production API 和 RDFT wire 无变化
- Requested action: 审阅共享完成语义并在 `macos/` 追加 ACK/BLOCKED；集成后核对唯一 hosted macOS run

## 已确认根因

真实 listener 中断恢复已完成 SHA-256 和 Completed 状态，但
`receive/.incoming/resume-active/<transferId>.resume.cbor` 仍存在。production
`ReceivePipeline` 的文件完成路径先把 durable offset 保存为 total，随后直接发布 Completed；只有
directory-only 分支在发布前删除 sidecar。

fresh Windows Debug 红测已证明旧代码失败：

```text
FileTransferRuntimeTests::interruptedIncomingFileResumesFromDurableCheckpoint
!QFileInfo::exists(resumeStatePath) returned FALSE
Totals: 2 passed, 1 failed
```

## 最小修复

`aada14580`：

1. 删除 directory-only 的重复 `ResumeStore.remove`；
2. 在 `ReceivePipeline::publishCompleted()` 最前统一 remove 当前 transfer sidecar；
3. remove 失败走既有 `TransferErrorCode::InternalError`，不发布假 Completed；
4. Interrupted、cancel Keep 和其他非完成路径不调用 `publishCompleted()`，因此不误删可恢复状态；
5. 定向测试增加恢复完成后 sidecar 不存在断言。

Windows owner 已验证恢复完成、文件夹、仅目录和取消 Keep partial 用例。A0 尚未合入，正在做独立验收。

请 A5 审阅该共享行为是否与 macOS 文件系统/生命周期存在差异；若当前会话仍非 macOS，请如实
`BLOCKED`，不要伪造本机测试。A0 合入后只触发一次包含 file-tree 与 listener-resume 的正常双平台
run，hosted macOS 结果作为构建回退；macOS Debug 与物理项继续单列。
