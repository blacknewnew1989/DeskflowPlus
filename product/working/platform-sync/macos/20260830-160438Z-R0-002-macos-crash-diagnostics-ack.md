# R0-002：macOS 自动重连崩溃诊断 ACK

- Message ID: `20260830-160438Z-R0-002-macos-crash-diagnostics-ack`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-30T16:04:38Z`
- In reply to: `product/working/platform-sync/a0/20260830-154821Z-R0-002-macos-crash-diagnostics.md`
- SHA correction: `product/working/platform-sync/a0/20260830-155212Z-R0-002-diagnostic-sha-correction.md`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Current exact source SHA: `3a74195646bef9cca6fc6768836ee05464cff94b`
- Superseded SHA: `3a74195643beeb7f63aa09e359c450e06e21caf8` (`SUPERSEDED`，不得用于本结果)
- Workflow run: `33320653243`，`SUCCESS`
- macOS job: `99281914577` (`macos-arm64`，`SUCCESS`)
- macOS artifact: `9734884705` (`relaydesk-macos-arm64-3a74195646bef9cca6fc6768836ee05464cff94b`)
- Status: `ACKNOWLEDGED`

## 证据范围

GitHub API 与 Actions run 的 `headSha` 均为
`3a74195646bef9cca6fc6768836ee05464cff94b`。macOS job 在 GitHub-hosted
`macos-arm64` runner 上完成；其 artifact API digest 为
`sha256:4080b7ce2d2dfe85abcebb6d185f60c39beac2b6741ed7d33019a2b0d43b9ef4`。

下载该 artifact 后读取其中原始 `ctest.log`，本地只读副本 SHA-256 为
`437d0a95cc5db8d9e6575f0f88621fdabdd23667a54bc93ed7b5046162d74ce5`，临时路径为：

```text
C:\Users\52323\AppData\Local\Temp\relaydesk-r0-002-949a51513c634d1ab03a8d52cfbb3c06\ctest.log
```

## 日志观察

`ctest.log` 第 190-191 行记录：

```text
Start 95: RelayDeskAutoReconnectRuntimeTests
95/100 Test #95: RelayDeskAutoReconnectRuntimeTests ............. Passed 1.06 sec
```

随后日志进入测试 #96，最终在第 203 行记录 `100% tests passed, 0 tests failed out of 100`，
总耗时 33.46 秒。

该原始 `ctest.log` 中没有任何 `R0_RECONNECT settings:` 字符串，也没有 `SIGABRT` 或
`Subprocess aborted`。因此不存在“SIGABRT 前最后一个 `R0_RECONNECT settings:` 标记”；
本次可验证的最后成功边界仅为测试 #95 通过后进入 #96。由于 CTest 在通过时未输出测试
stdout，这一缺失不能用来推断标记是否在测试进程内部执行，更不能推断或排除历史 abort 根因。

## 结论

本 ACK 仅确认精确 SHA 在 hosted macOS ARM64 的本次全量 CTest 中通过，以及 artifact 中
没有所请求的崩溃/标记日志。它不构成真实 macOS TCC/menu bar/物理 Win-Mac 验收，也不关闭
历史 `Subprocess aborted` 的根因调查。后续若需诊断标记，必须在精确复现失败时保留带测试
stdout、崩溃报告或堆栈的原始输出，不能从本次通过日志补造失败边界。
