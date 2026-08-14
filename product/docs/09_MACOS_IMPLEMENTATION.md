# 09 macOS 实现、自动构建与协作

## 0. 执行责任

macOS 平台的源码同步、依赖准备、构建、测试、打包、提交和推送全部由 Codex 对应平台代理完成。用户不负责安装工具链或运行命令。

默认协同流程：

```text
fetch origin -> rebase product/relaydesk-v1 -> 平台开发
-> 小功能 commit -> 任务完成 push -> A0 合并/push
-> GitHub Actions 自动构建 -> artifact 输出 dist/macos/<commit>/
```

若当前 Codex 主机不是 macOS，优先使用 GitHub Actions 对应 runner，不要求用户提供开发机。真实系统权限与跨屏行为只进入最终验收。

## 1. P0 平台目标

- macOS 14+ Apple Silicon 本地构建；
- Apple Silicon arm64 优先；
- Xcode 与 Command Line Tools；
- CMake；
- Qt 6.7+；
- OpenSSL/Homebrew 依赖；
- `.app` 和测试 DMG；
- 与 Windows 共用紧凑 Qt 单栏首页，并提供 macOS menu bar 后台入口。

上游 CI 对不同架构/系统版本可能有额外要求，以 v1.26.0 官方构建文档、CMake targets 和实际依赖探测结果为准。该 tag 未发现仓库级 `CMakePresets.json`。

## 2. Phase 0 原版自动构建

A5 先从远程同步同一产品 commit，然后自行执行：

```bash
git fetch origin --prune
git switch agent/a5/BASE-003-macos-baseline
git rebase origin/product/relaydesk-v1
bash product/scripts/setup-macos.sh
bash product/scripts/build-macos.sh --config Release --tests
bash product/scripts/package-macos.sh --config Release
```

本机环境不可用时，A5/A0 触发 `.github/workflows/relaydesk-build.yml` 的 macOS job，读取日志并自动修复。不得要求用户安装 Xcode、Homebrew、Qt 或运行命令。

A5 必须记录：

```text
macOS version
hardware architecture
Xcode version
AppleClang version
CMake version
Qt version/path
OpenSSL version/path
Homebrew prefix
configure/build/test command
.app path
permissions granted
```

Universal binary 可通过上游支持的 `CMAKE_OSX_ARCHITECTURES` 评估，但 P0 不因 Intel 产物阻塞 Apple Silicon MVP。

## 3. 上游 macOS 平台层

核查 `OSX*` 的 `.cpp/.m/.mm`：

- event tap；
- key state；
- clipboard/pasteboard；
- Cocoa app；
- media keys；
- power/screen wake；
- accessibility；
- app lifecycle；
- bundle integration。

P0 不重写这些输入路径。新增平台代码尽量放独立 Objective-C++ adapter，并向共享 C++ 暴露窄接口。

### 3.1 共享 Qt 界面边界

macOS 必须直接消费 `product/docs/07_UI_UX_SPEC.md` 定义的共享信息架构和状态模型：默认
`560 × 420 logical px`、最小建议 `520 × 380`、`52 px` 顶栏、单行权限条、两行语义设备
条目和 `52 px` 迷你传输条。不得为 macOS 复制一套窗口、设备模型、权限模型或传输中心。

平台层只负责：

- `MacPermissionProbe` 与系统设置入口；
- menu bar template 图标、窗口恢复/激活和真正退出；
- Dock/Finder 的彩色 App 图标；
- 系统通知、bundle lifecycle 和原生可访问性属性。

系统字体、标题栏和菜单栏遵循 macOS，但不得改变页面层级、主操作语义或 permission/transfer
snapshot。设计基线是
`product/assets/design/relaydesk-compact-ui-approved-20260814.png`；它是实现输入，不是已完成证据。

## 4. 权限

