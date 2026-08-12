# TASK BOARD

A0 维护本表；用户不参与任务移动和 Git 操作。

## Ready

| ID | 任务 | Owner | 依赖 |
|---|---|---|---|
| UI-001 | 设备卡片与传输中心骨架 | A3 | AUTO-004 |
| DISC-002 | 多网卡 discovery service | A2 | DISC-001 |
| FILE-006 | file TLS listener/client | A2/A6 | PAIR-004, FILE-001 |

## In Progress

| ID | 任务 | Owner | 当前证据 |
|---|---|---|---|
| DISC-002 | 多网卡 discovery service | A2 | DISC-001 codec 已集成，service 待实现 |
| FILE-006 | file TLS listener/client | A2/A6 | 协议/路径/manifest 基础已集成 |
| CI-001 | 非门禁 Windows/macOS build workflow | A7 | Phase 0 PASS；继续监控当前集成 HEAD |

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

## 规则

- 小功能完成：最小测试 + 独立 commit。
- 共享接口完成：push 代理分支供另一平台同步。
- 阶段完成：合入 `product/relaydesk-v1`、更新状态、push、stage tag、触发双平台构建。
- PR、review、required checks 不是 Done 条件。
- 不能真机运行的项使用 `NOT_RUN`，但继续其他任务。
