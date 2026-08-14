# UI-010：macOS 同步紧凑界面、权限与托盘契约

- Message ID: `20260814-011325Z-UI-010-macos-ui-brand-sync`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-14T01:13:25Z`
- Base product SHA: `071e58956700ef09b3a7792070f021d9209833ee`
- Platform branch: `agent/a5/macos-ui-brand-refresh`
- Commit/tag/run: `product/relaydesk-v1@071e58956700ef09b3a7792070f021d9209833ee`
- Status: `READY`
- Affected contracts: `共享 Qt 紧凑首页、PermissionSnapshot 能力门控、品牌资源、托盘生命周期；无线协议变化`
- Tests: `git diff --check PASS；macOS 实现与真机权限测试 NOT_RUN`
- Blocker: `none`
- Requested action: `从指定产品提交建立分支，按下述边界实现并回写 ACK`
- In reply to: `N/A`

## Summary

用户已确认紧凑设计稿。共享窗口默认内容尺寸为 `560×420 logical px`，建议最小尺寸为
`520×380 logical px`；首页为单列设备列表、单行权限摘要与紧凑传输条。确认稿位于
`product/assets/design/relaydesk-compact-ui-approved-20260814.png`，SHA-256 为
`2F9CF97352AB9819EB5AA2B5D54B9EC9A4FBF171CEA56525FB7E2EF149CFBE94`。

权限必须按能力门控。Accessibility、Input Monitoring 与 Local Network 分别展示状态、用途与
设置入口，应用重新获得前台焦点后自动复检。输入权限缺失只禁用对应键鼠能力，不得阻断设备浏览
和文件传输；Local Network 缺失只降级局域网发现/连接。A5 复用共享 Qt 界面、
`PermissionSnapshot` 和权限状态模型，不另建一套 macOS 信息架构。

macOS 适配须同步完成以下工作：

- 为稳定 bundle ID 配置用途文案、系统设置深链和前台复检；
- 使用共享原创 Logo 生成 `.icns`，保证 Dock、Finder、About 与 DMG 品牌一致；
- 菜单栏使用可辨识的单色 template 图标，不直接使用彩色应用图标；
- 支持独立的“最小化到托盘”和“关闭到托盘”开关；菜单栏提供打开 RelayDesk、暂停/继续共享、
  偏好设置与真正退出；真正退出须停止输入捕获、发现、传输与核心进程；
- 覆盖 `not-determined/granted/denied/revoked/upgrade` 权限状态、窗口恢复、最小化/关闭到菜单栏、
 真正退出与 unsigned `.app/.dmg` 打包测试。

系统设置中的最终授权点击和真实双机行为保留为 `NOT_RUN`，待用户最终验收。完成后请在
`product/working/platform-sync/macos/` 追加 ACK，写明产品基线、分支、提交、测试和仍为
`NOT_RUN` 的真机项。
