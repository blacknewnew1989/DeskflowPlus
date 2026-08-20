# UI-010 紧凑界面、原创品牌与托盘改版

## 结论

2026-08-14 用户确认的界面稿已经落实到共享 Qt 界面。8 月 13 日的 UI-011 收口提交
`8aba552b89a6a8a4600df3c5d4270e711de07416` 和
`relaydesk-phase4-20260813-03` 均为历史候选，不能作为当前交付证据。

当前实现提交及阶段文档/标签目标为 `c134126b95977ca6b97036be18dcfc33a4a3a09a`。
主窗口/托盘定向回归（offscreen 各连续
10 次、native 各 1 次）以及本地原生串行 CTest 98/98 均为 `PASS`。精确标签
`relaydesk-phase4-20260820-02` 的 run `32362194153` 为 `SUCCESS`：Windows 99/99、
macOS 100/100、双平台打包、草稿 Release 和自动生命周期均 `PASS`。分支 run `32356352794`
另记录七语言与托盘/关闭恢复 Windows 单机 GUI `PASS`。WIN-020 又以精确 portable 实际完成
输入核心启停、发现/文件监听、传输中心和设置持久化。Windows unsigned 提示人工确认、
macOS TCC/menu bar 与物理 Win↔Mac 仍为 `NOT_RUN`，不得由 hosted CI 替代。

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
| `998d18929`、`24c8777f` | 补齐输入目标布局、共享端口和紧凑操作区同步 |
| `20f68ee18`、`3f7ddd30` | 修复 Windows 托盘资源和主窗口回归运行资源 |

## 本地自动验证

| 范围 | 结果 |
|---|---|
| 当前候选原生串行 CTest | 98/98 PASS，47.41 s；日志 `product/working/windows-debug-ctest-20260820-131000.log` |
| 主窗口/托盘定向回归 | offscreen 各连续 10 次及 native 各 1 次 PASS |
| Qt 翻译 | 七个 `relaydesk_*.ts` 均为 182/182 个唯一、完整键；七个 QM 实际加载 PASS |
| Windows 安装树翻译闭包 | 七个 QM 文件集合、magic、大小、摘要及 `lconvert` 实际加载 PASS |
| Windows 单机真实运行 | WIN-020：七语言、托盘、手动地址、Server 核心 24800 启停、UDP 24802、动态文件监听、传输中心和传输设置/登录启动重启持久化 PASS |
| 产品 Python contracts | `product/tests` 26/26 PASS；`product/scripts/tests` 37/37 PASS；日志为 `product/working/product-tests-a624a9e40.log`、`product/working/script-tests-a624a9e40.log` |
| 品牌校验 | macOS 生成往返、Windows 生成、集中品牌配置均 PASS |
| macOS 权限前台刷新 | `ApplicationActive` 自动复检与 150 ms 合并回归 PASS；最终 App 系统设置往返待实测 |
| 包校验 | `validate-package.py` PASS：49 个必需文件、12 个 JSON、60 个协议向量 |
| 工作流定义 | YAML 解析、Windows staged install 与翻译报告路径契约 PASS |
| XML / `git diff --check` | PASS |

当前 Debug 增量构建为 PASS。Windows 本地 Release 打包因缺少原生 Strawberry Perl 未执行，
已回退到精确标签 Actions；权威平台编译仍以 Actions 的 MSVC 2022 + Qt 与 macOS
Clang/arm64 + Qt 结果为准。

## 品牌资源摘要

| 资源 | 字节 | SHA-256 |
|---|---:|---|
| `product/assets/branding/relaydesk-mark.svg` | 813 | `6f04f0686d6849ea9db722d0dc1238f8ab000c92e5cd03b7ec341904a76fb679` |
| `src/apps/res/RelayDesk.ico` | 26,154 | `2826b2f2e4e49208109819668eb5faecf07f5883dd80f19a16891f954368ae19` |
| `src/apps/res/RelayDesk.icns` | 107,265 | `eaa77c7fe6d7773c32c79cdb8bfdd50c645188460f2489832c1a405270da5218` |
| `deploy/mac/dmg-background.tiff` | 963,472 | `77b1dd2a0ff9dea1a15a14d81387b624c57fcef836249fed4c68613fa1f2e5de` |

## 双平台构建（当前候选）

- 当前产品提交及阶段/标签目标：`c134126b95977ca6b97036be18dcfc33a4a3a09a`。
- 注释标签：`relaydesk-phase4-20260820-02`，tag object
  `9398524f927f33ed58890a0f52cc9bdf20bd3075`。
- Actions run：`32362194153` `SUCCESS`；Windows `96403951016`、macOS `96403950941`、
  materials `96403950792`、draft release `96407573119`、macOS lifecycle `96407573193` 均 `SUCCESS`。
- Windows MSI/portable 与 macOS App/DMG 的 manifest、`SHA256SUMS`、本地 `Get-FileHash`、
  Release API digest 一致；下载目录 `dist/actions/32362194153`。分支 run `32356352794`
  的 `evidence-windows-gui-runtime/result.json` 另记录 Windows 单机七语言和托盘/关闭恢复 PASS。
- Windows CTest 99/99 及 TEST-005 19/19 PASS；macOS CTest 100/100 及
  生命周期 19/19 PASS。

## 明确保留的 NOT_RUN

以下事项需要真实操作系统授权或两台物理设备，不能由共享代码编译冒充 `PASS`：

1. macOS Local Network、Accessibility、Input Monitoring 三项真实授权、撤销和升级复检；
2. macOS 最终 App 的按能力门控、系统设置往返，以及真实 menu bar 最小化、恢复、设置、
   暂停/继续和退出交互；
3. Windows unsigned SmartScreen/UAC 提示的人工视觉确认；
4. Windows 与 macOS 物理双机的键鼠、滚轮、剪贴板和文件传输联调；
5. Developer ID、Apple notarization 与 Windows Authenticode（无真实签名凭据）。

`MAC-037` 保持 `IN_PROGRESS`。当前实现已经复用共享 Qt 界面和同一
`PermissionSnapshot`，并按三项能力分别门控；只有最终 App 的真实系统权限与前台交互
证据尚未完成，不得用单元测试冒充该项 `PASS`。
