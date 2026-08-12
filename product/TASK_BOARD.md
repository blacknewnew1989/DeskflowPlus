# TASK BOARD

A0 维护本表；用户不参与任务移动和 Git 操作。

## Ready

| ID | 任务 | Owner | 依赖 |
|---|---|---|---|
| BASE-002 | Windows 原版构建或 Actions 构建记录 | A4 | AUTO-004 |
| BASE-003 | macOS 原版构建或 Actions 构建记录 | A5 | AUTO-004 |
| UI-001 | 设备卡片与传输中心骨架 | A3 | AUTO-004 |

## In Progress

| ID | 任务 | Owner | 当前证据 |
|---|---|---|---|
| BASE-002 | Windows 原版构建或 Actions 构建记录 | A0/A4/A7 | 本机准备已回退；run `31594287736` queued |
| BASE-003 | macOS 原版构建或 Actions 构建记录 | A5/A7 | run `31594287736` queued |
| CORE-001 | 文件传输共享接口与协议骨架 | A6 | `agent/a6/file-protocol` 开发中 |
| CI-001 | 非门禁 Windows/macOS build workflow | A7 | 唯一 workflow active，失败修复/监控中 |

## Blocked

只记录真实环境阻塞。不得因为缺签名凭据、缺本地某个平台或等待人工审批而阻塞共享核心和 CI 构建。

## Done

| ID | 任务 | Owner | 证据 |
|---|---|---|---|
| AUTO-001 | 识别当前 GitHub 仓库、origin、登录状态 | A0/A1 | GitHub admin/push 权限确认 |
| AUTO-002 | 添加 upstream、fetch v1.26.0、验证 760e3b9 | A0/A1 | `760e3b99` |
| AUTO-003 | 创建/恢复产品分支并安装资料/workflow | A0/A1 | bootstrap worktree and commit |
| AUTO-004 | bootstrap commit + push origin | A0 | `9b0a4111` pushed |
| BASE-004 | 核查真实模块/CMake/测试/打包结构 | A1 | `5b01f073` baseline audit |

## 规则

- 小功能完成：最小测试 + 独立 commit。
- 共享接口完成：push 代理分支供另一平台同步。
- 阶段完成：合入 `product/relaydesk-v1`、更新状态、push、stage tag、触发双平台构建。
- PR、review、required checks 不是 Done 条件。
- 不能真机运行的项使用 `NOT_RUN`，但继续其他任务。
