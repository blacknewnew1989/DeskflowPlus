# R4 Windows 托盘生命周期验收记录

## 范围

本记录只覆盖 `agent/a4/r4-windows-tray-lifecycle@8ef6461ea1bf92fff948242d586d7a86dce4cf5b`
的 Windows native 主窗口生命周期。它不继承旧 `TRAY-001` 或 `WIN-020` 的状态。

## 结果

| 验收点 | 结果 | 当前证据 |
|---|---|---|
| WM_CLOSE close-to-tray | PASS | 真实 `deskflow.exe` 可见窗口收到 Windows WM_CLOSE 后隐藏，进程仍存活 |
| SW_MINIMIZE minimize-to-tray | PASS | 真实 `deskflow.exe` 可见窗口收到 Windows SW_MINIMIZE 后隐藏，进程仍存活 |
| close-to-quit | PASS | `closeToTray=false` 时，真实窗口 WM_CLOSE 后进程自然退出 |
| Windows tray 图标/context menu | NOT_RUN | 当前会话无法通过可靠 UIA 观测并归因图标和菜单；未合成 tray action |
| tray menu Show/Hide/Quit | NOT_RUN | 未通过真实 tray 菜单完成交互，不能以 QAction 或窗口消息替代 |
| macOS menu bar、物理交互、发布 | NOT_RUN | 不在本 Windows native 验收范围 |

因此 `R4-UI-010A` 仅在上述 Windows native 生命周期范围为 `PASS`，总项
`R4-UI-010` 保持 `IN_PROGRESS`。

## 构建与 Receipt

- 临时构建目录：`C:\Users\52323\AppData\Local\Temp\relaydesk-a4-r4-ui010a`；
- GUI 与既有 quit harness 构建：269/269；
- 只补齐 `deskflow-core` target 的增量构建：55/55；
- native receipt：`C:\Users\52323\AppData\Local\Temp\relaydesk-a4-r4-ui010a\ui010a-native-lifecycle.json`；
- receipt 字段：`windowVisible`、`actionPosted`、`hiddenAfterAction`、`aliveAfterAction`、
  `naturalExit`。三个场景均记录为预期值。

首次 native 启动在 GUI 同目录缺少 `deskflow-core.exe` 时停止于 core 启动前置条件。补齐该产物后，
三个 native 场景均完成。这是构建闭包问题，不是 tray production failure。

两次 `MainWindowQuitRegression` 尝试均在 Qt platform plugin/QStyleHints 初始化前失败，未进入
production QAction contract；该环境缺口不记为 product red 或 PASS。

## 清理

本切片 source diff 为零。验收使用的 portable config 位于临时 build 目录，结束后已删除；用户
`C:\Users\52323\AppData\Roaming\RelayDesk\RelayDesk.conf` 未改写。验收期间未写
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，结束后没有 `deskflow`、`deskflow-core`
或 `MainWindowQuitRegression` 进程残留。
