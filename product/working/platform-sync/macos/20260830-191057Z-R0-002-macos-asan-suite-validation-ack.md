# R0-002：macOS ASan 全套生命周期复验 ACK

- Message ID: `20260830-191057Z-R0-002-macos-asan-suite-validation-ack`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-30T19:10:57Z`
- In reply to: `product/working/platform-sync/a0/20260830-190041Z-R0-002-macos-asan-suite-validation.md`
- Product branch / SHA: `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Normal redevelop SHA: `80a49b02cd1940ffe96c3596e9409d3fa9e7ecfe`
- Diagnostic SHA / ref: `5839ce528525c53702224d72ae5b83c27a27999a` / `agent/a0/r0-reconnect-diagnostics`
- Workflow / macOS job: `33329642343` / `99305807755`
- Status: `ACKNOWLEDGED`

## macOS ASan 结果

GitHub Actions `headSha` 已核对为
`5839ce528525c53702224d72ae5b83c27a27999a`。macOS job 成功完成，启用参数为：

```text
CFLAGS=-fsanitize=address -fno-omit-frame-pointer
CXXFLAGS=-fsanitize=address -fno-omit-frame-pointer
LDFLAGS=-fsanitize=address
```

从该 job 原始日志精确计数：

| 验收项 | 结果 |
|---|---|
| settings-only A/B | 50/50 PASS |
| ordered A/B | 50/50 PASS |
| 完整 CTest | 101/101 PASS，43.83 秒 |
| `RelayDeskAutoReconnectRuntimeTests` | #95 PASS，1.39 秒 |
| `RelayDeskTwoProcessRuntimeTests` | #99 PASS，0.94 秒 |
| `ERROR: AddressSanitizer` / `SUMMARY: AddressSanitizer` | 0 匹配 |
| `SIGABRT` / `Received signal 6` / `Abort trap` | 0 匹配 |

macOS crash diagnostics 收集步骤已执行，日志展示其等待并扫描最近五分钟的 `.ips` 后复制规则；
该 job log 未输出任何 `.ips` 文件名。这里仅记录日志范围内未见 report，不把它扩展为对 runner
上所有诊断文件的全局否定。

## Materials 边界

同一 workflow 的 `Development materials diagnostic` job 以 exit code 2 失败。这是该诊断分支
单平台矩阵的预期失败，独立于 macOS ASan build、A/B、CTest 和安装生命周期；它使 workflow
总状态为 failure，但不推翻上述 macOS job 的实际通过结果。

## 结论

本 ACK 证明诊断 SHA 上的两类重连 A/B 和全套 macOS ASan CTest 未报告此前两类
stack-use-after-scope 或 SIGABRT。它不等同于产品分支发布 PASS：产品仍为 `c544dc76f`，
正常重开发修复线为 `80a49b02c`，诊断 workflow 差异仍须在后续正常双平台验证中删除并重新验证。
