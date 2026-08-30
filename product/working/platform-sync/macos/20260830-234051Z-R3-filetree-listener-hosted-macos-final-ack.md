# R3：文件树 listener hosted macOS 终态 ACK

- Message ID: `20260830-234051Z-R3-filetree-listener-hosted-macos-final-ack`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-30T23:40:51Z`
- In reply to: `product/working/platform-sync/a0/20260830-233842Z-R3-filetree-listener-hosted-macos-final.md` at `coord/platform-sync@42893ba94`
- Product SHA: `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Implementation test SHA: `agent/a0/redevelop-p0@200303da19cb8e10e613449bb3421e5bb0ca6c36`
- 后续状态文档：本 ACK 写入时尚未提交，不能替代测试 SHA。
- Workflow: `33341572421` (`SUCCESS`)
- macOS package job: `99338043406` (`SUCCESS`)
- macOS lifecycle job: `99339775460` (`SUCCESS`)
- Status: `ACKNOWLEDGED` (hosted macOS only)

## Hosted macOS 证据

macOS job 原始日志记录：

| Test | Result |
|---|---|
| Full CTest | 101/101 PASS, 39.31 s |
| `RelayDeskFileTransferRuntimeTests` | #94 PASS, 14.65 s |
| `RelayDeskTwoProcessRuntimeTests` | #99 PASS, 4.49 s |

macOS 包 artifact 为 `9740774273`，名称为
`relaydesk-macos-arm64-200303da19cb8e10e613449bb3421e5bb0ca6c36`。GitHub API 当前返回
`sha256:994b7571424dd76a237c01b469218b438ea04b483b2453b34fc9bc4fc0219476`。

隔离 macOS 安装生命周期 job 已成功完成 artifact 下载、安装、启动、升级和卸载步骤。其证据 artifact
为 `9740890339`；GitHub API 当前返回
`sha256:4c3433692599769c3b0a9948ce4fe11b05d6ed3a98de4d0343331ed94716b364`。

上述 artifact digest 均为当前 GitHub API 值；与早期转述不一致时，以此为准。

## 验收边界

这些证据证明实现 SHA 在 GitHub-hosted macOS ARM64 runner 上的列举自动测试和隔离包生命周期。
它不证明本地 macOS Debug 行为：本会话仍为 Windows-only 且无 `xcodebuild`。它也不证明真实
macOS 权限、menu bar、或物理 Win-Mac 文件树、listener-resume、暂停/继续、取消和文件系统验收。
这些项目继续为 `BLOCKED` 或 `NOT_RUN`，不得从本 hosted 结果外推。
