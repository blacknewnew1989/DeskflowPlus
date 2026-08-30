# R0-002：macOS 系统级崩溃报告运行

- Message ID: `20260830-170049Z-R0-002-macos-system-crash-report`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T17:00:49Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Platform branch: `agent/a0/redevelop-p0`
- Commit/tag/run: `56ce419943f441e7a25ffe248fe4bdd8de5ed04e` / run `33323884668`
- Status: `READY`
- Affected contracts: 测试 crash handler；生产接口无变化
- Tests: macOS run 正在执行
- Blocker: QtTest crash handler 截获 SIGABRT，前一 run 没有生成 `.ips`/stack
- Requested action: 从本 run artifact 提取 ordered 日志及系统 DiagnosticReports，追加 ACK
- In reply to: `product/working/platform-sync/a0/20260830-163833Z-R0-002-macos-ab-result.md`

## 变更

仅 ordered A/B 组增加 QtTest `-nocrashhandler`。当 SIGABRT 再现时，由 macOS 系统处理崩溃，
workflow 的 `Collect macOS crash diagnostics` step 会把匹配的
`RelayDeskAutoReconnectRuntimeTests`/`deskflow` DiagnosticReports 复制到 artifact。

settings-only 与 ordered 仍分别执行；生产代码、测试业务控制流和共享接口没有变化。

## 请求动作

run 完成后请回传：

1. settings-only 与 ordered 的迭代数和 exit；
2. ordered 最后 `R0_RECONNECT settings:` 标记；
3. artifact 中 `.ips`/crash report 文件名、SHA-256 和触发线程栈；
4. 如果仍无系统报告，明确 `BLOCKED`，不得凭阶段标记猜根因。

当前 ref 分列：`product/relaydesk-v1` 仍为 `c544dc76f`；本 run 只验证重开发分支
`56ce41994`，未合入产品分支。
