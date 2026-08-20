# TEST-005 macOS 安装生命周期回归

## 当前候选执行记录（2026-08-20）

| 字段 | 当前事实 |
|---|---|
| 结果 | **PASS**（hosted 构建、打包和自动生命周期） |
| 产品实现提交 | `c134126b95977ca6b97036be18dcfc33a4a3a09a` |
| 当前阶段/标签目标 | `c134126b95977ca6b97036be18dcfc33a4a3a09a` |
| 注释标签 | `relaydesk-phase4-20260820-02`，tag object `9398524f927f33ed58890a0f52cc9bdf20bd3075` |
| 唯一工作流 | `relaydesk-build.yml` |
| Workflow run | [32362194153](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/32362194153)，`SUCCESS` |
| macOS 打包 job | `96403950941`，`SUCCESS` |
| macOS 生命周期 job | `96407573193`，`SUCCESS` |
| Runner | GitHub-hosted `macos-14`，Apple Silicon（`ARM64`） |
| 打包边界 | ad-hoc App 签名；unsigned 内部 DMG 容器 |
| CTest | 100/100 PASS，34.36 s |
| 生命周期 | 19/19 PASS：严格 ad-hoc codesign、App ZIP symlink、DMG 校验/挂载、隔离启动/替换/卸载和用户数据保留 |

### 当前 artifact 与包

| artifact | GitHub artifact ID | API digest | 大小 |
|---|---:|---|---:|
| macOS 包 | `9404129846` | `e47b85e61bf8e3e882e08c27dbcaf2ea7b04a15fb17722d632199df89c03106a` | 65,770,515 bytes |
| macOS 生命周期证据 | `9404365531` | `4b0d1a54f05fecda535569481ec0d5b5d8f22cd186774a297ef3b4c8fab5bd80` | 12,568 bytes |

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| App ZIP | 28,830,111 bytes | `af35a8abacc5bf455ec7c74036a26417d8d9e8cf16d1ada36f8ffbe5b7f1b8d9` |
| DMG | 28,919,663 bytes | `24ca64893fa1f41af0fe3921e715452aaccafdba45ed4a518a6c36370eca3297` |

下载目录为 `dist/actions/32362194153`。manifest、`SHA256SUMS`、本地 `Get-FileHash` 与
Release API digest 一致。草稿 Release 名为 `RelayDesk internal relaydesk-phase4-20260820-02`，
`draft=true`；以标签 `relaydesk-phase4-20260820-02` 的 Release 页面或 run 定位，避免引用
不稳定的 untagged URL。分支 run `32356352794` 在同目标提交上同时完成 Windows 99/99、
macOS 100/100 与同目标包摘要；其 GUI 证据仍仅限 Windows 单机。

## 当前自动生命周期证据

当前 hosted runner 已完成以下 19/19 检查：精确提交/平台/变体、大小和 SHA 的 manifest
校验；`ditto -x -k` 解压 App ZIP；完整嵌套
`codesign --verify --deep --strict --verbose=4`；`hdiutil verify`；向嵌入式 GPL SLA 输入
`Y` 后只读挂载 DMG；挂载 App 的严格签名复验；向隔离 `Applications` 目录复制；GUI 与
`deskflow-core` 的非交互 `--version` 启动；同 bundle 替换；App-only 卸载；配置、可信
设备和接收历史数据保留；以及卸载与临时沙箱清理。

自动环境将 `HOME`、`TMPDIR` 和 XDG 配置、数据、缓存根定向至 `RUNNER_TEMP`，不写入
runner 真实 home 或 `/Applications`。此结论仅证明隔离自动生命周期，不等同于真实用户
macOS 会话。

## 明确的 NOT_RUN 边界

以下均为 `NOT_RUN`：macOS System Settings 中的 Local Network、Accessibility、Input
Monitoring（TCC）授权、撤销和前台复检；真实 menu bar 的最小化/恢复/设置/暂停/继续/退出；
真实 Developer ID 签名、notarization、Gatekeeper 首次打开确认、真实 `/Applications` 安装；
以及物理 macOS 对端与 Windows 的双向键鼠、剪贴板、文件传输和断线续传。hosted CI 不能
替代这些系统交互或物理验收。

## 历史候选执行记录（2026-08-13）

下列 `4377afeed9816fc503c30705681532af274fa5a9` 结果为历史候选，保留用于故障演进和
可追溯性，不能证明当前 `a624a9e40` 候选。

| 字段 | 历史事实 |
|---|---|
| Workflow run | [31657596578](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31657596578) |
| macOS 打包 job / 生命周期 job | `94315371007` / `94317213373`，均 PASS |
| Runner | GitHub-hosted `macos-14`，Apple Silicon（`ARM64`） |
| CTest | 76/76 PASS |
| macOS artifact | `9165097233`，61,594,340 bytes，`sha256:ae11fd8d46e4f63c964e8cab76b2aaa6345b73a7b2e494138278deea952db1cf` |
| 历史 TEST-005 artifact | `9165178241`，9,227 bytes，`sha256:8bf7febb76c53965af6a922707307f1673edc079ef3644497620d5c1b83078e9` |
| 历史 App ZIP | `RelayDesk-macos-arm64-adhoc-4377afee.app.zip`，`245e58be387855d669aec315d59b479605ec0d6c5530184b9b72755be3cf8dbe` |
| 历史 DMG | `relaydesk-4377afeed9816fc503c30705681532af274fa5a9-macos-arm64-adhoc.dmg`，`bae891ad14835943e9eb755d4085e6f0f29ba74cdb68f7ae943c1497e5501ed3` |
| 历史生命周期报告 | `test005-macos-install-regression.json`，`8ed6be86e1793c2a80c49a55babbe6d603722973b492e84e57913723886b9636` |
| 历史命令日志 | `test005-macos-install-regression.commands.log`，`b55771058142765f315b1282cddfe72712e0d0b964c007438baed8e4e2040dd3` |

历史报告记录 bundle identifier `local.relaydesk.desktop`、版本 `1.26.0.9999`、变体 `adhoc`。
App ZIP 由 `/usr/bin/ditto` 创建与解压，中央目录包含 121 个条目、18 个 Unix symlink、6 个
Qt framework，且 `Versions/Current`、顶层 executable、`Resources` 三类 framework 链接均
为 6/6；严格 codesign 显示 `Signature=adhoc`、`TeamIdentifier=not set` 且无 `Authority=`。

## 历史故障演进

- Phase 1 run `31623677270` 的 `codesign verification error: nested code is modified or invalid`
  未向上传递非零退出码，故不能作为签名证据。
- Phase 2 run `31654263274` 将同一嵌套签名问题暴露为 CPack 硬失败；Qt 从 6.10.1 升级至
  6.10.2，采用上游 macdeployqt 签名顺序修复。
- run `31655714399` 在 `QtDBus.framework` 严格校验失败，证明通用 Python ZIP 路径压平了
  Qt framework symlink；提交 `f18863736` 改用 `ditto` 收集 App ZIP。
- 提交 `c6e238938` 在 macdeployqt 前注册所有 App 资源安装规则，使部署/签名成为最后一次
  bundle 变更，并加入最终 Stage 的严格校验。
- run `31656450252` 已通过 ZIP 严格校验和 `hdiutil verify`，但停在嵌入 DMG 许可证提示；
  提交 `4377afeed` 为自动化提供已记录的交互接受输入而不移除 SLA，最终 run `31657596578`
  完成全生命周期 PASS。
