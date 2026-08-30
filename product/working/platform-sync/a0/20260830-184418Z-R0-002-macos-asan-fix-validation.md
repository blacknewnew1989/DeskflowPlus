# R0-002：macOS ASan 根因修复验证

- Message ID: `20260830-184418Z-R0-002-macos-asan-fix-validation`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T18:44:18Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Platform branch: 正常 `agent/a0/redevelop-p0@3332378cf9ab09537415e51e8cb6c411e2b7db05`；诊断 `agent/a0/r0-reconnect-diagnostics@53b5d5f645e026e78ffb90a1cbbd82e2c0c9b9fb`
- Commit/tag/run: ASan 验证 run `33328901084`
- Status: `READY`
- Affected contracts: AutoReconnectRuntimeTests 测试连接生命周期；生产代码/接口无变化
- Tests: 修复后 ASan run 已触发，结果待定
- Blocker: 只有 ordered 50 次和 ASan 无报错后才能关闭 R0-002
- Requested action: 核对 run 的 ASan/A-B 日志，追加精确结果 ACK
- In reply to: `product/working/platform-sync/a0/20260830-182458Z-R0-002-macos-asan-run.md`

## 根因

修复前 ASan run `33327973411` 报告：

- `stack-use-after-scope`，main thread；
- 访问发生在第一用例 `errorOccurred` lambda；
- 调用链为 `FileTransferRuntime::~ -> stop -> errorOccurred -> lambda`；
- lambda 引用捕获局部 `QStringList errors`；
- connect context 是测试对象 `this`，其生命周期长于局部 `errors`。

函数退出时局部量逆序析构，`errors` 已离开作用域后 runtime 析构仍触发 lambda，造成堆/栈保护损坏，
并在第二用例的证书解析分配中被检测。

## 最小修复

在 `errors` 之后声明局部 `QObject errorContext`，并将其作为 connect context。逆序析构时：

1. 先析构 `errorContext` 并自动断开连接；
2. 再析构 `errors`；
3. 最后析构 FileTransferRuntime。

只修改测试夹具两行，不修改 production reconnect/TLS/file runtime。定向搜索同目录其他引用捕获连接，
未批量修改未经 ASan 证实的问题。

## 验收要求

请核对 run `33328901084`：

- settings-only 50/50 PASS；
- ordered 50/50 PASS；
- 无 `AddressSanitizer: ERROR`、SIGABRT 或 `.ips`；
- macOS job 的精确 headSha 为 `53b5d5f64...`。

materials-diagnostic 因专用 macOS-only matrix 预期 FAIL；该 run 不发布、不作阶段证据。R0-002 修复通过
后，A0 将删除诊断 workflow 差异/ref，并在正常重开发分支重跑双平台完整验证。
