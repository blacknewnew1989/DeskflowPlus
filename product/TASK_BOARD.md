# 任务看板

A0 维护本表；用户不参与任务移动和 Git 操作。

> 2026-08-30 起进入 P0 重新开发。下方旧 Phase 0-4 看板保留为历史线索，其中的 `PASS` 不继承
> 到本轮。当前状态以本节和 `product/docs/reports/REDEVELOPMENT_BASELINE.md` 为准。

## R0 重新开发看板

| ID | 状态 | Owner | 当前范围与完成条件 |
|---|---|---|---|
| R0-001 | PASS | A0/A1/A7 | 基线 `30593b53e` 已普通推送，源码分类、P0 `NOT_RUN` 矩阵和网络限制均已记录 |
| R0-002 | PASS | A2/A5/A0 | ASan 确认并修复两处测试回调 `stack-use-after-scope`；settings-only/ordered 各 50/50，ASan 101/101；clean run `33330456697` Win 100/100、Mac 101/101 |
| R0-003 | PASS | A6/A7/A0 | fresh build 的 `RelayDeskConflictResolverTests` 连续 50/50 PASS，旧 debug 卡住未复现且不定性源码 |
| R0-004 | PASS | A2/A5/A6/A7/A0 | E4 单向同机双进程：真实 discovery/pair/trust/TLS 1 MiB+ 文件；Win Debug/Release 10/10，run `33326619207` 双平台目标 PASS |
| R0-005 | NOT_RUN | A4/A5/A7/A0 | 重建同 SHA Windows/macOS 平台证据、精确阶段标签、artifact 和草稿 Release |
| R0-006 | FINAL_ACCEPTANCE_REQUIRED | A0/用户 | 最终包完成后执行物理 Win↔Mac、macOS TCC/menu bar 和 unsigned 系统交互 |
| R0-007 | PASS | A0/A5 | clean-run macOS ACK `0661191ae` 已普通推送；重开发 ref `b6a8852d0` 与未合入产品 ref `c544dc76f` 分列闭环 |

## R3 当前纵向切片

| ID | 状态 | Owner | 当前范围与完成条件 |
|---|---|---|---|
| R3-CTRL-001 | PASS | A6/A7/A0 | receiver 双进程 queued pause/resume/cancel；Windows 两配置独立 10/10，run `33333471632` Win 100/100、Mac 101/101；macOS 本机 Debug `BLOCKED` |
| R3-FILETREE-001 | PASS | A6/A7/A0 | 同机双进程一次 send 完成独立文件、嵌套文件夹、空目录精确树与摘要；run `33341572421` 双平台目标 PASS |
| R3-LISTENER-RESUME-001 | PASS | A6/A7/A0 | 同一 peer 对象 listener 中断后从非零 durable offset 恢复；完成态 sidecar 清理已修复，fresh Win/hosted Win+Mac PASS |
| R3-PROCESS-RECOVERY-001 | PASS | A3/A6/A7/A0 | receiver/sender 单文件 relaunch、receiver 文件树 relaunch、Store 目录 entry 与 shutdown 生命周期已集成；run `33385968319@043d6b3fb` hosted Windows 101/101、macOS 102/102，关键 FileTransferRuntime/TwoProcess 目标和 macOS lifecycle PASS。范围仅为正常退出后的 localhost OS 子进程恢复；crash/强杀/断电为 R5 `NOT_RUN`，物理 Win↔Mac/TCC/人工安装/正式发布仍未成立 |

## R4 UI / 平台基线

