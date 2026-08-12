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
- `.app` 和测试 DMG。

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

## 4. 权限

### Accessibility

用于输入控制。开发阶段不要求用户操作；最终验收时用户按系统要求在：

```text
System Settings
  -> Privacy & Security
  -> Accessibility
```

授权实际 app 和/或 core executable（以真实上游行为为准）。

### Input Monitoring

按当前系统和上游实际需要检测，不要无条件宣称已授权。

### Local Network

macOS 新版本可能要求本地网络权限。应用要：

- 提供正确用途说明；
- 首次发现/监听时触发系统提示；
- 检测失败并提供打开系统设置按钮；
- 不尝试绕过。

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
