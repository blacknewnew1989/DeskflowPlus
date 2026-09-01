# 项目状态

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: P0 重新开发 R4；旧 PASS 仍为历史候选证据，当前结论按重开发 SHA 独立记录
- Last updated: 2026-09-01
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus\working\relaydesk-redevelop-p0`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `agent/a0/redevelop-p0`
- Current product branch tip: `23940663abe959dab213454bf04a50049878ac81`
- Redevelopment starting tip: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Current verified redevelopment implementation tip: `7b17b81b745af74382c931e61e89b1455e5fb588`
- Redevelopment anchor: `relaydesk-pre-redevelop-20260830-01`
- Current verified redevelopment stage tag: none
- Last frozen protocol commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Current implementation: 既有实现正在按 `KEEP_UPSTREAM`、`REUSE_AFTER_AUDIT`、`REWRITE`
  和 `REMOVE` 重新审计。静态调用图不能转为本轮 PASS，详见
  `product/docs/reports/REDEVELOPMENT_BASELINE.md`。
- Historical verified stage tag: `relaydesk-phase4-20260820-02`（只作为历史候选，不证明重开发状态）

## 2026-08-30 P0 重新开发 R0

| ID | 状态 | 当前证据 / 下一步 |
|---|---|---|
| R0-001 | PASS | 基线报告提交 `30593b53e` 已普通推送；远端分支、产品基线和上游 tag 均已 API 复读 |
| R0-002 | PASS | macOS ASan 定位两处测试回调 `stack-use-after-scope`；局部连接 context 按逆序析构先断开。修复后 settings-only 50/50、ordered 50/50、ASan CTest 101/101，clean run `33330456697` 双平台全绿 |
| R0-003 | PASS | fresh build 的 `RelayDeskConflictResolverTests` 连续 50/50 PASS；旧 debug 目录卡住未复现，不认定源码缺陷 |
| R0-004 | PASS | E4 限定：同机双进程 discovery/pair/trust/TLS 单向 1 MiB+ 文件；Windows Debug/Release 10/10，run `33326619207` Win #98、Mac #99 PASS |
| R0-005 | IN_PROGRESS | `-01@9905434d0` run `33466625278` 与 `-02@21c454c1d` run `33470396960` 均因 Windows Composition 控制槽 300s/`0xC0000409` 失败；`-03@23940663a` run `33473271512` 中 controls 已通过，但 mini-bar 槽 300s timeout，三次失败均保留且不重跑。`211b8eb08`、`5694d8b0c` 已关闭测试生命周期与 cancel menu 门闩问题；`5a3b81e3b` / A0 `7b17b81b7` 再将 controls、mini-bar 作为同一 EXE 的独立 CTest 进程，轻量聚合保留其余10槽。Windows fresh Release 两个重型槽各3/3、轻量聚合 PASS、完整 CTest 103/103、汇总守卫退出0，独立review GO。下一唯一候选为未占用 `relaydesk-phase4-20260901-04`；同SHA Windows package/log/artifact/digest/draft Release齐全后才可关闭 Windows 发布门槛 |
| R0-006 | FINAL_ACCEPTANCE_REQUIRED | 物理 Win↔Mac、macOS TCC/menu bar 和 unsigned 系统交互留最终验收 |
| R0-007 | PASS | A5 已在 `coord/platform-sync` 推送 clean-run ACK `0661191ae` 和 lifecycle 终态附录 `a8eb7e7eb`；准确分列重开发 ref 与未合入产品 ref，R0-002 跨平台闭环 |
| NET-001 | PASS | 普通 Git push 已恢复并推送 `30593b53e`、`72008201e` 和 coordination commits；保留间歇风险记录 |

### R3 当前纵向切片

| ID | 状态 | 当前证据 / 下一步 |
|---|---|---|
| R3-CTRL-001 | PASS | receiver 通过 queued production intent 直接 pause/resume/cancel；Windows Debug/Release 独立 10/10，run `33333471632` Win #98、Mac #99 PASS |
| R3-FILETREE-001 | PASS | 一次 production send 传输独立文件+嵌套文件夹+空目录；fresh Win 两配置 10/10，run `33341572421` Win #98、Mac #99 PASS |
| R3-LISTENER-RESUME-001 | PASS | 同一 peer 对象 listener stop/start 后从非零 durable offset 恢复；sidecar production 清理已修复，fresh Win 两配置 10/10、hosted Win/Mac PASS |
| R3-PROCESS-RECOVERY-001 | PASS | 正常退出后的 Windows localhost OS 子进程恢复已覆盖 receiver/sender 单文件 relaunch 与 receiver 文件树 relaunch；Store 目录 entry、shutdown 生命周期已收口。run `33385968319@043d6b3fb`：Windows 101/101、macOS 102/102，FileTransferRuntime #94/#95 与 TwoProcess #99/#100 PASS，macOS hosted lifecycle PASS。crash/强杀/断电进入 R5 `NOT_RUN`；物理 Win↔Mac、TCC、人工安装与正式发布不属于本 PASS；详见 `product/docs/reports/R3_PROCESS_RECOVERY_RUNTIME.md` |

### R4 UI / 平台基线

| ID | 状态 | 当前证据 / 下一步 |
|---|---|---|
| R4-UI-001 | PASS | `R4-UI-001A` 已验证 trust card 成功动作与 revoke 写失败反馈，`R4-UI-001B` 已验证 manual address，`R4-UI-001C` 已关闭 auto-accept primary 写失败的模态与 diagnostic 泄露；总项仅在 Windows localhost/offscreen production UI 范围关闭 |
| R4-UI-001A | PASS | owner `fb4e75f92` / A0 内容等价提交 `709533024`：真实 MainWindow/DevicesDock 手势持久化 auto-accept、成功 revoke tombstone/card、重复边界；revoke primary 写失败显示七语言非模态脱敏反馈，内存/card/backup reload 不谎称成功，恢复后成功动作清旧反馈。共享 Store 契约统一 primary committed/backup degraded。owner 双槽各3/3，MainWindow 15/15、DevicesDock 30/30、Store 11/11、PairingManager 10/10、PairingTrust 11/11、i18n 7/7；A0 fresh 278/278 与 MainWindow 15/15。证据见第10节，仅限 localhost/offscreen |
| R4-UI-001B | PASS | owner `d81c13e54` / A0 内容等价提交 `34f248170`：真实 MainWindow/DevicesDock manual-address dialog Add/Save 持久化 127.0.0.1 与自定义 ports，使 disabled/empty 的 DiscoveryRuntime listener 无需重启应用即启动；重开显示、Remove/Save 清空 store，runtime 不重复启动。owner 槽3/3、MainWindow16/16、DevicesDock30/30、DiscoverySettings22/22、DeviceDiscoveryRuntime10/10、AddressCandidate10/10；A0 fresh 278/278、槽3/3、MainWindow16/16。证据见第11节；不证明 probe 数据包/真实LAN |
| R4-UI-001C | PASS | owner `55388091f` / A0 内容等价提交 `38e955bd6`：真实 auto-accept 菜单手势在 primary 写失败时无模态，显示固定本地化脱敏 trust feedback，runtime/card/store 保持原状态；恢复写入后成功更新并只清旧 trust feedback。精确 parent RED 退出1、`modalSeen=1 feedbackVisible=0`；owner 新增槽3/3及相关完整目标全绿，UI/pairing 双review GO；A0 fresh 构建7/7、槽3/3。证据见第12节 |
| R4-UI-002 | PASS | owner `b6a37091f` / A0 内容等价提交 `a2cb8a2af`：两个真实 UDP discovery/pairing runtime 与 production DevicesDock 完成 Pair、六位 SAS Confirm、双向独立指纹 trust 持久化、重复配对边界及独立 Cancel 无 trust。owner Confirm/Cancel 各 3/3、10 个完整相关目标全绿；A0 fresh Confirm/Cancel 与 PairingTrustRuntime 11/11。证据见 R4 基线报告第 9 节，仅限 localhost/offscreen |
| R4-UI-003 | IN_PROGRESS | `R4-UI-003A` 已验证权限卡与分项 capability gating，`R4-UI-003B` 已验证真实 Windows current-host probe 的受控端口转换和 production 权限卡一致性；Windows 系统设置返回、macOS TCC/系统设置往返和原生窗口仍 `NOT_RUN` |
| R4-UI-003A | PASS | owner `16f0dbe39` / A0 内容等价提交 `bfe7a5d86`：真实 MainWindow/DevicesDock 中 Windows Firewall Denied 与 ListeningPort NeedsAction 分别禁用 Pair 并显示固定文案，恢复 Granted 后同一窗口无需重启重新启用并发出 pairing intent；macOS 条件分支保持 LocalNetwork 与输入权限分离。owner 新增槽最终3/3、MainWindow18/18、DevicesDock30/30、PermissionModel9/9、Snapshot4/4、WindowsProbe22/22，UI/platform 双review GO；A0 fresh 构建4/4、槽3/3。证据见第13节，仅限 snapshot 注入的 localhost/offscreen contract |
| R4-UI-003B | PASS | A0 fresh2 构建 282/282；临时 dynamic slots `controlled-listener` 与 `current-host-mainwindow` 均为 3/0/0。真实 WindowsFirewallProbe 验证本进程受控 loopback 端口 `NotListening`→`Listening`，真实 MainWindow probe snapshot 与 PermissionStatusModel/DevicesDock 权限卡逐项一致；前后 Firewall 规则投影 SHA-256 相同、均为 899 条。系统设置打开/返回仍 `NOT_RUN`，证据见 `product/docs/reports/R4_WINDOWS_PERMISSION_RUNTIME.md` |
| R4-UI-004 | PASS | `b036e1f7b` / A0 `5aa0bfc4b`：TransferUiRuntime 消费 typed `TransferStartResult`，NotRunning/PeerUnavailable 写入现有本地化反馈并保留选择；红测 1→0，完整 TransferUiRuntime/DevicesDock 目标退出 0 |
| R4-UI-005 | PASS | owner `5ac175f90` / A0 内容等价提交 `36d83e77c`：真实 FileTransferRuntime/TLS localhost offer 已进入 production composition/model/DevicesDock；真实按钮 accept 完成文件 SHA/Completed row/状态清理，reject 不落文件且 sender typed Rejected。owner 三完整相关目标退出 0，A0 fresh Composition 11/11；命令与日志见 R4 基线报告第 6 节。Ask、原生窗口系统与物理双机仍 `NOT_RUN` |
| R4-UI-006 | PASS | `R4-UI-006A` 已验证 production widget pause/resume/cancel，`R4-UI-006B` 已验证真实 history retry；总项仅在 localhost/offscreen 范围关闭，原生/物理仍未覆盖 |
| R4-UI-006A | PASS | owner `361b3ba2e` / A0 内容等价提交 `9c38f79e9`：修复 receiver Transferring snapshot 未发布 `canPause` 的 production 缺陷；真实 Transfer Center 按钮完成双端 Paused 稳定、Resume SHA/清理与 Cancelled Keep partial 验收。owner 新增槽 3/3、Composition 12/12、Dock 4/4、FileRuntime 56/0/4；A0 fresh 槽 3/3、Composition 12/12。证据见 R4 基线报告第 7 节，仅限 localhost/offscreen |
| R4-UI-006B | PASS | owner `52fd8341a` / A0 内容等价提交 `248331980`：pre-Accept SourceChanged 形成旧 outgoing typed Failed/history/canRetry；真实 Retry 按钮产生 Applied、新 ID 与第二 offer，二次 Accept 后 SHA 与当前源一致，旧 Failed history/retry availability/recovery 状态保持正确。owner 槽 3/3、Composition 13/13、Dock 4/4、FileRuntime 56/0/4、HistoryStore 11/11；A0 fresh 槽 3/3、Composition 13/13。证据见 R4 基线报告第 8 节 |
| R4-UI-007 | PASS | `R4-UI-007A` 已验证真实后台传输驱动迷你条刷新及 Pause/Resume，`R4-UI-007B` 已验证 MainWindow details 路由到自身 TransferCenterDock；总项仅在 localhost/offscreen 范围关闭 |
| R4-UI-007A | PASS | owner `da3497e69` / A0 内容等价提交 `6a0664575`：真实 FileTransferRuntime/TLS/trust/discovery 经 production TransferRuntimeComposition/TransferCenterModel 驱动 TransferMiniBar 从隐藏到非零进度，标题、metrics、percent 与 model 精确一致；真实主按钮 Pause 双端稳定、Resume 后完成且 SHA 一致，body/键盘发出 details intent。UI/transfer 双review GO；owner-worktree A0 槽3/3，集成 A0 构建4/4、槽3/3。证据见第15节，仅限 localhost/offscreen |
| R4-UI-007B | PASS | owner `1a61ab239` / A0 内容等价提交 `678695f1c`：真实 MainWindow 自身 model/MiniBar/Dock 在无 active row 时均不可操作；注入 active UI row 后 MiniBar 自动显示，真实 body 点击经 production connect 使 dock 可见且前置，真实 list 可访问对应 TransferId。owner 槽3/3、MainWindow19/19、MiniBar4/4、Dock4/4，UI/transfer 双review GO；A0 构建4/4、槽3/3。证据见第16节，仅限 offscreen |
| R4-UI-008 | PASS | owner `258d7aa6e` / A0 内容等价提交 `941149532`：validated opener 拒绝与 history load/persist error 已接入 Transfer Center 本地化非模态反馈；打开成功只清打开失败，不覆盖仍有效的历史错误。owner 四完整目标退出 0，A0 四目标构建退出 0；命令与日志见 `product/docs/reports/R4_UI_PLATFORM_BASELINE.md` 第 5 节，fake opener 不证明 Explorer/Finder/OS shell 实际打开 |
| R4-UI-009 | PASS | owner `8aa690359` / A0 内容等价提交 `d015027e9`：真实 Save 持久化三字段并重开精确回显；同一 MainWindow 出现 pending offer 后，真实 DevicesDock 设置按钮打开专用 dialog，Save 后 incoming-offer runtime 无需重启更新且 store 复读一致。owner fresh 282/282、两槽各3/3、MainWindow19/19、TransferSettings10/10；A7 GO，A0构建4/4、两槽各3/3。仅限 Qt localhost/offscreen，详见 `product/docs/reports/R4_SETTINGS_RUNTIME.md` |
| R4-UI-010 | IN_PROGRESS | `R4-UI-010A` 已在 Windows native 当前 SHA 验证 WM_CLOSE close-to-tray、SW_MINIMIZE minimize-to-tray 和 close-to-quit 生命周期；`R4-UI-010B` 已在 hosted macOS 当前 SHA 验证 CTest 的 offscreen menu/tray Quit QAction contract、ad-hoc bundle 与隔离 install lifecycle。native tray 图标/菜单 Show/Hide/Quit、物理 macOS menu bar、物理交互和发布仍为 `NOT_RUN` |
| R4-UI-010A | PASS | Windows native 有界生命周期证据：真实 `deskflow.exe` 的 WM_CLOSE 在 closeToTray 时隐藏并保持进程存活，SW_MINIMIZE 在 minimizeToTray 时隐藏并保持进程存活，closeToTray=false 时自然退出。仅此子范围为 `PASS`；tray 图标/菜单交互未可靠观测，详见 `product/docs/reports/R4_WINDOWS_TRAY_LIFECYCLE.md` |
| R4-UI-010B | PASS | run `33464083567@38247729b` 的 macos-14 package #`99720205727` CTest 102/102；QuitRegression menu #14 0.23 s、tray #15 0.21 s，均为 offscreen QAction contract。isolated lifecycle #`99723079671` 的 TEST-005 PASS，artifact 下载 digest 与 package artifact API digest 一致。仅限 hosted build/CTest、ad-hoc bundle 与隔离生命周期，详见 `product/docs/reports/R4_MACOS_MENU_CONTRACT.md` |

完整调用链、自动证据边界和首个修复切片见 `product/docs/reports/R4_UI_PLATFORM_BASELINE.md`。

### R0-002 最终证据

- 根因：`FileTransferRuntime` 析构期间发出错误信号，测试使用长生命周期 `this` 作为 connect context，
  引用捕获的局部量已先离开作用域；macOS ASan 报告 `stack-use-after-scope`。
- 修复：`3332378cf` 在 `QStringList errors` 后声明局部 `QObject` 并用作连接 context；
  `80a49b02c` 对 full ASan 暴露的第二处同模式问题采用相同的局部 context。逆序析构时先销毁
  context 并断开连接，再销毁被捕获局部量和 runtime。
- 定向审计：提交前仅搜索 `src/unittests/relaydesk/app` 中 `connect(..., this, [&]` 等引用捕获；
  除上述两处 ASan 已确认问题外，没有发现第三处可确认的析构期悬空引用，未做全仓批量替换。
- ASan run `33329642343`：macOS job `99305807755` 成功；settings-only 50/50、ordered 50/50、
  CTest 101/101，AutoReconnect #95、TwoProcess #99 通过，ASan error/summary 与 SIGABRT 为 0。
- clean run [`33330456697`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33330456697)
  对 `agent/a0/redevelop-p0@b6a8852d0f1892ce5d5d493f8ec8fd85251101a9` 为 `SUCCESS`：Windows
  100/100（AutoReconnect #94、TwoProcess #98）、macOS 101/101（AutoReconnect #95、TwoProcess #99）、
  Windows TEST-005 和 macOS install lifecycle 均为 `PASS`。
- artifacts：Windows `9737664755` / `sha256:58bf837e1c640f3ffa383bf2ea7fa40ac3708ff0a2b5fd11bbe7eb0c56125bed`；
  macOS `9737551418` / `sha256:ba01e763ff9add351f5e283db6a119d206e10c1258ab991df0f1338387dab4cc`；
  macOS lifecycle `9737670033` / `sha256:12bca85604fa8338dc89c1df473b56ccd8e7998734dccefcc876ece9aaf51839`。
- 产品分支仍为 `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`。上述结论只属于尚未合入的
  重开发分支；精确阶段标签、Release、物理 Win↔Mac 和系统权限交互仍按 R0-005/R0-006 处理。

以下 2026-08-20 及更早内容全部为重开发前历史证据，只能用于选择候选测试和复现缺陷，不构成
当前 R0 PASS。

## 2026-08-20 收口复验

| ID | 状态 | Owner | 当前证据 / 下一步 |
|---|---|---|---|
| PAIR-006 | PASS | A2/A3/A0 | `dc4b7efed`、`19e5ab583`、`3f6efb6d1`、`d4d312e88`、`088702900`；撤销确认、TLS 断开、重连拒绝与 520×380 更多菜单已接通；MSVC/Qt 6.10.1 六目标 CTest 6/6 PASS |
| CTRL-002 | PASS | A6/A7/A0 | 接收方 queued production intent 已在独立 receiver 进程执行；Windows Debug/Release 各 10/10，run `33333471632` Win/Mac TwoProcess PASS。物理 Win-Mac 仍为 `FINAL_ACCEPTANCE_REQUIRED`。 |
| DISC-005 | PASS | A2/A3/A0 | 手动地址录入、保存和定向探测已集成；原生串行 CTest 98/98 PASS。真实局域网发现链路仍为 `NOT_RUN`。 |
| CONFLICT-003 | PASS | A6/A3/A0 | `Ask` 的逐文件用户决策和运行时链路已集成；原生串行 CTest 98/98 PASS。真实双机传输决策链路仍为 `NOT_RUN`。 |
| WIN-019 | IN_PROGRESS | A4/A7/A0 | 精确标签 Windows 包与自动安装生命周期、Windows 单机七语言和托盘/关闭恢复 GUI 自动化均已 PASS；WIN-021 已消除本机 MSI v14.51 运行库门槛，安装/修复/卸载、服务、防火墙、开始菜单和 GUI 启动 PASS；unsigned SmartScreen/UAC 人工交互仍为 `NOT_RUN`。 |
| WIN-021 | PASS | A4/A0 | 原 MSI 将最低 VC++ runtime 设为 v14.51，主机 v14.44.35211 因此两次 1603；修复包要求 v14.44，本机安装/修复/卸载 exit 0、GUI 启动与残留检查 PASS，见 `product/docs/reports/WIN-021_WINDOWS_MSI_RUNTIME_REQUIREMENT.md`。 |
| WIN-020 | PASS | A0 | 精确 portable 的七语言、托盘、手动地址、输入核心 24800、发现 24802、动态文件监听、传输中心、传输设置及 HKCU 登录启动均已真实操作并回滚；见 `product/docs/reports/WIN-020_WINDOWS_SINGLE_HOST_RUNTIME.md`。 |
| MAC-038 | NOT_RUN | A5/A7/A0 | 精确标签 macOS App/DMG 与自动生命周期已 PASS；TCC/menu bar 与 Win↔Mac 物理双机仍需真实 macOS 对端。 |

## 2026-08-14 紧凑界面变更

- 用户已确认设计输入：
  `product/assets/design/relaydesk-compact-ui-approved-20260814.png`；SHA-256：
  `2f9cf97352ab9819eb5aa2b5d54b9ec9a4fbf171cea56525fb7e2ef149cfbe94`。
- 已确认范围：小巧单栏首页、原创“双设备 + 中继点”临时 Logo、最小化/关闭到 tray 或
  menu bar，以及 macOS 权限分项能力门控与平台适配。
- 设计确认是实现输入，不是完成证据；现有 `relaydesk-phase4-20260813-03` 安装包早于本次
  改版，不能用于证明下列任务已实现。

| ID | 状态 | Owner | 当前证据 / 下一步 |
|---|---|---|---|
| UI-010 | PASS | A3/A0 | 七语言、主窗口布局与托盘回归均已通过；精确标签 Windows 99/99、macOS 100/100、双平台打包和自动生命周期均 PASS |
| UI-012 | PASS | A3/A0 | 高级页输入角色和 Client 远端主机配置已接通；定向 offscreen 各连续 10 次与 native 各 1 次 PASS，精确标签双平台构建 PASS；实机交互仍独立验收 |
| BRAND-002 | PASS | A3/A4/A5 | SVG 单源、主题资源、ICO/ICNS/DMG 与 CMake 接线及本地品牌校验 PASS；精确标签双平台包已生成并四方摘要一致 |
| TRAY-001 | PASS | A3/A4/A5 | 最小化/关闭到 tray 独立设置及安全停机已实现；Windows 单机 GUI 自动化、精确标签双平台构建和自动生命周期 PASS；macOS menu bar 与物理双机仍为 `NOT_RUN` |
| MAC-037 | IN_PROGRESS | A5/A3/A0 | 三项权限能力门控、ApplicationActive 自动复检与 150 ms 合并回归 PASS；待最终 App 的系统设置往返前台实测 |

## 自动执行状态

| 项目 | 状态 | 证据 |
|---|---|---|
| origin 可读写 | PASS | bootstrap push and subsequent integration push succeeded |
| upstream fetch | PASS | official refs fetched from `deskflow/deskflow` |
| v1.26.0=760e3b9 | PASS | `760e3b99b00053647a96b405276bf614bd860075` |
| bootstrap commit | PASS | `9b0a4111141abe0a619d5eaeea87b8690b771f70` |
| integration branch push | PASS | remote branch tracks local product branch |
| Windows build | PASS | Phase 2 tag run `31655013105`; CMake/Ninja/MSVC build, CPack MSI/7Z/source, CTest 74/74 |
| macOS build | PASS | Phase 2 tag run `31655013105`; arm64/Qt 6.10.2 build, DMG/App/source, CTest 75/75 |
| GitHub Actions artifacts | PASS | Windows artifact `9164266512`; macOS artifact `9164146467`; 30-day retention |
| Phase 1 implementation | PASS | brand/i18n/device/discovery/pairing/trust/reconnect/device UI and permission guidance integrated through `ead6acbd5` |
| Phase 1 dual-platform CI | PASS | tag run `31621226862`; Windows 60/60, macOS 61/61; build/package/upload all succeeded |
| Draft Release publication | PASS | Phase 2 tag run `31655013105`; four delivery binaries downloaded and API/manifest/local SHA-256 agree |
| Windows installer lifecycle | PASS | TEST-005 run `31657498852`; real clean install/repair/major-upgrade/two uninstalls and residue checks PASS; report `product/docs/reports/TEST-005_WINDOWS_INSTALL_LIFECYCLE.md` |
| Final macOS bundle seal/lifecycle | PASS | TEST-005 run `31657596578`; symlink-preserving App ZIP, strict ad-hoc codesign, DMG verify/mount, isolated install/upgrade/uninstall and user-data preservation PASS |
| Protocol/interface freeze | PASS | tag `relaydesk-protocol-v1-20260813-01`, run `31672497950`; Windows 84/84, macOS 85/85; artifact IDs `9170492840` / `9170386546` |
| Cross-platform file safety adapters | PASS | integration `e6f5fe519`; run `31678206041`: Windows 87/87, macOS 88/88, strict App seal and installer/lifecycle jobs PASS |
| Incoming file runtime composition | PASS | `8f5a992f8`; run `31682728899`: Windows 87/87, macOS 88/88, strict App seal and macOS lifecycle PASS; artifacts `9174449354` / `9174307269` |
| Multi-file/folder/resume production path | PASS | `e742ba4a4`, `7d9bfcbf6`, `5941ebd85`; real two-file/folder and 20 MiB interruption/resume TLS loopbacks PASS |
| Product GUI/reconnect/permission composition | PASS | `479a0f78f`, `b251933dd`, `cc923dacc`, `0341c9b86`, `f79cc64dd`; targeted composition/reconnect/firewall tests PASS |
| Pairing input-layout composition | PASS | `05f92a1ab`; trusted input peer add/persist/idempotency/rejection tests PASS on Windows and macOS |
| Historical Phase 4 exact-tag release | PASS | tag `relaydesk-phase4-20260813-03`; run `31706167585`; Windows 89/89, macOS 90/90, Windows installer and macOS lifecycle PASS; unsigned draft Release published |
| UI-011 local closeout | PASS | product branch reached `939bbb3a0`; 7 Qt regressions, 29 Python contracts, Windows staged-QM loader and brand checks PASS |
| Current seven-language catalogs | PASS | `088702900`; en/es/it/ja/ko/ru/zh_CN are each 182/182; Qt catalog load and 14 translation contracts PASS |
| Current revoke-trust composition | PASS | `dc4b7efed` through `088702900`; clean MSVC/Qt 6.10.1 targeted CTest 6/6 PASS |
| Current exact-SHA dual-platform Actions | PASS | 标签 `relaydesk-phase4-20260820-02`（目标 `c134126b95977ca6b97036be18dcfc33a4a3a09a`），run `32362194153` SUCCESS；Windows/macOS 打包、草稿 Release 与 macOS 生命周期均 PASS |
| Windows single-host runtime | PASS | 精确 portable 实际完成七语言、托盘、手动地址、`deskflow-core.exe server` 及 24800 启停、UDP 24802、动态文件监听、传输中心和设置/登录启动重启持久化；见 WIN-020 |
| Current branch dual-platform CI | PASS | 产品实现 `1b1a24739`；run `32433749495` SUCCESS：Windows 99/99（34.41 s）、macOS 100/100（37.91 s）、macOS 生命周期 19/19；release job 按分支运行规则 skipped |
| macOS 14 单变量修复回归 | PASS | A5 `6457d481` / run `32444914659` SUCCESS：链接警告归零，macOS 100/100（37.76 s）、Windows 99/99（62.32 s，TEST-005 PASS）和 macOS 生命周期 19/19 PASS；已集成于 `32712c6b2` + `ea70650ff`，待产品标签回归 |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | PASS | A2/A3/A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; local Release asset SHA verification PASS |
| 2 文件传输 | PASS | A2/A6/A0 | tag `relaydesk-phase2-20260813-04`; run `31655013105`; Win 74/74, Mac 75/75; four assets triple-digest verified |
| 3 可靠性/UI | PASS | A3/A6/A7 | tag `relaydesk-phase3-20260813-01`; run `31691378517` SUCCESS; Win 88/88, Mac 89/89; physical Win↔Mac remains final acceptance |
| 4 平台/发布 | PASS | A4/A5/A7 | `relaydesk-phase4-20260820-02` / run `32362194153` SUCCESS；unsigned MSI/7Z/App ZIP/DMG、草稿 Release 与自动生命周期均 PASS；系统交互和物理验收独立保留 `NOT_RUN` |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows（历史 Phase 4 内部候选，2026-08-13）

- Commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`
- Tag / workflow run: `relaydesk-phase4-20260813-03` / `31706167585`
- Artifact: `relaydesk-windows-x64-05f92a1ab721f7fd8b893e47e05643d5988e1719` (ID `9183676968`)
- Artifact ZIP SHA-256: `d0cd7ab0aee49473d62cd0673a2f0b9e80c6b04a6906fc43c375b2f748161e2c`
- MSI SHA-256: `28340705a8c31d663cd5f10ea605679210c5fec393048c5a2070ae92335d2f07`
- Portable SHA-256: `51e88f915007d51f7efcbe0a9e8496720edebb2b1ac98371584070eedf22655d`
- Build result: PASS (CTest 89/89; unsigned MSI + portable 7Z + source packages)
- Installer result: PASS (clean install, repair, real MSI major upgrade, two uninstalls, service,
  firewall, residue and user-data preservation)
- Physical Win↔Mac runtime result: NOT_RUN; final user acceptance required

### macOS（历史 Phase 4 内部候选，2026-08-13）

- Commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`
- Tag / workflow run: `relaydesk-phase4-20260813-03` / `31706167585`
- Artifact: `relaydesk-macos-arm64-05f92a1ab721f7fd8b893e47e05643d5988e1719` (ID `9183524798`)
- Artifact ZIP SHA-256: `4e03738e2186ff214081546875594c9c463615401dd5e81130683ba2f371013f`
- App ZIP SHA-256: `ad1a56cd74b32a7ebb499b73376a019745fe3a8e42ce69f1e73bc0696430b8af`
- DMG SHA-256: `0377d49f7bbb9284f666f2033219b5f39c73d7a496238257881ef299a35e2b29`
- Build result: PASS (CTest 90/90; ad-hoc App ZIP + DMG + source packages)
- Lifecycle result: PASS (strict ad-hoc codesign, ZIP symlinks, DMG verify/mount, isolated launch,
  replace, App-only uninstall and user-data preservation)
- Physical Win↔Mac runtime and OS permission result: NOT_RUN; final user acceptance required

### 2026-08-20 当前 Phase 4 内部候选

- Commit: `a624a9e40f027c4165dd8838b61cbe98af68d7f2`。
- 本地 Debug 增量构建：PASS；原生串行 CTest 98/98 PASS（47.41 s），日志
  `product/working/windows-debug-ctest-20260820-131000.log`。
- 定向主窗口/托盘回归：offscreen 各连续 10 次及 native 各 1 次均 PASS。
- `product/tests`：26/26 PASS；`product/scripts/tests`：37/37 PASS；日志分别为
  `product/working/product-tests-a624a9e40.log` 和
  `product/working/script-tests-a624a9e40.log`。
- `validate-package.py`：PASS（49 个必需文件、12 个 JSON、60 个协议向量）。
- 当前阶段/标签目标：`c134126b95977ca6b97036be18dcfc33a4a3a09a`；注释标签
  `relaydesk-phase4-20260820-02`（tag object `9398524f927f33ed58890a0f52cc9bdf20bd3075`）。
- Actions run：`32362194153` `SUCCESS`；materials job `96403950792`、Windows job
  `96403951016`、macOS job `96403950941`、draft release job `96407573119` 和 macOS
  lifecycle job `96407573193` 均 `SUCCESS`。
- Windows：CTest 99/99 PASS；TEST-005 19/19 PASS；MSI 安装、修复、主版本升级、
  两次卸载、服务、防火墙、残留和数据保留均 PASS。
- macOS：CTest 100/100 PASS；生命周期 19/19 PASS；严格 ad-hoc codesign、App ZIP
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
- 草稿 Release：`RelayDesk internal relaydesk-phase4-20260820-02`，`draft=true`（以标签
  `relaydesk-phase4-20260820-02` 的 Release 页面或 run `32362194153` 定位，避免引用不稳定的
  untagged URL）。
- 包取证：`dist/actions/32362194153`、manifest、`SHA256SUMS`、本地 `Get-FileHash` 与
  Release API digest 一致。
- 证据回填后的资料校验为 49 个必需文件、12 个 JSON、60 个协议向量 `PASS`。
- 分支 run `32356352794` 同样完成 Windows 99/99、macOS 100/100 与同目标包摘要；
  `dist/actions/32356352794/evidence-windows-gui-runtime/result.json` 记录七语言完整循环、
  托盘最小化/关闭/恢复与真退出均 PASS。该证据仅限 Windows 单机 GUI，不替代物理 Win↔Mac。
- 2026-08-20 当前桌面再次使用精确 portable 完成 WIN-020：输入 Server 核心真实监听
  `0.0.0.0:24800` 并可停止；GUI 发现监听 `0.0.0.0:24802`，文件通道动态 TCP 同时监听
  IPv4/IPv6；手动地址、传输中心、传输设置及 HKCU 登录启动均经退出/重启验证并回滚。
- 物理 Win↔Mac、macOS TCC/menu bar 和 unsigned SmartScreen/Gatekeeper/签名交互：`NOT_RUN`。

### macOS 14 修复集成（待产品标签回归）

- A5 `6457d481` 已修复先前 macOS 14 实验的两项超时并集成至 `32712c6b2` + `ea70650ff`。
  run `32444914659` `SUCCESS`：materials `96662753134`、macOS `96662753324`（100/100，37.76 s）、
  Windows `96662753384`（99/99，62.32 s，TEST-005 PASS）和 macOS lifecycle `96665719559` 均成功。
- artifacts：macOS `9433863107` / `89c4341b04a93e15487de9021068892236a46debf368bdd2b6e36fdd7f13fe9c`；
  Windows `9434070187` / `cd80850ff5997d7785eab132b1efe42cb84445147518eb0b62fad28e19486622`；
  lifecycle `9434078894` / `cecbd4ceb7d2ec2c2d07302afc4045cb8781ec88f4884e57cc8cb2505312e9da`。
- 最后已验证标签仍为 `relaydesk-phase4-20260820-02`；本次集成待创建新标签并执行精确标签回归。TCC/menu bar
  和物理 Win↔Mac 继续为 `NOT_RUN`。

### 后标签回归（未创建新标签）

- 分支 tip：`442aa79f2f5e06299fc6368bd46785f4ce003203`，仅包含自动化中文提交修复；产品实现为
  `1b1a24739dea3775d64fa7987d30e9b37372a5c1`。最后已验证标签仍为
  `relaydesk-phase4-20260820-02`，不得伪造下一标签。
- run `32433749495` `SUCCESS`：materials `96630635916`、Windows `96630636007`（99/99，34.41 s）、
  macOS `96630635945`（100/100，37.91 s）、macOS lifecycle `96633281248`（19/19）均成功；
  release job `96633282373` 因分支运行 skipped。
- Windows artifact `9430307996`（36,254,057 bytes，`a60f9885a6da1e3aaee2a3a7a69b7ac374bfab8eba266b777daae49891392d52`）；
  macOS artifact `9430175846`（65,777,695 bytes，`72f513ec5f04aa3e71755026c59410d8a71fd808b7cd2b8ba03be471b72a06d7`）；
  macOS lifecycle artifact `9430317569`（12,562 bytes，`2d6a1ad19ff5e9b730d114ea10e29ebd13122f69ac2d647e58cab17c9d6948af`）。

## 最终用户验收

只有开发完成后才填写：

- [ ] Windows 安装
- [ ] macOS 安装/首次打开
- [ ] macOS 权限授权
- [ ] Win→Mac 键鼠
- [ ] Mac→Win 键鼠
- [ ] Win→Mac 文件
- [ ] Mac→Win 文件
- [ ] 大文件断点续传
