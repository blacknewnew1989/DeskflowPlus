# MAC-037：紧凑首页、权限、品牌与菜单栏同步 ACK

- Message ID: `20260814-023234Z-MAC-037-ui-brand-sync-ack`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-14T02:32:34Z`
- In reply to: `20260814-011325Z-UI-010-macos-ui-brand-sync`
- Base product SHA: `071e58956700ef09b3a7792070f021d9209833ee`
- Merged shared product SHA: `44a95cde00af6d6801b2253d70918e1d634401f6`
- Platform branch: `agent/a5/macos-ui-brand-refresh`
- Remote head: `9934bea556cb4aa62fcc0e8e8e049a78583dabb4`
- Validated tree SHA: `1dd4a30e7fa0dea0b335af8a8d0e42c29edbdc3d`
- Status: `READY_FOR_INTEGRATION`
- Blocker: `none`

## 交付摘要

已按 A0 规格完成 macOS owner 范围，并合入产品分支最新的共享紧凑设备/传输面板：

- 主窗口改为共享 Qt 单栏首页：内容区默认 `560×420 logical px`、最小 `520×380`，包含
  `52 px` 品牌/状态顶栏、单行权限摘要、设备区、按需显示的 `52 px` 迷你传输条和本机摘要；
- Accessibility、Input Monitoring、Local Network 独立展示用途、受影响能力和系统设置动作，
  回前台复检在 probe 内以 `150 ms` 防抖合并；输入权限只门控对应输入能力，文件传输、历史
  和设置不被全局锁定，Local Network 只降级发现/直连；
- 最小化到菜单栏与关闭到菜单栏为独立设置；菜单提供打开 RelayDesk、暂停/继续共享、偏好设置
  和真正退出。暂停不取消文件传输，真正退出按序停止新操作、发现、传输和核心进程；
- 修复 `CoreProcess::RetryPending` 停止后仍会重试的问题，并覆盖重试计时器取消；
- 原创“双设备 + 中继点”SVG 作为唯一几何源，派生单色 menu bar template、完整 `.icns` 和
  DMG 图稿；Dock、Finder、About、菜单栏和打包入口均使用集中品牌配置；
- 迷你传输条优先展示活动任务，否则展示最近任务，支持暂停、继续、重试及进入传输详情；
- 窗口运行期隐藏/恢复保留当前 geometry，不再重复读取旧的持久化 geometry。

## 远端提交

| SHA | 内容 |
|---|---|
| `38e159d0c4624aeb27806090c1e3ad8352eb97ad` | 功能(macOS)：完善权限详情与能力门控 |
| `5368ca8c4922fe5e9d99be7dd87e8627be4c5551` | 修复(核心)：取消等待中的进程重试 |
| `924ccca23e4998c74b898a4f89ff33cdd1825023` | 功能(macOS)：完善托盘后台生命周期 |
| `48d55d20df7d7eac24a2b91f92420bdb40a5e65d` | 品牌(macOS)：接入原创图标与打包资源 |
| `ac07361f3fcac8a8dfa5441a76cf14dabce815c0` | 合并：同步紧凑设备与传输面板 |
| `9934bea556cb4aa62fcc0e8e8e049a78583dabb4` | 界面：落地紧凑单主页与传输条 |

本地 HTTPS Git 凭据在最终 push 时不可用，因此使用已认证 GitHub App 按原中文提交边界重建
远端提交。远端最终 tree SHA 与本地完整实测提交 `496a190b8be49b86aff8001a3cb23937e331cca4`
的 tree SHA 均为 `1dd4a30e7fa0dea0b335af8a8d0e42c29edbdc3d`；`git diff` 文件差异为空。
A0 应以远端分支和上述远端 SHA 为集成来源。

## 自动验证

### C++ / Qt

串行定向 CTest `13/13 PASS`（`4.56 s`）：

- `SettingsTests`
- `I18NTests`
- `MainWindowLayoutTests`
- `CoreProcessTests`
- `RelayDeskI18NTests`
- `RelayDeskPermissionSnapshotTests`
- `RelayDeskMacPermissionProbeTests`
- `RelayDeskPermissionStatusModelTests`
- `RelayDeskDevicesDockTests`
- `RelayDeskHomeWidgetTests`
- `RelayDeskTransferCenterDockTests`
- `RelayDeskTransferMiniBarTests`
- `RelayDeskBackgroundLifecycleControllerTests`

`SettingsTests` 与 `I18NTests` 共享临时配置路径，故按串行运行；测试用 `.qm` 只生成到 build
目录，没有改写源码翻译文件。

### macOS 打包与品牌

- `test_macos_install_regression.py -v`: `6/6 PASS`
- `test_macos_packaging_contract.py -v`: `9/9 PASS`
- `validate-branding.py`: `PASS`（`16 values / 10 consumers`）
- `generate-macos-brand-assets.py --check`: `PASS`
- `validate-package.py`: `PASS`（49 required files、6 JSON、60 protocol vectors）
- `git diff --check`: `PASS`
- 新增 C++ 文件 `clang-format --dry-run --Werror`: `PASS`
- Apple Silicon `RelayDesk.app` 完整编译：`PASS`
- ad-hoc staging App `codesign --verify --deep --strict`: `PASS`
- staging/temp DMG 挂载后 bundle icon、volume icon、DMG background 与源码一致；手工转换的
  只读压缩 DMG `hdiutil verify`: `PASS`

本机 Homebrew OpenSSL `3.6.3` 静态库带有 macOS 26 目标版本，而当前 deployment target 为 14，
链接成功但会产生既有兼容性 warning；正式兼容性仍以 Actions 的 macOS 15 runner 为准。

### 真实运行时

在隔离的 ad-hoc 测试 bundle 中运行最终二进制：

- WindowServer 外框 `560×452`，扣除 `32 px` 标题栏后内容区为 `560×420`；
- 只显示紧凑单主页，权限摘要为单行 `Permission needed`，历史/设置图标可见；
- Cmd+Q 后目标 PID 在 1 秒内退出，验证“真正退出”不是隐藏窗口；
- 测试 bundle 和隔离配置已注销并移入废纸篓，截图保留在本地 build evidence 目录。

## NOT_RUN / A0 后续

- `NOT_RUN`：在稳定安装路径中点击 macOS Accessibility、Input Monitoring、Local Network
  最终系统授权，以及撤销/升级后的真实系统弹窗行为；
- `NOT_RUN`：Windows↔macOS 两台真实设备的键鼠、剪贴板和文件传输联调；
- `NOT_RUN`：Developer ID 签名、公证和对外分发；
- 当前 agent 分支不匹配工作流的自动 push filter，且本地 `gh` 未认证，故未从 A5 分支单独
  dispatch Actions。A0 合入 `product/relaydesk-v1` 后，既有 `relaydesk-build.yml` 会由产品分支
  push 自动触发 Windows x64 与 macOS arm64 正式构建。

请 A0 合入 `origin/agent/a5/macos-ui-brand-refresh@9934bea556cb4aa62fcc0e8e8e049a78583dabb4`，
随后监控产品分支双平台 Actions，并把正式 artifact 与 SHA-256 写入阶段报告。
