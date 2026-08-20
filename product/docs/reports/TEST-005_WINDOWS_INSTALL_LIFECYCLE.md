# TEST-005 Windows 安装包生命周期

## 当前候选（2026-08-20）

当前候选为 `a624a9e40f027c4165dd8838b61cbe98af68d7f2`。其本地 Debug 增量构建、原生串行
CTest 98/98、主窗口/托盘定向回归、`product/tests` 26/26、`product/scripts/tests` 37/37
和 `validate-package.py`（49 个必需文件、10 个 JSON、60 个协议向量）均为 `PASS`。后三项
日志依次为 `product/working/product-tests-a624a9e40.log`、
`product/working/script-tests-a624a9e40.log` 和
`product/working/package-validation-a624a9e40-rerun.log`。

TEST-005 的当前候选 Windows 安装、修复、升级、卸载和残留验证仍为 `NOT_RUN`。本地
Release 打包因缺少原生 Strawberry Perl 未执行，已回退至精确标签 GitHub Actions；因此
当前候选尚无标签、Actions run、artifact、MSI、portable 7Z、包摘要或安装日志，本文不得
将下列历史 `PASS` 解释为当前候选结果。

unsigned SmartScreen/UAC 的交互提示也仍为 `NOT_RUN`，需在最终实际安装验收中观察。

## 历史候选（2026-08-13）

以下结果仅属于提交 `f1f4bed433846048149eed0fc3cfd98b7784c5db` 的历史候选。其自动化
Windows 安装生命周期为 `PASS`；不得用于证明 `a624a9e40` 的安装行为。

| 字段 | 历史事实 |
|---|---|
| 任务 | `TEST-005-windows` |
| 日期 | 2026-08-13 |
| 提交 | `f1f4bed433846048149eed0fc3cfd98b7784c5db` |
| 触发分支 | `release/test005-windows-install-regression`（取证后已删除的临时分支） |
| 运行器 | GitHub-hosted Windows x64 |
| Workflow run | `31657498852`，<https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31657498852> |
| 总体 workflow | `failure`，原因是较旧基线的 CTest 失败，非安装步骤失败 |
| Windows job | `94315075642`；仅因最终聚合包含 CTest 失败而为 `failure` |
| 安装步骤 | `Exercise Windows install, repair, major upgrade and uninstall`：`success` |
| 安装报告 | `test005-windows-install-regression.json`：`PASS` |
| 执行者 | Codex A7 |

工作流以 `-GeneratePreviousPackage` 和一次性 runner 明确许可 `-AllowSystemInstall` 调用
`product/scripts/test-windows-install-regression.ps1`。未提供该许可时，脚本拒绝所有机器
级 MSI 操作。

### 历史包与 artifact 身份

| 字段 | 历史事实 |
|---|---|
| Artifact ID | `9165078568` |
| Artifact 名称 | `relaydesk-windows-x64-f1f4bed433846048149eed0fc3cfd98b7784c5db` |
| Artifact 大小 | 32,863,770 bytes |
| GitHub artifact API digest | `sha256:d2fc4e054f4a150ec246ebcd8cdb3235545fb8ecead79ba69bfcf8a2665817db` |
| 报告 SHA-256 | `208fdfbc9f1dd633d3fdfa2f56b01f24fc247a628b7189bd4e75e7861c3ea96e` |
| 产品 | `RelayDesk` |
| 候选版本 | `1.26.0.9999` |
| 签名状态 | unsigned 内部包 |

| 包 | 字节 | SHA-256 |
|---|---:|---|
| `relaydesk-f1f4bed433846048149eed0fc3cfd98b7784c5db-win-x64-unsigned.msi` | 15,895,609 | `8816bacab2f37825dd19976ed20cf6a551856abe064f0d5688fa1a8d11b5b64e` |
| `relaydesk-f1f4bed433846048149eed0fc3cfd98b7784c5db-win-x64-unsigned-portable.7z` | 12,937,939 | `1f65cc98442ca1fe5a42ca77a33e6386f47b22dcd2981009a27a2d5656c4de36` |

该 artifact 还包含源包、`artifact-manifest.json`、`SHA256SUMS.txt`、CTest 日志、安装报告和
六个 Windows Installer verbose 日志。

