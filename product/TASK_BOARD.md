# 任务看板

A0 维护本表；用户不参与任务移动和 Git 操作。

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
| CTRL-002 | 支持接收方直接暂停、继续和取消传输 | A6/A0 | 已集成接收方直接 pause/continue/cancel；原生串行 CTest 98/98 PASS，真实双机控制链路 `NOT_RUN` |
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