| ID | 状态 | Owner | 当前范围与完成条件 |
|---|---|---|---|
| R4-UI-001 | PASS | A2/A3/A7/A0 | `R4-UI-001A` trust card 成功/revoke失败反馈、`R4-UI-001B` manual address 与 `R4-UI-001C` auto-accept失败反馈均已通过；范围仅限 Windows localhost/offscreen production UI |
| R4-UI-001A | PASS | A2/A3/A7/A0 | owner `fb4e75f92` / A0 内容等价提交 `709533024`；真实 auto-accept/revoke菜单+确认、tombstone/card/重复边界与 revoke primary失败非模态反馈通过，Store primary/backup契约统一。owner 双槽3/3、六完整目标及A0 fresh全绿，pairing/UI双review GO；详见第10节 |
| R4-UI-001B | PASS | A2/A3/A7/A0 | owner `d81c13e54` / A0 内容等价提交 `34f248170`；真实 manual dialog Add/Save/重开/Remove/Save、settings持久化与Discovery listener一次启动通过。owner 3/3、五完整目标与A0 fresh全绿，UI/discovery双review GO；详见第11节，实际probe/LAN仍未验证 |
| R4-UI-001C | PASS | A2/A3/A7/A0 | owner `55388091f` / A0 `38e955bd6`；精确 parent RED 证明 modal+diagnostic 且非模态不可见，修复后真实 widget 手势显示固定脱敏非模态反馈、状态不变，恢复成功清旧反馈。owner 相关目标全绿、双review GO，A0 fresh 构建与槽退出0；详见第12节 |
| R4-UI-002 | PASS | A2/A3/A7/A0 | owner `b6a37091f` / A0 内容等价提交 `a2cb8a2af`；本端真实 Pair/Confirm/Cancel widget 手势经 typed boundary 驱动双真实 UDP PairingTrustRuntime，双方独立 trust/指纹、card、重复与取消边界通过。owner 双槽各3/3、10个完整目标与A0 fresh全绿，pairing/UI双review GO；详见第9节 |
| R4-UI-003 | IN_PROGRESS | A3/A4/A5/A7/A0 | `R4-UI-003A` 权限卡/capability gating 与 `R4-UI-003B` current-host Windows probe 已 PASS；Windows 系统设置返回、macOS TCC/系统设置往返、原生窗口与物理设备仍 `NOT_RUN` |
| R4-UI-003A | PASS | A3/A4/A5/A7/A0 | owner `16f0dbe39` / A0 `bfe7a5d86`；真实 MainWindow/DevicesDock 手势验证 Windows Firewall 与 ListeningPort 分项禁用 Pair、固定文案及同窗口恢复；macOS source contract 保持 LocalNetwork 与输入权限分离。owner 相关目标全绿、UI/platform 双review GO，A0 fresh 构建与槽退出0；详见第13节 |
| R4-UI-003B | PASS | A4/A7/A0 | A0 fresh2 282/282；受控 loopback 端口的 production WindowsFirewallProbe 从 `NotListening` 到 `Listening`、current-host MainWindow probe snapshot 与 PermissionStatusModel/DevicesDock card 逐项一致，两个临时槽均 3/0/0；规则投影前后 SHA-256 相同且为899条。系统设置入口/返回仍 `NOT_RUN`；详见 `R4_WINDOWS_PERMISSION_RUNTIME.md` |
| R4-UI-004 | PASS | A3/A7/A0 | owner `b036e1f7b` / A0 `5aa0bfc4b`；typed NotRunning/PeerUnavailable 显示本地化反馈，失败保留设备/路径选择且不创建 transfer row；红测与两完整目标退出 0 |
| R4-UI-005 | PASS | A3/A6/A7/A0 | owner `5ac175f90` / A0 内容等价提交 `36d83e77c`；真实 TLS localhost offer 经 production widget accept/reject 完成 SHA/row/清理与 typed rejection 验收，双 reviewer GO，A0 fresh Composition 11/11。证据见 R4 基线报告第 6 节；Ask、原生窗口系统和物理双机仍 `NOT_RUN` |
| R4-UI-006 | PASS | A3/A6/A7/A0 | `R4-UI-006A` pause/resume/cancel 与 `R4-UI-006B` history retry 均已通过真实 localhost/offscreen widget/runtime 验收；原生与物理交互不由该 PASS 证明 |
| R4-UI-006A | PASS | A3/A6/A7/A0 | owner `361b3ba2e` / A0 内容等价提交 `9c38f79e9`；receiver action flags 修复后，真实按钮 Pause/Resume 与 More/Cancel 通过 TLS localhost 双端状态、稳定 bytes/part、SHA、Keep partial/恢复状态验收。owner 3/3、三完整目标与 A0 fresh Composition 全绿，双 reviewer GO；详见 R4 基线报告第 7 节 |
| R4-UI-006B | PASS | A3/A6/A7/A0 | owner `52fd8341a` / A0 内容等价提交 `248331980`；真实 Failed history row 的 Retry 按钮清 availability、产生 Applied/新 ID/第二 offer，二次 Accept 完成当前源 SHA；旧 history/sidecar不被改写。owner 3/3、四完整目标与 A0 fresh Composition 全绿，双 reviewer GO；详见第 8 节 |
| R4-UI-007 | PASS | A3/A7/A0 | `R4-UI-007A` 真实后台传输刷新/主操作与 `R4-UI-007B` MainWindow details→TransferCenterDock 路由均已通过 localhost/offscreen production UI 验收 |
| R4-UI-007A | PASS | A3/A7/A0 | owner `da3497e69` / A0 `6a0664575`；真实 TLS/runtime/composition snapshot 驱动 production MiniBar hidden→visible、非零进度与精确 model 文本/percent，真实主按钮 Pause稳定/Resume完成SHA；UI/transfer 双review GO，A0集成槽3/3。详见第15节 |
| R4-UI-007B | PASS | A3/A7/A0 | owner `1a61ab239` / A0 `678695f1c`；真实 MainWindow 自身 model/MiniBar/Dock 在 active row 前不可操作，真实 bar body 点击经 production route 打开并前置 dock，对应 TransferId row 可达。owner相关目标全绿、双review GO，A0槽3/3；详见第16节 |
| R4-UI-008 | PASS | A3/A7/A0 | owner `258d7aa6e` / A0 内容等价提交 `941149532`；打开文件/位置被拒绝及 history load/persist error 显示本地化非模态反馈，交错状态保留选择、按钮和历史记录。owner 四完整目标退出 0，A0 四目标构建退出 0；命令与日志见 R4 基线报告第 5 节，真实 OS shell 打开仍 `NOT_RUN` |
| R4-UI-009 | PASS | A3/A7/A0 | owner `8aa690359` / A0 `d015027e9`；真实 Save、三字段持久化/重开回显、pending offer 下真实 DevicesDock 设置按钮及同窗 runtime 热更新均通过。owner 两槽各3/3、完整MainWindow19/19、TransferSettings10/10，A7 GO；A0两槽各3/3。仅限Qt localhost/offscreen，详见 `R4_SETTINGS_RUNTIME.md` |
| R4-UI-010 | IN_PROGRESS | A3/A4/A5/A7/A0 | `R4-UI-010A` 已验证 Windows native close/minimize-to-tray 与 close-to-quit 生命周期；`R4-UI-010B` 已验证 hosted macOS 当前 SHA 的 offscreen Quit QAction、ad-hoc bundle 和隔离 install lifecycle。native tray 图标/菜单 Show/Hide/Quit、物理 macOS menu bar、物理交互和发布仍 `NOT_RUN` |
| R4-UI-010A | PASS | A4/A7/A0 | 当前 SHA `8ef6461ea` 的真实 `deskflow.exe`：WM_CLOSE closeToTray 隐藏存活、SW_MINIMIZE minimizeToTray 隐藏存活、closeToTray=false 自然退出。tray 图标/context menu 未可靠观测，不能外推其 Show/Hide/Quit；详见 `R4_WINDOWS_TRAY_LIFECYCLE.md` |
| R4-UI-010B | PASS | A5/A7/A0 | run `33464083567@38247729b` 的 macos-14 package #`99720205727`：CTest 102/102，MainWindowQuitRegression menu #14 0.23 s、tray #15 0.21 s 均 PASS；codesign 与七语言 translation bundle PASS。install lifecycle #`99723079671` 的 TEST-005 PASS，artifact 下载摘要匹配。范围仅 hosted/offscreen/隔离 lifecycle；物理 menu bar、TCC、Dock/Finder、人工安装、Developer ID/notarization、物理 Win↔Mac 和发布均 `NOT_RUN`；详见 `R4_MACOS_MENU_CONTRACT.md` |