### 历史生命周期断言

| 检查 | 历史观察 | 结果 |
|---|---|---|
| 首次安装 | 注册候选、服务、防火墙、二进制与配置 | PASS |
| 服务注册 | `RelayDesk` 服务存在、运行、自动启动并指向已安装 daemon | PASS |
| 防火墙注册 | 两条启用的 Private 入站规则，分别对应 Server/Client 且指向已安装 `deskflow-core.exe` | PASS |
| 同版本修复 | `REINSTALL=ALL REINSTALLMODE=vomus` 后产品、服务、防火墙、配置和标记保持有效 | PASS |
| 首次卸载 | 使用 ProductCode 成功移除候选 | PASS |
| 首次残留断言 | 产品注册、服务、防火墙、安装目录和开始菜单已移除；配置和标记保留 | PASS |
| 前序安装 | 合成的 `1.25.0` 前序包（ProductCode `D0818C14-5DD3-4DA6-B438-BE19D4FD1DE5`）成功安装 | PASS |
| 主版本升级 | 前序包升级到 `1.26.0.9999/BAEA988D-DDA6-4DC3-866D-97364E500E47`，共用 UpgradeCode `50C1FCAB-2BF8-447C-806D-A53C21C6A237`，前序注册消失 | PASS |
| 升级后验证 | 候选注册、服务、防火墙、二进制、配置和标记有效 | PASS |
| 升级后卸载与残留 | 两个 ProductCode、服务、防火墙、安装目录和开始菜单均不存在 | PASS |

前序包只改变 MSI 身份和版本，保留真实候选 payload。这证明真实 Windows Installer
主版本升级事务、稳定 UpgradeCode、ProductCode 替换、服务/防火墙切换、用户数据保留和
清理；不宣称覆盖真实历史产品的数据迁移。

两个独立外部标记文件代表信任和传输历史数据。报告记录 `userDataPreserved`、
`preexistingConfigPreserved` 和 `unrelatedUserDataHashPreserved` 均为 `PASS`：
`RelayDesk.conf` 与两个标记跨安装、修复、升级和卸载保留，原配置字节保持连续子序列，
标记 SHA-256 未变化；唯一临时测试根目录已移除。

每次生命周期事务均产生 verbose 日志，包含成功的 Windows Installer 产品事件，并以
`MainEngineThread is returning 0` 结束。

| 历史日志 | SHA-256 | 最终 engine result |
|---|---|---|
| `test005-msiexec-clean-install.log` | `86c318ae35f8d71d96f644017d3deaf2eb9e703ad1200cd3a07d1d8ed84db548` | `0` |
| `test005-msiexec-repair.log` | `8b520584e5ab43644aa0cb3c40fe3378f856eae3c8607266b364b1343b393ac6` | `0` |
| `test005-msiexec-clean-uninstall.log` | `147652d96cad0414920e11735306f7a80bf1c017df0d546526f5c4dd5f7c25ac` | `0` |
| `test005-msiexec-previous-install.log` | `1236c410a8026782bee061d78c59275ffc23e5eddeaa952d596c0cbfce508982` | `0` |
| `test005-msiexec-major-upgrade.log` | `26270a45449ca5eadda2cf26dd9346b4fc61754b279b003e5a6a459885e753e3` | `0` |
| `test005-msiexec-upgraded-uninstall.log` | `d8c549a5a0da46caefe9bc990eed469d7299047d01a77ae53123df4d558fc5c4` | `0` |

### 历史 workflow 与安装结果的边界

该历史 Windows CTest 日志为 69 项中的 68 项通过、1 项失败：

```text
48/69 Test #48: RelayDeskTransferRecoveryMatrixTests ... ***Exception: SegFault
99% tests passed, 1 tests failed out of 69
```

该临时分支早于已集成的 DeviceId 函数内
正则修复 `da0428940`；旧基线 CTest 失败导致 Windows job 最终聚合及 workflow 显示
`failure`。安装步骤在 CTest 后以诊断续跑继续执行，完成并上传报告和日志，因此历史
安装生命周期仍为 `PASS`，但不得以 workflow 总体颜色替代分项结论。
