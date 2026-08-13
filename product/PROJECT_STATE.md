# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: PROTO-FREEZE-001 protocol/interface freeze before further Phase 3-4 runtime composition
- Last updated: 2026-08-13
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus-relaydesk`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last product implementation commit: `bb4bdc4ac7e25a046a6a6415c507501ba765efdf`
- Current implementation: PROTO-FREEZE-001 code contracts are complete at candidate level: 24-message registry, 60 shared vectors, stable wire/pairing/transfer errors, strong IDs, typed UI/service/control boundaries, queued values, directional receive capability, canonical sender sink, and typed conflict commit disposition. Further service expansion remains paused until the freeze tag Actions evidence is recorded.
- Last verified stage tag: `relaydesk-phase2-20260813-04` (`d14a92335cc326f00c3bd12869585d48201d1bc0`)

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
| Protocol/interface freeze candidate | IN_PROGRESS | candidate contracts through `bb4bdc4ac`; final docs/tag and same-commit Windows/macOS Actions evidence pending |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | PASS | A2/A3/A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; local Release asset SHA verification PASS |
| 2 文件传输 | PASS | A2/A6/A0 | tag `relaydesk-phase2-20260813-04`; run `31655013105`; Win 74/74, Mac 75/75; four assets triple-digest verified |
| 3 可靠性/UI | IN_PROGRESS | A3/A6/A7 | resume/checkpoint/control/history integrated; conflict and transfer UI active |
| 4 平台/发布 | IN_PROGRESS | A4/A5/A7 | baseline unsigned packages exist; platform diagnostics/productized RC remain |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows（最新 Phase 2 内部候选）

- Commit: `d14a92335cc326f00c3bd12869585d48201d1bc0`
- Workflow run: `31655013105`
- Artifact: `relaydesk-windows-x64-d14a92335cc326f00c3bd12869585d48201d1bc0` (ID `9164266512`)
- Artifact ZIP SHA-256: `094412b225b9e9ca220a009e1c551a44ab2fe919dc20b05d1b3000d9e687f087`
- MSI SHA-256: `258b721996aed2fe0ae40cf97cd5deffe0f07c50d4586088da5d1d3ab7c8abc2`
- Portable SHA-256: `32199d39b2e78771666a746001d5415aeb9636c7e3e2f257631e499d17f770b9`
- Build result: PASS (CTest 74/74; unsigned MSI + portable 7Z + source packages)
- Runtime result: NOT_RUN

### macOS（最新 Phase 2 内部候选；最终 seal 另由 TEST-005 验证）

- Commit: `d14a92335cc326f00c3bd12869585d48201d1bc0`
- Workflow run: `31655013105`
- Artifact: `relaydesk-macos-arm64-d14a92335cc326f00c3bd12869585d48201d1bc0` (ID `9164146467`)
- Artifact ZIP SHA-256: `bee98016ac6169abd8f6addca7f03b2bf0fc36bcd517b9906c062a170770d622`
- App ZIP SHA-256: `cb0e460d9e7847c3e17f6aa0d5b85693ec0f66d3aebb3e4918ebde5ec7420730`
- DMG SHA-256: `82e00d0b9a4d1f6cbdc58cdd6f7f4c7581b94f60ec1a371be90151874055f7d4`
- Build result: PASS (CTest 75/75; ad-hoc App ZIP + DMG + source packages)
- Final bundle result: PASS in TEST-005 run `31657596578` for the later ad-hoc App ZIP/DMG at `4377afeed`; see `product/docs/reports/TEST-005_MACOS_INSTALL_LIFECYCLE.md`. The Phase 2 hashes above remain the Phase 2 stage assets and are not relabeled as the later TEST-005 packages.
- Runtime result: NOT_RUN

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
