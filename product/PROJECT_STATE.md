# PROJECT STATE

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Autonomous bootstrap
- Last updated: 未开始
- User action required during development: none

## Git 状态

- Repository root:
- Active source worktree:
- origin URL:
- upstream URL:
- Current branch:
- Last pushed commit:
- Last stage tag:

## 自动执行状态

| 项目 | 状态 | 证据 |
|---|---|---|
| origin 可读写 | NOT_RUN | |
| upstream fetch | NOT_RUN | |
| v1.26.0=760e3b9 | NOT_RUN | |
| bootstrap commit | NOT_RUN | |
| integration branch push | NOT_RUN | |
| Windows build | NOT_RUN | |
| macOS build | NOT_RUN | |
| GitHub Actions artifacts | NOT_RUN | |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | NOT_STARTED | A0/A1/A4/A5 | push integration + phase tag + build artifacts |
| 1 产品基础 | NOT_STARTED | A2/A3 | 小功能 commit，阶段 push |
| 2 文件传输 | NOT_STARTED | A2/A6 | 小功能 commit，阶段 push |
| 3 可靠性/UI | NOT_STARTED | A3/A6/A7 | 小功能 commit，阶段 push |
| 4 平台/发布 | NOT_STARTED | A4/A5/A7 | Windows/macOS packages + artifact |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows

- Commit:
- Workflow run:
- Artifact:
- SHA-256:
- Build result: NOT_RUN
- Runtime result: NOT_RUN

### macOS

- Commit:
- Workflow run:
- Artifact:
- SHA-256:
- Build result: NOT_RUN
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
