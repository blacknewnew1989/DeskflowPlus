# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Phase 1 product foundation / Phase 2 independent core slices
- Last updated: 2026-08-12
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus-relaydesk`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last pushed commit: `a5de2b3dc0c3c8bdf7ecd05f396b53365415e70d`
- Last stage tag: `relaydesk-phase0-20260812-01` (`808a3307b07422e7ea8c60af46148ce68af13649`)

## 自动执行状态

| 项目 | 状态 | 证据 |
|---|---|---|
| origin 可读写 | PASS | bootstrap push and subsequent integration push succeeded |
| upstream fetch | PASS | official refs fetched from `deskflow/deskflow` |
| v1.26.0=760e3b9 | PASS | `760e3b99b00053647a96b405276bf614bd860075` |
| bootstrap commit | PASS | `9b0a4111141abe0a619d5eaeea87b8690b771f70` |
| integration branch push | PASS | remote branch tracks local product branch |
| Windows build | PASS | phase tag run `31602699800`; CMake/Ninja/MSVC build, CPack MSI/7Z/source, CTest 27/27 |
| macOS build | PASS | phase tag run `31602699800`; arm64 build, DMG/App/source, CTest 28/28 |
| GitHub Actions artifacts | PASS | Windows artifact `9144025951`; macOS artifact `9143920156`; 30-day retention |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | IN_PROGRESS | A2/A3 | BRAND-001/I18N-001/DEV-001/DISC-001 integrated |
| 2 文件传输 | IN_PROGRESS | A2/A6 | CORE-001/FILE-001/003/008/015 integrated |
| 3 可靠性/UI | NOT_STARTED | A3/A6/A7 | 小功能 commit，阶段 push |
| 4 平台/发布 | NOT_STARTED | A4/A5/A7 | Windows/macOS packages + artifact |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows

- Commit: `808a3307b07422e7ea8c60af46148ce68af13649`
- Workflow run: `31602699800`
- Artifact: `relaydesk-windows-x64-808a3307b07422e7ea8c60af46148ce68af13649` (ID `9144025951`)
- SHA-256: `e97b274486a61909b89791bf85d576b534e532a3830929d1c0d5acfc672041dd` (GitHub artifact ZIP)
- Build result: PASS (MSI + unsigned portable 7Z + source packages)
- Runtime result: NOT_RUN

### macOS

- Commit: `808a3307b07422e7ea8c60af46148ce68af13649`
- Workflow run: `31602699800`
- Artifact: `relaydesk-macos-arm64-808a3307b07422e7ea8c60af46148ce68af13649` (ID `9143920156`)
- SHA-256: `cc73b5d9226dc973348be64a9fa0470a7d88be00af84996b4b272aa87121bda5` (GitHub artifact ZIP)
- Build result: PASS (unsigned/ad-hoc App ZIP + DMG + source packages)
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