`MacPermissionProbe`、`IMacPermissionBackend`、冻结的 `PermissionSnapshot` 和
`PermissionStatusModel` 是唯一权限链路。`MAC-037` 必须复用现有实现，不能创建 macOS 私有
UI 状态或用原生诊断字符串驱动业务。现有 probe/回前台 refresh 是实现基线；本轮需要把它们
接入已确认的紧凑首页、明确能力门控并补齐 UI/lifecycle 回归，在完成前不得把 `MAC-037`
标为 Done。

三项权限分别发布 `PermissionProbeEntry`，状态限定为冻结枚举：`Unknown`、`NotRequired`、
`Granted`、`Denied`、`NeedsAction`。每项在权限详情中必须有：

- 本地化名称和用途；
- 当前状态，以及状态不可确定时的保守提示；
- 受影响的具体能力；
- `canOpenSettings` 为真时的“打开系统设置”动作；
- 仅供日志使用的诊断，不直接展示未本地化原生错误。

首页只显示单行权限摘要；点击摘要进入上述三项详情。状态探测不弹系统授权框，系统 prompt
或设置跳转只能来自用户明确动作。应用收到 `Qt::ApplicationActive` 后对三项异步复检，
防抖合并连续激活事件，并通过既有 snapshot signal 刷新界面，不要求重启应用。

### Accessibility

使用 `AXIsProcessTrusted()` 做无提示状态探测，用于依赖辅助功能的输入控制。开发阶段不要求
用户操作；最终验收时用户按系统要求在：

```text
System Settings
  -> Privacy & Security
  -> Accessibility
```

授权实际 app 和/或 core executable（以真实上游行为为准）。

未授权时仅禁用实际依赖 Accessibility 的输入方向，并显示“打开辅助功能设置”；不得停止
文件监听、文件发送/接收、历史或设置页面。

### Input Monitoring

使用 `CGPreflightListenEventAccess()` 做无提示探测，并按当前系统和上游实际需要映射为
`Granted`、`NeedsAction` 或 `NotRequired`。不得因为 API 返回不确定而无条件宣称已授权。

未授权时只禁用依赖全局事件读取的输入方向；不依赖该权限的文件传输继续运行。只有用户
点击对应动作时才请求授权或打开“输入监控”设置。

### Local Network

通过现有异步 Network.framework Bonjour browser 保守探测：browser ready 可映射为
`Granted`，策略拒绝映射为 `Denied/NeedsAction`，无关网络或探测错误保持 `Unknown`；不得
把普通离线误报为用户拒绝。应用要：

- 提供正确用途说明；
- 首次发现/监听时触发系统提示；
- 检测失败并提供打开系统设置按钮；
- 不尝试绕过。

Local Network 影响局域网发现和直连，也可能使文件通道不可达；它不应被描述为“输入权限”。
Local Network 未授权时仍允许查看历史、修改设置和管理已保存设备，连接能力按真实网络结果
降级。反向要求同样成立：只缺 Accessibility/Input Monitoring 而 Local Network 可用时，
文件发送与接收不得被禁用。

### 系统设置与回前台复检

`openSystemSettings(PermissionKind)` 只接受上述三种 macOS kind，并打开对应隐私页；深链失效
时回退到最接近的 Privacy & Security 页面并返回稳定错误。不得在 probe 回调里启动长时间
扫描、同步等待系统设置或反复触发 prompt。

用户从系统设置回到 RelayDesk 后，`MacPermissionProbe::refresh()` 必须重新读取
Accessibility、Input Monitoring 并启动/更新 Local Network 异步探测；每一项独立更新，
不得因为一项先返回就沿用另一项的假状态。

### 升级

应用签名、bundle path 或 executable 变化后，旧授权可能失效。升级测试必须覆盖：

- 原路径覆盖安装；
- 新版本；
- 删除旧权限后重新授权；
- core/helper 是否独立出现。

## 5. App Sandbox

P0 目标是直接分发/公证的桌面应用，不以 Mac App Store Sandbox 为目标。不要在未评估输入注入和任意用户选取文件访问前开启 sandbox。

## 6. 文件系统

### 协议与本地名称

