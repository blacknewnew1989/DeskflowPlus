# TASK BOARD

A0 维护本表；用户不参与任务移动和 Git 操作。

## Ready

No development or release-engineering tasks remain for Phase 0-4. Physical two-machine acceptance
is listed in `product/docs/reports/RELAYDESK_V1_INTERNAL_RC.md` and is intentionally user-owned.

## In Progress

No active implementation tasks.

## Blocked

只记录真实环境阻塞。不得因为缺签名凭据、缺本地某个平台或等待人工审批而阻塞共享核心和 CI 构建。

## Done

| ID | 任务 | Owner | 证据 |
|---|---|---|---|
| PROTO-FREEZE-001 | RelayDesk v1 wire protocol / shared interface freeze | A6/A2/A0 | tag `relaydesk-protocol-v1-20260813-01`; run `31672497950` SUCCESS; Win 84/84, Mac 85/85; artifacts `9170492840` / `9170386546` |
| WIN-018 | Windows file safety adapter | A4/A0 | `bc0b9ffc9`; real NTFS junction/atomic commit tests; run `31678206041` Windows 87/87 PASS |
| MAC-013..018 | macOS file safety adapter and race hardening | A5/A0 | `b5e91d54e` through `e6f5fe519`; run `31678206041` macOS 88/88 PASS |
| COMP-004 | IFileTransferService / FileTransferRuntime incoming composition | A6/A0 | `cf8982ef8`, `e1a0ecdf6`, `8f5a992f8`; real pinned TLS 1 MiB+73B receive/atomic commit PASS; run `31682728899` Win 87/87, Mac 88/88 |
| COMP-005 | multi-file/folder and interrupted resume composition | A6/A0 | `e742ba4a4`, `7d9bfcbf6`, `5941ebd85`; real two-file/empty-dir and 20 MiB disconnect/reconnect/resume loopbacks PASS |
| COMP-006 | MainWindow transfer service/UI/history composition | A3/A0 | `f04293dad`, `14e6f2453`, `479a0f78f`; typed lifecycle and asynchronous history/free-space bridge tests PASS |
| COMP-007 | conflict four-policy production composition | A6/A0 | `153d38df6`, `2717f77d6`; AutoRename/Overwrite/Skip/Ask real TLS paths and 7/7 suites PASS |
| COMP-008 | authenticated reconnect and Windows permission product wiring | A3/A2/A4/A0 | `b251933dd` through `f79cc64dd`; selected-candidate TLS, async failure completion and firewall probe tests PASS |
| REL-001/002 | Phase 4 release candidate, installation instructions and acceptance checklist | A0/A7 | tag `relaydesk-phase4-20260813-02`; run `31688962563` SUCCESS; Win 88/88, Mac 89/89; installer/lifecycle PASS; four final packages locally SHA-256 verified |
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