## 重开发前历史看板

## Ready

既有 Phase 0-4 真机双机验收仍列在 `product/docs/reports/RELAYDESK_V1_INTERNAL_RC.md`。
2026-08-20 当前候选已补齐下列用户入口；组件测试已通过，真实局域网和双机链路仍待最终验收：

| ID | 任务 | Owner | 当前范围 |
|---|---|---|---|
| DISC-005 | 为 RelayDesk 手动地址提供录入与管理入口 | A2/A3/A0 | 已复用 `DiscoverySettings.manualAddresses`、迁移和候选解析；录入、保存和定向探测已集成，原生串行 CTest 98/98 PASS；真实局域网链路 `NOT_RUN` |
| CONFLICT-003 | 为冲突策略 `Ask` 提供真实逐文件用户决策 | A6/A3/A0 | 已实现逐文件决策与运行时链路；Incoming Offer 不再静默降级为 AutoRename，原生串行 CTest 98/98 PASS；真实双机传输决策 `NOT_RUN` |

## In Progress

| ID | 任务 | Owner | 当前范围与完成证据 |
|---|---|---|---|
| UI-010 | 按已确认设计图实现共享 Qt 紧凑单栏首页 | A3/A0 | 当前产品树默认 560×420、最小 520×380；主窗口/托盘定向回归、精确标签 Windows 99/99、macOS 100/100 与双平台打包/自动生命周期均 PASS |
| UI-012 | 在高级设置页恢复输入角色与 Client 远端主机配置 | A3/A0 | Server/Client 保存后同步 CoreProcess，TLS 控件按当前 radio 即时更新，远端主机行显示与尺寸恢复回归已覆盖；offscreen 各连续 10 次与 native 各 1 次 PASS，实机平台交互待验证 |
| BRAND-002 | 实现原创“双设备 + 中继点”临时 Logo | A3/A4/A5 | `5c2092203`；SVG 单源、ICO/ICNS/DMG 同步及 macOS/Windows 品牌校验 PASS；精确标签平台包已生成并四方摘要一致 |
| TRAY-001 | 完成最小化/关闭到托盘与后台生命周期 | A3/A4/A5 | `56568584f`；独立设置与同步安全停机已实现，Windows 单机 GUI 自动化、精确标签双平台构建和自动生命周期 PASS；macOS menu bar 与物理双机仍 `NOT_RUN` |
| MAC-037 | 适配紧凑首页、权限能力门控和 macOS menu bar | A5/A3/A0 | `9ac7f0d79`；同一 PermissionSnapshot、三项能力门控、激活复检、template 图标与定向回归 PASS；待最终 App 系统权限前台往返实测 |
| MAC-039 | macOS 14 runner 链接警告与超时回归 | A5/A0 | `6457d481` / run `32444914659` SUCCESS；链接警告归零、macOS 100/100、Windows 99/99（TEST-005 PASS）及 macOS lifecycle 19/19 PASS；修复已集成，待产品标签回归；TCC/menu bar 与物理验收仍 `NOT_RUN` |
| CTRL-002 | 支持接收方直接暂停、继续和取消传输 | A6/A7/A0 | receiver queued production intent 已在同机双进程执行；Windows Debug/Release 与 hosted Win/Mac PASS，物理双机仍 `FINAL_ACCEPTANCE_REQUIRED` |
| WIN-019 | 构建并运行验收最新 Windows unsigned 包 | A4/A7/A0 | 标签 `relaydesk-phase4-20260820-02` 的 hosted Windows 包、CTest 99/99、TEST-005 19/19 自动生命周期均 PASS；WIN-020 已补精确 portable 单机真实运行；unsigned SmartScreen/UAC 人工交互仍 `NOT_RUN` |

