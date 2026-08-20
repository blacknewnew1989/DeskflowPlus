# RelayDesk v1 内部发布候选

## 范围

RelayDesk 基于 Deskflow v1.26.0，包含冻结的 RDFT v1 协议、发现/配对/信任、认证重连、
传输界面与历史、单文件、多文件、文件夹、冲突处理、SHA-256、原子提交和中断续传。

本文分别记录历史候选与当前候选。历史构建、Actions 和包仅证明其对应提交，不能证明
2026-08-20 当前候选。

## 当前候选（2026-08-20）

- 产品提交：`a624a9e40f027c4165dd8838b61cbe98af68d7f2`。
- 本地 Debug 增量构建：`PASS`。
- 原生串行 CTest：98/98 `PASS`，47.41 s；日志为
  `product/working/windows-debug-ctest-20260820-131000.log`。
- 主窗口/托盘定向回归：offscreen 各连续 10 次、native 各 1 次均 `PASS`。
- `product/tests`：26/26 `PASS`；`product/scripts/tests`：37/37 `PASS`；日志分别为
  `product/working/product-tests-a624a9e40.log` 和
  `product/working/script-tests-a624a9e40.log`。
- `validate-package.py`：`PASS`（49 个必需文件、10 个 JSON、60 个协议向量）；日志为
  `product/working/package-validation-a624a9e40-rerun.log`。
- Windows Release 本地打包：因缺少原生 Strawberry Perl 未执行，已回退至 Actions。
- `relaydesk-phase4-20260820-01`、GitHub Actions run、artifact、草稿 Release、MSI、
  portable 7Z、App ZIP、DMG 和安装生命周期均为 `NOT_RUN`；不得预填包名、摘要或结果。

以下操作系统和物理设备项目仍为 `NOT_RUN`：macOS Local Network、Accessibility、
Input Monitoring 与 menu bar 交互；Windows/macOS 双机配对、双向键鼠/滚轮/文本和图片
剪贴板、单双/多文件及文件夹传输、冲突决策、断线续传、睡眠唤醒重连；unsigned
SmartScreen/UAC/Gatekeeper 交互；Developer ID、Windows Authenticode 与 notarization。

## 历史发布候选（2026-08-13）

以下候选已经完成当时的自动化验证，作为可追溯历史证据保留：

- 产品提交：`05f92a1ab721f7fd8b893e47e05643d5988e1719`。
- 注释标签：`relaydesk-phase4-20260813-03`（标签对象
  `7254073dc61b1053f67dbea7e55c3e249a80e782`）。
- GitHub Actions run：`31706167585`（`SUCCESS`）。
- 草稿 unsigned Release：`RelayDesk internal relaydesk-phase4-20260813-03`。

更早标签保持不可变：`relaydesk-phase4-20260813-01` 记录由 `4903df2d1` 修复的
Windows 编译失败；`relaydesk-phase4-20260813-02` 是首个完整内部候选；`-03` 额外修复
配对到 Deskflow 布局的组合桥接，在配对或可信设备重新发现后，将具备输入能力的对等端
幂等写入 `ServerConfig`，同时保留外部配置、无效名称和已满布局。

### 历史自动化证据

- 协议冻结：标签 `relaydesk-protocol-v1-20260813-01`，run `31672497950`。
- Windows 真实 MSI 清洁安装、修复、主版本升级、卸载、服务/防火墙和残留生命周期：
  `PASS`，见 `TEST-005_WINDOWS_INSTALL_LIFECYCLE.md`。
- macOS App/DMG 签名封存、挂载、隔离启动、替换和保留用户数据的卸载：`PASS`，见
  `TEST-005_MACOS_INSTALL_LIFECYCLE.md`。
- 生产文件运行时：真实 pinned TLS 回环覆盖单文件、两文件加嵌套空目录、四种冲突策略、
  Windows/macOS 平台安全原子提交，以及在 1 MiB 持久检查点中断、重启 listener 后继续的
  20 MiB 传输。
- 精确标签 Windows job `94467163015`：构建/打包 `PASS`、CTest 89/89 `PASS`，并完成
  unsigned MSI 生命周期套件。
- 精确标签 macOS job `94467163121`：构建/打包 `PASS`、CTest 90/90 `PASS`、严格
  ad-hoc App 校验 `PASS`；安装生命周期 job `94470799096` 校验 ZIP symlink、DMG、隔离
  启动、替换、App-only 卸载和用户数据保留。
- `RelayDeskInputLayoutTests` 覆盖首次插入与持久化、重复观察幂等、信任/输入资格、无效
  名称和外部配置保留；草稿 Release job `94470799137` 为 `PASS`。

### 历史包

下列四个文件均从历史精确标签草稿 Release 下载并在本地复验；本地摘要与 GitHub
release asset digest、`SHA256SUMS.txt` 一致。

| 平台 | 包 | 字节 | SHA-256 |
|---|---|---:|---|
| Windows x64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-win-x64-unsigned.msi` | 16,243,769 | `28340705a8c31d663cd5f10ea605679210c5fec393048c5a2070ae92335d2f07` |
| Windows x64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-win-x64-unsigned-portable.7z` | 13,244,230 | `51e88f915007d51f7efcbe0a9e8496720edebb2b1ac98371584070eedf22655d` |
| macOS arm64 | `RelayDesk-macos-arm64-adhoc-05f92a1a.app.zip` | 28,821,846 | `ad1a56cd74b32a7ebb499b73376a019745fe3a8e42ce69f1e73bc0696430b8af` |
| macOS arm64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-macos-arm64-adhoc.dmg` | 29,068,808 | `0377d49f7bbb9284f666f2033219b5f39c73d7a496238257881ef299a35e2b29` |

历史 Actions artifact：Windows `9183676968`，API ZIP digest
`d0cd7ab0aee49473d62cd0673a2f0b9e80c6b04a6906fc43c375b2f748161e2c`；macOS `9183524798`，
API ZIP digest `4e03738e2186ff214081546875594c9c463615401dd5e81130683ba2f371013f`；macOS
lifecycle artifact `9183692586`，API ZIP digest
`afc76c0ab786e7be7606e4b2f3f0622085f75f036c803273c09db6412c8630b8`。

### 历史内部安装说明

Windows 可使用 `unsigned.msi` 安装，或使用 `unsigned-portable.7z` 便携试用。无证书时
包保持 unsigned，Windows 可能显示未知发布者警告；继续前应核对该历史包的已记录
SHA-256。MSI 安装 RelayDesk 服务和专用网络防火墙规则，卸载时保留用户配置。

macOS 可使用 ad-hoc App ZIP 或 DMG，并将 RelayDesk 拖入 Applications。内部包未公证，
首次启动可能要求 Finder Open 或系统“隐私与安全性”确认；仅在 macOS 提示时授予
Local Network、Accessibility 和 Input Monitoring。App-only 卸载保留用户 Library 中的
RelayDesk 设置、信任和历史。
