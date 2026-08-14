# UI-010 紧凑界面、原创品牌与托盘改版

## 结论

2026-08-14 用户确认的界面稿已经落实到共享 Qt 界面。当前本地实现与针对性回归为
`PASS`，精确标签的 Windows x64 与 Apple Silicon macOS 构建、安装包核验仍为
`IN_PROGRESS`。旧标签 `relaydesk-phase4-20260813-03` 早于本次改版，不能作为本次交付证据。

## 已确认设计输入

- 设计图：`product/assets/design/relaydesk-compact-ui-approved-20260814.png`
- 设计图 SHA-256：
  `2f9cf97352ab9819eb5aa2b5d54b9ec9a4fbf171cea56525fb7e2ef149cfbe94`
- 默认内容尺寸：560×420 logical px；最小 520×380 logical px。
- 视觉基线：Ink `#18262D`、Teal `#1EA99A`、Coral `#E86659`、Cloud `#F4F7F7`。
- 信息结构：52 px 顶栏、常驻单行权限摘要、紧凑设备区、52 px 传输摘要；权限详情和
  传输次要动作按需展开。

## 实现范围

- `UI-010`：把原 Server/Client + 多 Dock 首页改为紧凑单栏产品首页；设备、配对、接收
  offer 和传输中心在小窗口下保持可用。
- 权限体验：首页始终保留一个低干扰状态条；详情逐项展示状态、用途、受影响能力、处理
  提示和系统设置动作；快照刷新保留键盘焦点，长文案使用省略号并保留完整辅助说明。
- `BRAND-002`：新增原创“双设备 + 中继点”SVG 单源，并派生浅/深主题、symbolic、
  Windows ICO 与 macOS ICNS；窗口、About、托盘/menu bar 和平台打包统一消费集中配置。
- `TRAY-001`：最小化到托盘与关闭到托盘分别配置；菜单提供显示、暂停/继续共享、设置和
  真正退出。退出同步停止传输、发现、核心与托盘；中断传输先写入可恢复状态。
- 恢复窗口只按屏幕可见区域裁切，不覆盖用户主动放大的尺寸。

## 实现提交

| 提交 | 内容 |
|---|---|
| `071e58956` | 权限、紧凑界面、品牌与托盘产品规范 |
| `44a95cde0` | 紧凑设备与传输面板 |
| `ce3f81d7f` | 安全停止共享与可恢复传输 |
| `23ac2cf4e` | 原创图标及 Windows/macOS 资源接线 |
| `8e0819174` | 可操作、可访问的权限详情 |
| `d0d449edc` | 启动中的核心安全停止 |
| `d419b56f2` | 紧凑首页与最小化托盘生命周期 |

## 本地自动验证

| 范围 | 结果 |
|---|---|
| `PermissionStatusModelTests` | 7/7 PASS |
| `DevicesDockTests` | 16/16 PASS，包括 520×380 级别紧凑布局、权限详情与 reparent |
| `TransferCenterDockTests` | 4/4 PASS |
| `SettingsTests` | 15/15 PASS，包括最小化/关闭到托盘设置互不联动 |
| `ProductStringsTests` | 5/5 PASS |
| `FileTransferRuntimeTests` | 18/18 PASS，包括停机中断与检查点保留 |
| 产品 Python tests | 15/15 PASS |
| 品牌校验 | PASS：14 个品牌值、13 个消费者、5 份 SVG |
| Qt 对象编译 | MainWindow、Messages、SettingsDialog、AboutDialog、CoreProcess PASS |
| Qt 翻译 | zh_CN 350 条、RelayDesk en/zh_CN 各 177 条完成 |
| XML / `git diff --check` | PASS |

Windows 本地针对性编译使用 MinGW/Qt；权威平台编译仍以精确标签 Actions 的
MSVC 2022 + Qt 与 macOS Clang/arm64 + Qt 结果为准。

## 品牌资源摘要

| 资源 | 字节 | SHA-256 |
|---|---:|---|
| `product/assets/branding/relaydesk-mark.svg` | 813 | `6f04f0686d6849ea9db722d0dc1238f8ab000c92e5cd03b7ec341904a76fb679` |
| `src/apps/res/relaydesk.ico` | 27,645 | `0dca7886af39aead9f09f592e575707a1cf732c68194cc558559b10ad4f84de3` |
| `src/apps/res/RelayDesk.icns` | 87,419 | `110c44bdf9ecf3e2c9760a5b966a015518c8e19de1f392a76c3a9ff1dde58ad1` |

## 双平台构建（待回填）

- 计划标签：`relaydesk-phase4-20260814-01`
- 精确标签提交、tag object、Actions run/jobs：`IN_PROGRESS`
- Windows MSI/portable 与 macOS App/DMG 摘要：`IN_PROGRESS`
- Windows 安装生命周期与 macOS App/DMG 生命周期：`IN_PROGRESS`

## 明确保留的 NOT_RUN

以下事项需要真实操作系统授权或两台物理设备，不能由共享代码编译冒充 `PASS`：

1. macOS Local Network、Accessibility、Input Monitoring 三项真实授权、撤销和升级复检；
2. macOS 按能力门控及真实 menu bar 最小化、恢复、设置、暂停/继续和退出交互；
3. Windows 与 macOS 物理双机的键鼠、滚轮、剪贴板和文件传输联调；
4. Developer ID、Apple notarization 与 Windows Authenticode（无真实签名凭据）。

`MAC-037` 保持 `IN_PROGRESS`，macOS owner 必须复用共享 Qt 界面与同一
`PermissionSnapshot`，不得另造平台专用首页或把任一权限缺失升级为全局阻断。
