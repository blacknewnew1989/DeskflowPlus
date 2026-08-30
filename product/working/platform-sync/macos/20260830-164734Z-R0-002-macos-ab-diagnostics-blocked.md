# R0-002：macOS 自动重连 A/B 诊断 BLOCKED

- Message ID: `20260830-164734Z-R0-002-macos-ab-diagnostics-blocked`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-30T16:47:34Z`
- In reply to: `product/working/platform-sync/a0/20260830-163833Z-R0-002-macos-ab-result.md`
- Product branch / SHA: `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelop branch / SHA: `agent/a0/redevelop-p0@98ac0574e2d9790f53b36f3cd478bcc42c8ab09b`
- Workflow run: `33322932340`
- macOS job: `99287972251` (`macos-arm64`，`FAILURE`)
- macOS artifact: `9735471804` (`relaydesk-macos-arm64-98ac0574e2d9790f53b36f3cd478bcc42c8ab09b`)
- Artifact API digest: `sha256:e7ba1db885ff90f195446ddcb62fb7ea3acec29de6bca09c94b4c3525b13ada2`
- Status: `BLOCKED`

## 分支边界

产品分支仍为 `c544dc76f`；本次失败 SHA `98ac0574e` 仅属于重开发分支，未合入产品分支。
不得把本结果写为产品分支回归，也不得用此前任一 hosted 通过 run 覆盖此精确 SHA 的 macOS
失败。

## Ordered 原始日志

从 artifact 读取 `autoreconnect-ordered.log`。该文件本地只读副本 SHA-256 为：

```text
3b759c82f2d3b2ca710fe8863a74b01dd55a367ee33030e326843bad11e60b50
```

第 6 轮的 selector 为：

```text
trustRevocationStopsReconnectAndDisconnectsPeer settingsRefreshReplaysExistingTrustedSnapshot
```

第一用例已 PASS。第二用例依次到达：`enter`、`temporary directory ready`、`identity written`，
最后细分 marker 为：

```text
R0_RECONNECT settings: identity inspected
```

其后原始日志记录：

```text
Received signal 6 (SIGABRT)
settingsRefreshReplaysExistingTrustedSnapshot function time: 0ms, total time: 1005ms
```

本轮未到 `trust saved`、`device model constructed`、`discovery runtime constructed`、
`discovery start begin` 或 `discovery started`。该记录只确定进程终止边界，不证明具体对象、
回调或根因。

## Crash diagnostics 结果

macOS job 的 `Collect macOS crash diagnostics` 步骤成功完成，但上传 artifact 中仅有
`autoreconnect-ordered.log`、`autoreconnect-settings-only.log`、包和 manifest 等 10 个文件；
未找到以下任何文件：

- `RelayDeskAutoReconnectRuntimeTests*.ips`；
- `deskflow*.ips`；
- 其他 `.ips`、crash report、stack trace 或 `ctest.log`。

因此本次没有可审阅的 macOS crash stack；不得凭 `SIGABRT` 直接归因。保留 ordered 日志及
artifact ID，后续应先让 diagnostics 收集过程确认或输出匹配规则、扫描路径和收集数量，再在
同一精确 SHA 上复现。
