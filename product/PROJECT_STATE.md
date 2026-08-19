# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Phase 4 最终收口（撤销信任链与本地六目标回归 `PASS`；接收方传输控制、手动地址入口、`Ask` 用户决策及精确标签双平台构建待完成）
- Last updated: 2026-08-20
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus-relaydesk`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last pushed integration tip: `939bbb3a02d9d7062a6a5cff4363cea95f1abd11`
- Current local implementation tip: `088702900731a167c0efb1a3c7114f99be75b2cc`（尚未推送）
- Last frozen protocol commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Current implementation: v1 内部发布主链已组合。共享 Qt 外壳包含紧凑首页、权限分项、设备/传输区域、集中品牌资源、托盘后台生命周期、输入角色设置和可信设备撤销。撤销后既有文件 TLS 连接关闭，主动连接与自动重连均拒绝该设备。接收方直接暂停/继续/取消的发送端同步仍有取消收尾红测；RelayDesk 手动地址录入和冲突策略 `Ask` 的逐文件用户决策尚无 GUI 入口。macOS TCC/menu bar 与 Win↔Mac 物理双机验收仍为 `NOT_RUN`。
- Last verified stage tag: `relaydesk-phase4-20260813-03` (`05f92a1ab721f7fd8b893e47e05643d5988e1719`)

## 2026-08-20 收口复验

| ID | 状态 | Owner | 当前证据 / 下一步 |
|---|---|---|---|
| PAIR-006 | PASS | A2/A3/A0 | `dc4b7efed`、`19e5ab583`、`3f6efb6d1`、`d4d312e88`、`088702900`；撤销确认、TLS 断开、重连拒绝与 520×380 更多菜单已接通；MSVC/Qt 6.10.1 六目标 CTest 6/6 PASS |
| CTRL-002 | FAIL | A6/A0 | 接收方直接 pause/resume 已越过，直接 cancel 后双端未在 15 秒内同时进入 `Cancelled`；修复前不得集成 A6 控制帧提交 |
| DISC-005 | NOT_STARTED | A2/A3/A0 | `DiscoverySettings.manualAddresses` 与候选解析已存在，但 RelayDesk GUI 尚无录入/管理入口 |
| CONFLICT-003 | NOT_STARTED | A6/A3/A0 | 底层策略组合已存在，但 Incoming Offer 仍固定 AutoRename，`Ask` 尚无逐文件决策入口 |
| WIN-019 | IN_PROGRESS | A4/A7/A0 | 最新代码的 Windows unsigned 安装包、安装后桌面/托盘/参数/生命周期验收待上述红项收口后执行 |
| MAC-038 | NOT_RUN | A5/A7/A0 | 最新 App 的 TCC/menu bar 与 Win↔Mac 物理双机仍需真实 macOS 对端；不以本地或 Actions 测试替代 |

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
| UI-010 | IN_PROGRESS | A3/A0 | 当前树已补齐七语言 182/182；Qt/UI 与脚本回归 PASS，待精确标签 Windows/macOS 构建 |
| UI-012 | IN_PROGRESS | A3/A0 | 高级页恢复输入角色与 Client 远端主机配置；TLS 控件按当前角色即时更新，远端主机行往返尺寸可恢复；MSVC + Qt 6.10.1 `MainWindowLayoutTests` 8/8 PASS，待安装包平台运行验证 |
| BRAND-002 | IN_PROGRESS | A3/A4/A5 | SVG 单源、主题资源、ICO/ICNS/DMG 与 CMake 接线已完成；macOS/Windows 品牌校验 PASS，待平台包核验 |
| TRAY-001 | IN_PROGRESS | A3/A4/A5 | 最小化/关闭到 tray 独立设置及安全停机已实现；菜单/托盘独立进程真实退出回归 2/2 PASS，待平台包与真机交互 |
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
| Phase 4 exact-tag release | PASS | tag `relaydesk-phase4-20260813-03`; run `31706167585`; Windows 89/89, macOS 90/90, Windows installer and macOS lifecycle PASS; unsigned draft Release published |
| UI-011 local closeout | PASS | product branch reached `939bbb3a0`; 7 Qt regressions, 29 Python contracts, Windows staged-QM loader and brand checks PASS |
| Current seven-language catalogs | PASS | `088702900`; en/es/it/ja/ko/ru/zh_CN are each 182/182; Qt catalog load and 14 translation contracts PASS |
| Current revoke-trust composition | PASS | `dc4b7efed` through `088702900`; clean MSVC/Qt 6.10.1 targeted CTest 6/6 PASS |
| Current exact-SHA dual-platform Actions | NOT_RUN | run only after CTRL-002, DISC-005 and CONFLICT-003 are closed on one final product SHA |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | PASS | A2/A3/A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; local Release asset SHA verification PASS |
| 2 文件传输 | PASS | A2/A6/A0 | tag `relaydesk-phase2-20260813-04`; run `31655013105`; Win 74/74, Mac 75/75; four assets triple-digest verified |
| 3 可靠性/UI | PASS | A3/A6/A7 | tag `relaydesk-phase3-20260813-01`; run `31691378517` SUCCESS; Win 88/88, Mac 89/89; physical Win↔Mac remains final acceptance |
| 4 平台/发布 | PASS | A4/A5/A7 | tag `relaydesk-phase4-20260813-03`; run `31706167585`; layout fix + exact-tag unsigned MSI/7Z/App ZIP/DMG downloaded and SHA-256 verified |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows（Phase 4 最终内部候选）

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

### macOS（Phase 4 最终内部候选）

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
