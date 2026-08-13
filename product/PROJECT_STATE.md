# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Phase 0-4 internal release candidate delivered
- Last updated: 2026-08-13
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus-relaydesk`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last product implementation commit: `4903df2d1c0ea8c37a28db2e0e9f743daa566e90`
- Last frozen protocol commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Current implementation: the v1 internal-release code path is composed: MainWindow owns transfer UI/service/history, incoming single/multi-file/folders, four conflict policies and interrupted resume use bounded workers and platform atomic commits, reconnect dials the selected authenticated TLS candidate, and Windows/macOS permission adapters feed the shared model.
- Last verified stage tag: `relaydesk-phase4-20260813-02` (`4903df2d1c0ea8c37a28db2e0e9f743daa566e90`)

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
| Phase 4 exact-tag release | PASS | tag `relaydesk-phase4-20260813-02`; run `31688962563`; Windows 88/88, macOS 89/89, Windows installer and macOS lifecycle PASS; unsigned draft Release published |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | PASS | A2/A3/A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; local Release asset SHA verification PASS |
| 2 文件传输 | PASS | A2/A6/A0 | tag `relaydesk-phase2-20260813-04`; run `31655013105`; Win 74/74, Mac 75/75; four assets triple-digest verified |
| 3 可靠性/UI | PASS | A3/A6/A7 | MainWindow/history/multi-file/folder/resume/conflict/reconnect composed; physical Win↔Mac remains final acceptance |
| 4 平台/发布 | PASS | A4/A5/A7 | tag `relaydesk-phase4-20260813-02`; run `31688962563`; exact-tag unsigned MSI/7Z/App ZIP/DMG downloaded and SHA-256 verified |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows（Phase 4 最终内部候选）

- Commit: `4903df2d1c0ea8c37a28db2e0e9f743daa566e90`
- Tag / workflow run: `relaydesk-phase4-20260813-02` / `31688962563`
- Artifact: `relaydesk-windows-x64-4903df2d1c0ea8c37a28db2e0e9f743daa566e90` (ID `9177022266`)
- Artifact ZIP SHA-256: `e3e6387cdf054aa1a1fb596e38bb7ce00dc971e1047c35cb29da5da073d6af54`
- MSI SHA-256: `35c7ebcc5538b553e866b1f8e38bda2d0951248defddaef557a03da732845d1c`
- Portable SHA-256: `c4bf6ba0ca094233dff4246be3b6cbce8fa8cae4908e87057cc3556c4f12bfd2`
- Build result: PASS (CTest 88/88; unsigned MSI + portable 7Z + source packages)
- Installer result: PASS (clean install, repair, real MSI major upgrade, two uninstalls, service,
  firewall, residue and user-data preservation)
- Physical Win↔Mac runtime result: NOT_RUN; final user acceptance required

### macOS（Phase 4 最终内部候选）

- Commit: `4903df2d1c0ea8c37a28db2e0e9f743daa566e90`
- Tag / workflow run: `relaydesk-phase4-20260813-02` / `31688962563`
- Artifact: `relaydesk-macos-arm64-4903df2d1c0ea8c37a28db2e0e9f743daa566e90` (ID `9176744262`)
- Artifact ZIP SHA-256: `bbba52bd0f2785848cc3971d5f3abcb073c7b09f67f4e56287b4621d108efdda`
- App ZIP SHA-256: `9ac817a661081b519a5009579bca502611f6d9c0da0758799a5a753c9ed77097`
- DMG SHA-256: `7d4af9b3a4935a49d791fc2837992e50703bed9879fe21c0ce10d1659bab1d27`
- Build result: PASS (CTest 89/89; ad-hoc App ZIP + DMG + source packages)
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
