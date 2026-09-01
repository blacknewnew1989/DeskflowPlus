# R4 Windows 权限运行时证据

## 范围

本报告记录 `R4-UI-003B` 在 Windows 当前主机的只读动态验收。基线为
`agent/a0/redevelop-p0@9524c90381f8b06df850816fdb0dfc9debcae68d`。

验收只覆盖：

- production `WindowsFirewallProbe` 对受控、本进程 loopback TCP 端口从未监听到监听的状态转换；
- production `MainWindow`、`PermissionStatusModel` 与 `DevicesDock` 权限卡对同一 current-host probe
  snapshot 的一致性。

验收未创建、修改或删除 Windows Firewall 规则，也没有修改 production 源码。

## 运行结果

- A0 fresh2 C 盘目录构建：282/282，退出 0；
- 临时槽 `controlled-listener`：3 passed、0 failed、0 skipped，退出 0，100 ms；
- 临时槽 `current-host-mainwindow`：3 passed、0 failed、0 skipped，退出 0，601 ms；
- 端口槽先释放本进程 loopback 临时端口，probe 得到 `NotListening`，再由同一进程绑定该端口，
  probe 得到 `Listening`；没有修改防火墙规则；
- current-host 槽通过真实 `MainWindow` 创建的 production `WindowsFirewallProbe`，等待刷新完成，
  逐项比对 probe snapshot 与 `PermissionStatusModel` 的 Windows Firewall/ListeningPort kind、state、
  error code，并比对 DevicesDock 权限卡的标题和文案；没有注入 fake snapshot。

## 防火墙不变复核

验收前后使用同一只读 canonical `Get-NetFirewallRule` 投影：
`Name`、`DisplayName`、`Enabled`、`Direction`、`Action`、`Profile`。两次 JSON SHA-256 均为
`30EFF3C905815B4A06D4042340C5776BAC5E19DC4B398BE098C0D09DD5DC5626`，规则数均为 899。

此前中断的旧临时构建目录出现过 Ninja recovery，不能作为本报告证据；以上结果只采用 A0 fresh2。

## 状态与边界

因此 `R4-UI-003B` 在 Windows localhost/offscreen、current-host probe 与 production 权限卡一致性范围内
为 `PASS`。`R4-UI-003` 保持 `IN_PROGRESS`。

Windows 系统设置入口及其返回保持 `NOT_RUN`：验收开始前已存在 `SystemSettings` 进程（PID 20900，启动于
2026-08-30），无法把新窗口可靠归因到当前 UI 手势；同时 production launcher 是 detached 启动，未提供
可观察的返回契约。没有用 fake opener、直接 ShellExecute 或 open result 代替该证据。

macOS TCC、系统设置往返、native Windows/macOS 窗口、真实 LAN/多网卡、物理设备与正式发布均不属于本报告。