- 协议使用 UTF-8 NFC `/` 相对路径；
- macOS 文件系统可能以不同 Unicode normalization 呈现，比较时要规范化；
- `:`/`/` 和 NUL 处理遵循安全 PathPolicy；
- 不跨平台同步 xattr/resource forks/ACL；
- executable bit：macOS→macOS 可作为 P1，跨平台 P0 不保证。

### Symlink

P0 不跟随接收目录内 symlink。目录逐级打开/创建时验证；发送端遇到 symlink 默认跳过并列出，或整个任务拒绝，策略必须一致。

### Quarantine

收到文件是否设置 quarantine 由平台策略评估。不要人为清除系统安全属性来“方便打开”。应用不自动执行收到内容。

## 7. 文件 I/O

- staging：`Downloads/RelayDesk/.incoming/<transferId>`；
- 使用 `QSaveFile`/安全 rename 或平台适配；
- APFS rename 通常可原子，但跨 volume 目标必须处理；
- 用户选择外置盘时检查卷是否仍挂载；
- 文件名大小写冲突需基于目标卷实际语义安全处理；
- app nap/系统内存回收不能导致传输状态丢失，复用上游 power/lifecycle 处理并真实测试。

## 8. 文件剪贴板（P1）

涉及：

- `NSPasteboard`；
- file URLs；
- promised files/remote references；
- Finder paste 触发；
- Objective-C++ bridge。

设计原则：

- 复制时只发布短期远程引用；
- 粘贴时由主应用启动传输；
- 不把大文件放 pasteboard；
- Finder 生命周期和 pasteboard ownership 变化要处理；
- P0 应用内 drop 不依赖此功能。

## 9. Finder Quick Action（P1）

优先轻量：

- Automator/Quick Action 或 Finder Sync extension 评估；
- 只通过本地 IPC 把路径交给主应用；
- extension 不保存私钥、不连接 peer；
- 失败时不影响 Finder；
- 打包、签名、entitlement 独立测试。

## 10. 通知

- 接收请求；
- 完成；
- 失败；
- 点击激活 app/打开目录；
- 遵守通知权限；
- 不自动打开文件。

## 11. 自动打包、签名与公证

发布链路参数化：

1. build `.app`；
2. 嵌套二进制/Qt frameworks 正确签名；
3. hardened runtime/entitlements 以实际需求最小化；
4. `codesign --verify --deep --strict`；
5. 打 DMG；
6. `notarytool submit`；
7. staple；
8. Gatekeeper 干净机验证。

凭据：

- Developer ID Application；
- team ID；
- notary profile/API credentials；
- CI secrets。

无凭据时生成内部未签名包并标注；不得在仓库写真实证书或密码。

## 12. Bundle 身份

集中配置：

- `CFBundleDisplayName`；
- `CFBundleIdentifier`；
- version/build；
- copyright；
- local network usage string；
- icons；
- URL scheme（P1）；
- helper identifiers。

改 bundle ID 会影响隐私授权，开发阶段尽早稳定。

## 13. macOS 测试

- macOS 14 与较新版本；
- Apple Silicon；
- Accessibility 未授权/已授权/被撤销；
- Local Network 未授权/被拒绝；
- 中文/英文系统；
- RIME/系统输入法；
- 睡眠、屏保、唤醒；
- Wi-Fi 切换；
- 外置盘拔出；
- case-sensitive volume（条件允许）；
- Unicode normalization；
- 10k small files；
- 10GB+；
- app upgrade 后权限；
- signed/notarized clean-machine run。

## 14. macOS 任务完成后的 Git 操作

每个小功能完成后 commit。macOS backlog 任务完成后，A5 自动：

```bash
git fetch origin --prune
git rebase origin/product/relaydesk-v1
# 运行受影响测试
git push -u origin HEAD
```

A0 合并并推送集成分支。A5 在 `product/working/handoffs/<task-id>.md` 写入 commit、构建命令、App/DMG artifact 和 Windows 需要验证的事项。

macOS Accessibility、Input Monitoring 与 Local Network 的真实点击授权不要求用户在开发中途完成，统一写入最终验收步骤。
