# UI-010 紧凑界面、原创品牌与托盘改版

## 结论

2026-08-14 用户确认的界面稿已经落实到共享 Qt 界面。UI-011 收口实现已推送到
`agent/a0/ui011-final-closeout` 的 `8aba552b89a6a8a4600df3c5d4270e711de07416`，本地针对性
回归为 `PASS`；精确产品 SHA 的 Windows x64 与 Apple Silicon macOS 构建、安装包核验
仍为 `IN_PROGRESS`。旧标签 `relaydesk-phase4-20260813-03` 早于本次改版，不能作为本次
交付证据。

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
| `5c2092203` | 修复 macOS `iconutil` 往返校验并同步 ICNS、ICO 与 DMG 品牌资源 |
| `4ee4576ea` | 使翻译包校验兼容 macOS 系统 Python 3.9 |
| `9ac7f0d79` | 恢复 macOS 配置、构建和打包入口的可执行权限 |
| `75b61df99` | 在 Windows 安装树中校验七语言 QM 闭包与可加载性 |
| `56568584f` | 以独立进程验证菜单和托盘退出确实终止应用 |
| `8aba552b8` | 补齐七语言权限语义并统一为 178/178 个完整键 |

## 本地自动验证

| 范围 | 结果 |
|---|---|
| UI-011 定向 Qt 回归 | 7/7 PASS：布局、菜单/托盘 true-quit、七语言、权限探针、权限状态与后台生命周期 |
| 菜单/托盘 true-quit | 2/2 PASS；保留 close/minimize-to-tray 设置时，两个独立进程均在 3 秒 watchdog 前退出 |
| Qt 翻译 | 七个 `relaydesk_*.ts` 均为 178/178 个唯一、完整键；七个 QM 实际加载 PASS |
| Windows 安装树翻译闭包 | 七个 QM 文件集合、magic、大小、摘要及 `lconvert` 实际加载 PASS |
| 产品 Python contracts | 29/29 PASS：macOS/Windows 翻译与打包契约 |
| 品牌校验 | macOS 生成往返、Windows 生成、集中品牌配置均 PASS |
| macOS 权限前台刷新 | `ApplicationActive` 自动复检与 150 ms 合并回归 PASS；最终 App 系统设置往返待实测 |
| 工作流定义 | YAML 解析、Windows staged install 与翻译报告路径契约 PASS |
| XML / `git diff --check` | PASS |

Windows 本地针对性编译使用 MinGW/Qt；权威平台编译仍以精确标签 Actions 的
MSVC 2022 + Qt 与 macOS Clang/arm64 + Qt 结果为准。

## 品牌资源摘要

| 资源 | 字节 | SHA-256 |
|---|---:|---|
| `product/assets/branding/relaydesk-mark.svg` | 813 | `6f04f0686d6849ea9db722d0dc1238f8ab000c92e5cd03b7ec341904a76fb679` |
| `src/apps/res/RelayDesk.ico` | 26,154 | `2826b2f2e4e49208109819668eb5faecf07f5883dd80f19a16891f954368ae19` |
| `src/apps/res/RelayDesk.icns` | 107,265 | `eaa77c7fe6d7773c32c79cdb8bfdd50c645188460f2489832c1a405270da5218` |
| `deploy/mac/dmg-background.tiff` | 963,472 | `77b1dd2a0ff9dea1a15a14d81387b624c57fcef836249fed4c68613fa1f2e5de` |

## 双平台构建（待回填）

- 计划标签：`relaydesk-phase4-20260814-01`
- 精确产品提交、tag object、Actions run/jobs：`IN_PROGRESS`
- Windows MSI/portable 与 macOS App/DMG 摘要：`IN_PROGRESS`
- Windows 安装生命周期与 macOS App/DMG 生命周期：`IN_PROGRESS`

## 明确保留的 NOT_RUN

以下事项需要真实操作系统授权或两台物理设备，不能由共享代码编译冒充 `PASS`：

1. macOS Local Network、Accessibility、Input Monitoring 三项真实授权、撤销和升级复检；
2. macOS 最终 App 的按能力门控、系统设置往返，以及真实 menu bar 最小化、恢复、设置、
   暂停/继续和退出交互；
3. Windows 与 macOS 物理双机的键鼠、滚轮、剪贴板和文件传输联调；
4. Developer ID、Apple notarization 与 Windows Authenticode（无真实签名凭据）。

`MAC-037` 保持 `IN_PROGRESS`。当前实现已经复用共享 Qt 界面和同一
`PermissionSnapshot`，并按三项能力分别门控；只有最终 App 的真实系统权限与前台交互
证据尚未完成，不得用单元测试冒充该项 `PASS`。
