# RelayDesk v1 内部发布候选

## 范围

RelayDesk 基于 Deskflow v1.26.0，包含冻结的 RDFT v1 协议、发现/配对/信任、认证重连、
传输界面与历史、单文件、多文件、文件夹、冲突处理、SHA-256、原子提交和中断续传。

本文分别记录历史候选与当前候选。历史构建、Actions 和包仅证明其对应提交，不能证明
2026-08-20 当前候选。

## 当前候选（2026-08-20）

- 产品实现提交及当前阶段/标签目标：`c134126b95977ca6b97036be18dcfc33a4a3a09a`。
- 本机 `RelayDeskTransferSettingsTests`：`PASS`；排除当前桌面外部剪贴板占用导致的
  `MSWindowsClipboardTests` 后，CTest 98/98 `PASS`。
- 主窗口/托盘定向回归：offscreen 各连续 10 次、native 各 1 次均 `PASS`。
- `product/tests`：26/26 `PASS`；`product/scripts/tests`：37/37 `PASS`；日志分别为
  `product/working/product-tests-a624a9e40.log` 和
  `product/working/script-tests-a624a9e40.log`。
- `validate-package.py`：`PASS`（49 个必需文件、12 个 JSON、60 个协议向量）。
- 注释标签：`relaydesk-phase4-20260820-02`（tag object
  `9398524f927f33ed58890a0f52cc9bdf20bd3075`）。
- GitHub Actions run `32362194153` 为 `SUCCESS`；materials job `96403950792`、Windows job
  `96403951016`、macOS job `96403950941`、draft release job `96407573119` 和 macOS
  lifecycle job `96407573193` 均 `SUCCESS`。
- Windows：CTest 99/99 PASS（31.59 s）；TEST-005 19/19 PASS，MSI 安装、修复、主版本
  升级、两次卸载、服务、防火墙、残留和数据保留均 PASS。
- macOS：CTest 100/100 PASS（34.36 s）；生命周期 19/19 PASS，严格 ad-hoc codesign、App ZIP
  symlink、DMG 校验/挂载、隔离启动/替换/卸载和用户数据保留均 PASS。
- Windows artifact `9404344378`（36,250,178 bytes，API digest
  `b1ec6712fb9b2341b5205b20009acb9d66605f3987908b8c487fead603fee188`）；macOS artifact
  `9404129846`（65,770,515 bytes，API digest
  `e47b85e61bf8e3e882e08c27dbcaf2ea7b04a15fb17722d632199df89c03106a`）；macOS lifecycle
  artifact `9404365531`（12,568 bytes，API digest
  `4b0d1a54f05fecda535569481ec0d5b5d8f22cd186774a297ef3b4c8fab5bd80`）。
- Windows MSI：16,309,970 bytes，`2d81741175e3ca7a69be0d30c37811ec8419236b27aac5e12e577852f054677d`；
  portable 7Z：13,320,217 bytes，`66f08d9cd90094c4009ae2dd98aefa2d13f3ae819f964bb31b3f76651d057647`。
- macOS App ZIP：28,830,111 bytes，`af35a8abacc5bf455ec7c74036a26417d8d9e8cf16d1ada36f8ffbe5b7f1b8d9`；
  DMG：28,919,663 bytes，`24ca64893fa1f41af0fe3921e715452aaccafdba45ed4a518a6c36370eca3297`。
- 草稿 Release：`RelayDesk internal relaydesk-phase4-20260820-02`，`draft=true`；以标签
  `relaydesk-phase4-20260820-02` 的 Release 页面或 run `32362194153` 定位，避免引用不稳定
  的 untagged URL。
- 包取证：`dist/actions/32362194153`、manifest、`SHA256SUMS`、本地 `Get-FileHash` 与
  Release API digest 一致。分支 run `32356352794` 在同目标提交上验证 Windows 99/99、
  macOS 100/100；其 Windows 单机 GUI 取证包含七语言和托盘/关闭恢复 PASS。
- WIN-020 使用上述精确 portable 在当前 Windows 桌面完成真实运行：七语言与托盘、手动地址、
  `deskflow-core.exe server` 的 TCP 24800 启停、GUI UDP 24802、动态文件 TLS 监听、传输中心、
  接收目录/来件/冲突策略及 HKCU 登录启动均经完全退出和重启复验；验收后已回滚运行配置。
  详见 `WIN-020_WINDOWS_SINGLE_HOST_RUNTIME.md`。

以下操作系统和物理设备项目仍为 `NOT_RUN`：macOS Local Network、Accessibility、
Input Monitoring 与 menu bar 交互；Windows/macOS 双机配对、双向键鼠/滚轮/文本和图片
剪贴板、单双/多文件及文件夹传输、冲突决策、断线续传、睡眠唤醒重连；unsigned
SmartScreen/UAC/Gatekeeper 交互；Developer ID、Windows Authenticode 与 notarization。

## 后标签自动化修复回归（2026-08-21）

该回归不创建新标签，不覆盖 `relaydesk-phase4-20260820-02` 的已验证身份。分支 tip 为
`442aa79f2f5e06299fc6368bd46785f4ce003203`，仅包含自动化中文提交修复；产品实现提交为
`1b1a24739dea3775d64fa7987d30e9b37372a5c1`。

- run `32433749495`：`SUCCESS`。materials `96630635916`、Windows `96630636007`
  （99/99，34.41 s）、macOS `96630635945`（100/100，37.91 s）和 macOS lifecycle
  `96633281248`（19/19）均成功；release job `96633282373` 为分支运行预期 `skipped`。
- Windows artifact `9430307996`：36,254,057 bytes，API SHA-256
  `a60f9885a6da1e3aaee2a3a7a69b7ac374bfab8eba266b777daae49891392d52`；macOS artifact
  `9430175846`：65,777,695 bytes，`72f513ec5f04aa3e71755026c59410d8a71fd808b7cd2b8ba03be471b72a06d7`；
  macOS lifecycle artifact `9430317569`：12,562 bytes，
  `2d6a1ad19ff5e9b730d114ea10e29ebd13122f69ac2d647e58cab17c9d6948af`。
- 当前 Windows MSI/portable：16,305,874 bytes /
  `a4a4bc07b677692cc424f5e82e9bf2f38e65bc52b38f6b61ef811c4e917ba8d9`，13,322,560 bytes /
  `63487f414cfafdfa12d82cc15199d802393e59c5dc4cac9c9bed7639e27768da`；macOS App ZIP/DMG：
  28,830,131 bytes / `4a361a003c0d8097cb949e4cf27191512f04a1b87e943724ad23bbe1ac60434f`，28,919,559 bytes /
  `57a6eb1e73ffae0825451dfafb5a0ee3fd695032a8ebcc00ce34500569f7cb17`。
- Windows MSI 的本机运行库修复和交互外部边界见 WIN-021；物理 Win↔Mac、TCC/menu bar、
  SmartScreen/UAC/Gatekeeper/签名仍为 `NOT_RUN`。
- macOS 14 单变量实验 `0b14ddfe4` / run `32435396307` 将链接警告降为零并完成构建打包，
  但 CTest 98/100 有两项超时，生命周期/Release skipped；实验不得合入，也不是发布证据。

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
