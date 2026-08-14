# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Phase 4 后续紧凑界面与后台运行改版（`IN_PROGRESS`；既有内部候选仍是上次验证基线）
- Last updated: 2026-08-14
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus-relaydesk`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last product implementation commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`
- Last frozen protocol commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Current implementation: the v1 internal-release code path is composed: MainWindow owns transfer UI/service/history, incoming single/multi-file/folders, four conflict policies and interrupted resume use bounded workers and platform atomic commits, reconnect dials the selected authenticated TLS candidate, Windows/macOS permission adapters feed the shared model, and trusted input peers are synchronized idempotently into the Deskflow screen layout after pairing or rediscovery.
- Last verified stage tag: `relaydesk-phase4-20260813-03` (`05f92a1ab721f7fd8b893e47e05643d5988e1719`)

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
| UI-010 | IN_PROGRESS | A3/A0 | PRD 与 UI/UX 规格已更新；待共享 Qt 紧凑首页实现和回归 |
| BRAND-002 | IN_PROGRESS | A3/A4/A5 | 设计方向已确认；待 SVG 单源、平台派生资源和打包接线 |
| TRAY-001 | IN_PROGRESS | A3/A4/A5 | 行为契约已定义；待最小化/关闭到托盘、菜单动作和真正退出验证 |
| MAC-037 | IN_PROGRESS | A5/A3/A0 | macOS owner 规格已同步；待权限能力门控、menu bar 适配和 lifecycle 回归 |

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