除 WIN-019 的 unsigned SmartScreen/UAC 人工交互、MAC-037 的 TCC/menu bar 和真实物理验收外，
上述实现、精确标签双平台 CI、平台产物及自动生命周期均为 `PASS`。详见
`product/docs/reports/UI-010_COMPACT_UI_TRAY.md`。

## Blocked

只记录真实环境阻塞。不得因为缺签名凭据、缺本地某个平台或等待人工审批而阻塞共享核心和 CI 构建。

## Done

| ID | 任务 | Owner | 证据 |
|---|---|---|---|
| WIN-020 | Windows 精确 portable 单机真实运行 | A0 | 七语言、托盘、手动地址、输入核心 24800 启停、UDP 24802、动态文件监听、传输中心、传输设置和 HKCU 登录启动退出/重启持久化 PASS；配置与注册表已回滚；双机/macOS 保持 `NOT_RUN` |
| WIN-021 | Windows MSI 最低运行库要求 | A4/A0 | v14.51 LaunchCondition 导致 v14.44.35211 主机两次 1603；云端 MSI 反编译确认修复为 v14.44；本机修复包安装/修复/卸载 exit 0、GUI、服务、两条防火墙规则、开始菜单和无残留 PASS |
| PROTO-FREEZE-001 | RelayDesk v1 wire protocol / shared interface freeze | A6/A2/A0 | tag `relaydesk-protocol-v1-20260813-01`; run `31672497950` SUCCESS; Win 84/84, Mac 85/85; artifacts `9170492840` / `9170386546` |
| WIN-018 | Windows file safety adapter | A4/A0 | `bc0b9ffc9`; real NTFS junction/atomic commit tests; run `31678206041` Windows 87/87 PASS |
| MAC-013..018 | macOS file safety adapter and race hardening | A5/A0 | `b5e91d54e` through `e6f5fe519`; run `31678206041` macOS 88/88 PASS |
| COMP-004 | IFileTransferService / FileTransferRuntime incoming composition | A6/A0 | `cf8982ef8`, `e1a0ecdf6`, `8f5a992f8`; real pinned TLS 1 MiB+73B receive/atomic commit PASS; run `31682728899` Win 87/87, Mac 88/88 |
| COMP-005 | multi-file/folder and interrupted resume composition | A6/A0 | `e742ba4a4`, `7d9bfcbf6`, `5941ebd85`; real two-file/empty-dir and 20 MiB disconnect/reconnect/resume loopbacks PASS |
| COMP-006 | MainWindow transfer service/UI/history composition | A3/A0 | `f04293dad`, `14e6f2453`, `479a0f78f`; typed lifecycle and asynchronous history/free-space bridge tests PASS |
| COMP-007 | conflict four-policy production composition | A6/A0 | `153d38df6`, `2717f77d6`; 四策略底层/TLS 组合 7/7 PASS；`Ask` 的真实用户决策入口另由 CONFLICT-003 跟踪 |
| COMP-008 | authenticated reconnect and Windows permission product wiring | A3/A2/A4/A0 | `b251933dd` through `f79cc64dd`; selected-candidate TLS, async failure completion and firewall probe tests PASS |
| MAC-036 | 配对设备自动同步到 Deskflow 键鼠屏幕布局 | A5/A0 | `05f92a1ab`; run `31706167585` SUCCESS; Windows 89/89, macOS 90/90; add/persist/idempotency/rejection tests PASS |
| PHASE3-CI | Reliability/UI stage tag and dual-platform verification | A0/A7 | tag `relaydesk-phase3-20260813-01`; run `31691378517` SUCCESS; Win 88/88, Mac 89/89; macOS lifecycle and draft publication PASS |
| REL-001/002 | Phase 4 release candidate, installation instructions and acceptance checklist | A0/A7 | `relaydesk-phase4-20260813-03` / run `31706167585` 为历史候选；当前 `relaydesk-phase4-20260820-02` / run `32362194153` SUCCESS，Win 99/99、Mac 100/100、双平台包、草稿 Release 及自动生命周期 PASS；真实系统交互和物理验收仍 NOT_RUN |
| AUTO-001 | 识别当前 GitHub 仓库、origin、登录状态 | A0/A1 | GitHub admin/push 权限确认 |
| AUTO-002 | 添加 upstream、fetch v1.26.0、验证 760e3b9 | A0/A1 | `760e3b99` |
| AUTO-003 | 创建/恢复产品分支并安装资料/workflow | A0/A1 | bootstrap worktree and commit |
| AUTO-004 | bootstrap commit + push origin | A0 | `9b0a4111` pushed |
| BASE-004 | 核查真实模块/CMake/测试/打包结构 | A1 | `5b01f073` baseline audit |
| BASE-002 | Windows Release Actions 构建/测试/打包 | A0/A4/A7 | run `31602699800`, CTest 27/27, MSI/7Z |
| BASE-003 | macOS arm64 Actions 构建/测试/打包 | A0/A5/A7 | run `31602699800`, CTest 28/28, App/DMG |
| AUTO-006 | 触发并监控首次双平台 workflow | A0/A7 | phase tag run `31602699800` PASS |
| BRAND-001 | 集中品牌与安装包身份配置 | A1/A3 | `7c1df18ad`, `e9cb1121a` |
| I18N-001 | 中文翻译基线与语义 key | A3 | `a36ad2a91`, `f69555c6c`, `c097c2157` |
| DEV-001 | 稳定 DeviceIdentity/deviceId | A2 | `2168f3941` |
| DISC-001 | 严格 UDP discovery codec | A2 | `f82ef0eac`; local Qt Test PASS |
| CORE-001 | 严格 CBOR 控制消息 | A6 | `ef53feb5d`; Qt Test PASS |
| FILE-001 | RDFT FrameCodec | A6 | `bf367feaf`; Qt Test PASS |
| FILE-003 | 共享 PathPolicy | A6 | `85efded28`; Qt Test PASS |
| FILE-008 | 流式单文件 manifest | A6 | `1bd40469e`; Win/mac timestamp fixes integrated |
| FILE-015 | 有界多文件/文件夹 manifest | A6 | `a5de2b3dc`; local transfer CTest 4/4 PASS |
| DEV-002 | 复用 Deskflow TLS identity | A2 | `acc06b567`; Qt Test PASS |
| DISC-002 | 多网卡 UDP discovery service | A2 | `68c423a63`; real loopback and Qt Test PASS |
| DISC-003 | DeviceSnapshot registry/TTL | A2 | `907df4412`, `7894979a0`; Qt Test PASS |
| DISC-004 | 手动地址、迁移与候选解析 | A2 | `4306100f4`, `55fc7d830`, `a0ea42ba3`; Qt Test PASS |
| PAIR-001 | 有界 SAS pairing state machine | A2 | `2b851fe28`; Qt Test PASS |
| PAIR-002 | 六位码与 peer identity exchange | A2 | `9b67b2f13`; Qt Test PASS |
| PAIR-003 | 原子 trust store 与真实 UDP manager/service | A2 | `bc7c12f2b`, `0955ec823`, `21b60f497`, `a8e77f61f`, `c1c35bf14`; Qt Test PASS |
| PAIR-004 | TLS fingerprint pinning | A2 | `46682deb7`; Qt Test PASS |
| PAIR-005 | 可信设备自动重连 | A2 | `9da6930db`; Qt Test PASS |
| PAIR-006 | 撤销信任并终止连接/重连 | A2/A3/A0 | `dc4b7efed`、`19e5ab583`、`3f6efb6d1`、`d4d312e88`、`088702900`；撤销确认、TLS 断开、主动连接/自动重连拒绝，MSVC/Qt 六目标 6/6 PASS |
| UI-001 | 设备首页模型 | A3 | `e7890507a`; Qt Test PASS |
| UI-002 | 配对向导模型 | A3 | `05152e338`; Qt Test PASS |
| UI-003 | Devices Dock 应用入口 | A3 | `36004dda0`; Qt Test PASS |
| UI-004 | 权限契约与可操作状态提示 | A3/A4/A5 | `ad2c941fc`, `acc20f843`; Qt Test PASS |
| FILE-002 | 严格 CBOR message registry/types | A6 | `ef53feb5d`; Qt Test PASS |
| FILE-006 | pinned TLS file listener/client | A2/A6 | `3a869489a`; loopback Qt Test PASS |
| FILE-007 | bounded capability negotiation | A6 | `8fe0e48b0`; Qt Test PASS |
| FILE-009 | offer/accept/reject control flow | A6 | `97996a0a7`, `1bec537d2`; Qt Test PASS |
| FILE-010 | streaming single-file sender | A6 | `7158d6014`; Qt Test PASS |
| FILE-011/012 | `.part` receiver, SHA-256 verify and atomic commit | A6 | `4c6922dfa`, `f67916798`; Qt Test PASS |
| FILE-016 | bounded manifest paging | A6 | `7f62f2dad`; Qt Test PASS |
| FILE-017/018 | backpressure and source mutation handling | A6 | `761ee7e3d`; Qt Test PASS |
| RESUME-001..004 | atomic state, durable checkpoints, negotiation and restart | A6/A7 | `3e5728b92` through `553537117`; local transfer CTest PASS |
| CTRL-001 | pause/resume/cancel state machine | A6 | `97e7b4aad`; Qt Test PASS |
| HIST-001 | bounded atomic transfer history | A6 | `3063ae589`; Qt Test PASS |
| CI-001 | 唯一非门禁 Windows/macOS build workflow | A0/A7 | tag run `31621226862`; Windows 60/60、macOS 61/61、打包上传 PASS |
| TEST-002/003 | 10 GiB logical bounded-memory 与输入优先级/I/O ownership probe | A6 | `bdbe3cd78`、`09eb2f2ad`; current-tree targets PASS |
| PHASE1-REL | Phase 1 双平台 CI、草稿 Release 与本地 SHA 复验 | A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; App/DMG/MSI/7Z 三重摘要一致 |
| UI-005..009 | 拖放发送、Incoming Offer、传输中心、进度/通知、历史动作 | A3/A6 | `9c4ba0f25` through `88551dfb8`; current-tree Qt tests PASS |
| CONFLICT-001/002 | 并发安全冲突策略 | A6 | `c86275888`, `915721e22`; current-tree Qt Test PASS |
| RESUME-005 | explicit partial cleanup policy | A6 | `cb86ecdfb`, `104c13bea`; current-tree Qt Test PASS |
| TEST-004 | 确定性中断/恢复组合矩阵 | A6 | `2989e86e1`; 9 Qt cases / current-tree target PASS |
| WIN-001..004 | Windows diagnostics/startup/product packages/optional signing | A4/A7 | `b1db63680` through `22d27754d`; native probes and packaging suites PASS |
| MAC-001..004 | macOS permission probes/product packages/optional notarization | A5/A6 | `588cbb395` through `74634f3c1`; cross-platform Actions + contract tests PASS |
| COMP-001 | runtime audit and discovery composition | A6 | `a06575dc1`, `ac0e9d74a`; real UDP loopback PASS |
| COMP-002 | pairing/trust runtime composition | A6/A0 | `8a7c025cb`, `c086fa967`; discovery UDP → pairing → atomic trust → GUI, local composition tests PASS |
| COMP-003 | transfer UI intent runtime adapter | A3/A0 | `c8505c366`; typed send/offer/control/history intents and safe completion opener, 6/6 PASS |
| PHASE2-CI | 文件传输内核阶段标签双平台验证 | A0 | tag `relaydesk-phase2-20260813-04`, run `31655013105`, Win 74/74 + Mac 75/75 + draft Release + local digest verification PASS |
| TEST-005 | Windows/macOS 安装、升级、卸载与数据保留回归 | A7/A0 | Windows run `31657498852` installer report PASS；macOS run `31657596578` overall/lifecycle PASS；证据见 `product/docs/reports/TEST-005_*_INSTALL_LIFECYCLE.md` |

## 规则

- 小功能完成：最小测试 + 独立 commit。
- 共享接口完成：push 代理分支供另一平台同步。
- 阶段完成：合入 `product/relaydesk-v1`、更新状态、push、stage tag、触发双平台构建。
- PR、review、required checks 不是 Done 条件。
- 不能真机运行的项使用 `NOT_RUN`，但继续其他任务。
