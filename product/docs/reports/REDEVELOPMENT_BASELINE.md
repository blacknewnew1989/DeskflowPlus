# RelayDesk P0 重新开发 R0 基线

## 1. 目的与结论

本报告建立 RelayDesk P0 重新开发的新证据起点。2026-08-30 之前的代码、测试、Actions、
安装包和阶段报告只作为需求线索或候选复用材料，不自动继承为本轮 `PASS`。

R0 当前结论：

- Deskflow v1.26.0 的成熟输入捕获、注入、Server/Client 协议和屏幕切换实现未被重写；
- RelayDesk 新增模块大多已接入生产组合根，可列为 `REUSE_AFTER_AUDIT` 候选；
- 本轮尚未重新建立定向测试、两应用进程、平台运行或精确标签发布证据；
- 全部 P0 功能状态初始化为 `NOT_RUN`；
- 自动化历史失败和当前弱复现证据均保持 `UNVERIFIED`，不得推断根因；
- 物理 Windows 与 macOS 双机项目保持 `FINAL_ACCEPTANCE_REQUIRED`。

## 2. Git 与远端实时证据

| 项目 | 实时结果 | 证据方式 |
|---|---|---|
| 上游仓库 | `deskflow/deskflow` | GitHub API |
| 上游标签 | `v1.26.0` tag object `82fd4b78e4c8271a77420937b829f21d1cbe623d` | GitHub API |
| 上游目标提交 | `760e3b99b00053647a96b405276bf614bd860075` | 解引用上游 annotated tag |
| 产品远端分支 | `origin/product/relaydesk-v1` = `c544dc76fb4f29aefb6ef30c8acc4475b6778e07` | GitHub API |
| 重开发分支 | `origin/agent/a0/redevelop-p0` = `c544dc76fb4f29aefb6ef30c8acc4475b6778e07` | GitHub refs API 创建后复读 |
| 重开发前锚点 | `relaydesk-pre-redevelop-20260830-01` = `c544dc76fb4f29aefb6ef30c8acc4475b6778e07` | GitHub refs API 创建后复读 |
| 本地重开发 worktree | `F:\github\DeskflowPlus\working\relaydesk-redevelop-p0` | `git worktree list` |
| 本地重开发分支 | `agent/a0/redevelop-p0`，跟踪同 SHA 远端 ref | `git status --branch` |

本地 `v1.26.0` 与 API 解引用结果一致，且 `760e3b99` 是当前产品提交的祖先。实时远端结论以
GitHub API 为准，不使用本地 ref 冒充远端状态。

## 3. Git 网络限制

2026-08-30 的 `origin fetch` 成功；随后 `upstream --tags --prune` 的 Git Smart HTTP 连接
卡住。精确进程树存活但新临时 pack 连续数分钟保持 12 bytes，确认无对象进展后只终止本轮
fetch 的 `git fetch`、`remote-https`、`index-pack` 子进程。

随后使用以下进程级方式复核普通 Git 传输：

```powershell
git -c http.version=HTTP/1.1 `
  -c http.curloptResolve=github.com:443:140.82.112.4 `
  ls-remote origin
```

该路径仍以 `Recv failure: Connection was reset` 失败。GitHub API 可用，因此只用 refs API
创建了指向当前已存在提交 `c544dc76` 的重开发分支和轻量锚点。不得把该回退扩展为手工提交
对象流程。后续新提交仍必须优先使用普通 `git push`；如果普通 push 失败，该提交的远端状态
保持 `UNVERIFIED`，不得只修改报告或远端 ref 冒充推送完成。

本次传输问题不证明上游或 `origin` 配置错误，也不构成代码测试失败。

## 4. 历史证据边界

### 当前产品 SHA 的托管证据

产品提交 `c544dc76` 的分支 run `33315290514` 为 `SUCCESS`：

- Windows job `99267461839`：CTest 99/99、打包和 TEST-005 生命周期通过；
- macOS job `99267461879`：CTest 100/100、App/DMG 打包通过；
- macOS lifecycle job `99269710600`：通过；
- artifacts：Windows `9733476262`、macOS `9733329920`、macOS lifecycle `9733481545`；
- publish job `99269711209`：分支运行按规则 `skipped`。

该 run 证明当前 SHA 在 hosted runner 上曾构建、测试和打包成功，只作为环境与候选测试入口。
它不是重新开发阶段的测试 PASS，也不是精确阶段标签或 Release 证据。

### 已知失败

精确标签 `relaydesk-phase4-20260821-01` 指向 `1772733883c77f53341d596bff63d786f8828349`。
run `32446566789` 的 macOS job 中，`RelayDeskAutoReconnectRuntimeTests` 以
`Subprocess aborted` 结束，结果为 99/100，exit code 8；macOS lifecycle 和 Release 被跳过。

日志没有断言、堆栈或崩溃位置，因此根因保持 `UNVERIFIED`。历史上对销毁顺序的修改不能作为
根因证明。R0 需要基于可注入 scheduler/connector 建立确定性生命周期复现，再决定是否改代码。

### 当前定向复现

既有 Windows debug 目录曾观察到 `RelayDeskConflictResolverTests` 无进展，但构建 SHA 未证明。
A0 随后从重开发 worktree 配置 `build/windows/r0-debug-fresh`，只构建并运行目标测试：

- 未改源码的 `RelayDeskAutoReconnectRuntimeTests`：连续 20/20 `PASS`，约 2.8 秒/轮；
- 提交 `72008201e`：增加受控 scheduler 销毁回调用例并移除两处 `qWait(1100)`；
- 改后 `RelayDeskAutoReconnectRuntimeTests`：连续 50/50 `PASS`，约 0.6 秒/轮；
- `RelayDeskConflictResolverTests`：连续 50/50 `PASS`，总计 5.40 秒。

因此旧目录卡住未在 fresh build 复现，不认定为当前源码缺陷。历史 macOS abort 的生产根因仍未
证明；`72008201e` 是测试确定性收口，不是生产修复。R0-002 仍等待 macOS 精确 SHA 复验。

## 5. 源码分类

| 分类 | 路径 | R0 结论 |
|---|---|---|
| `KEEP_UPSTREAM` | `src/lib/platform/**`、`src/lib/server/**`、`src/lib/client/**`、`src/lib/net/**` | 与 `760e3b99` 对比未改；保留 Deskflow 成熟输入和网络核心。 |
| `KEEP_UPSTREAM` | `src/apps/deskflow-core/deskflow-core.cpp` | 未改生产入口；CMake 只有集中品牌图标接线。 |
| `REUSE_AFTER_AUDIT` | `src/lib/gui/config/RelayDeskInputLayout.*`、`RelayDeskInputTarget.*` | 使用既有 `ServerConfig`/QSettings，不重写输入核心；需两进程和物理布局验证。 |
| `REUSE_AFTER_AUDIT` | `src/lib/relaydesk/device/**`、`discovery/**`、`pairing/**`、`trust/**`、`reconnect/**` | 生产组合根可追踪；UDP/TLS、撤销和重连仍需本轮证据。 |
| `REUSE_AFTER_AUDIT` | `src/lib/relaydesk/transfer/**`、`filetransport/**`、`app/FileTransferRuntime.*`、`IncomingTransferRuntime.*` | RDFT、独立 TLS、流式接收、续传和 UI service 已接线；需新鲜构建、真实 socket/FS 与双进程复验。 |
| `REUSE_AFTER_AUDIT` | `src/lib/relaydesk/platform/**` | Windows/macOS 文件安全和权限实现存在；平台真实运行未继承。 |
| `REUSE_AFTER_AUDIT` | `src/lib/relaydesk/model/**`、`widgets/**`、`i18n/**` | typed intent 可追到 service；长翻译、native UI 和物理交互需复验。 |
| `REUSE_AFTER_AUDIT` | `src/lib/gui/MainWindow.*`、`dialogs/**`、`core/CoreProcess.*` | 组合根接入发现、配对、文件、重连、托盘和权限；生命周期是 R0 高风险项。 |
| `REUSE_AFTER_AUDIT` | `.github/workflows/relaydesk-build.yml`、`product/scripts/**`、`deploy/**` | 可作为构建入口；旧 run 和产物不继承为新阶段 PASS。 |
| `REUSE_AFTER_AUDIT` | `src/unittests/relaydesk/**`、`product/tests/**` | 只复用测试骨架；必须确认覆盖生产 wiring 且在本轮重跑。 |
| `REMOVE_FROM_CURRENT_EVIDENCE` | 旧阶段报告、旧 artifact/log、`product/working/actions/**` | 保留 Git 历史，但不进入本轮“当前 PASS”。 |
| `REMOVE_FROM_PRODUCTION_SCOPE` | `product/starter/**` | 仅为示例，不是生产实现或验收证据。 |

静态审计未证明任何整个模块必须重写。发现确定性失败、生产 wiring 断裂、数据损坏风险或平台不兼容
后，才把对应最小切片升级为 `REWRITE`。不要重写 Deskflow 上游输入核心。

## 6. P0 初始验收矩阵

| 能力 | 当前状态 | 候选自动证据 | 本轮仍缺少 |
|---|---|---|---|
| 键鼠/滚轮 | `NOT_RUN` | Deskflow 上游实现和宿主测试 | 两应用进程、物理 Win↔Mac、负载下输入 |
| 文本/图片剪贴板 | `NOT_RUN` | 上游编解码和宿主测试 | 双进程双向、物理跨平台 |
| 发现/手动地址 | `NOT_RUN` | codec、TTL、UDP loopback、runtime | 本轮真实两应用进程与 LAN |
| 配对/信任/撤销 | `NOT_RUN` | pairing、store、pinning、UDP/TLS loopback | 两应用进程、指纹变化、物理跨平台 |
| 自动重连 | `NOT_RUN` | coordinator/runtime 测试骨架 | 确定性生命周期复现、断线/IP/睡眠运行 |
| 单/多文件和文件夹 | `NOT_RUN` | RDFT、TLS socket、真实临时文件系统 | 新鲜构建、两个应用进程和物理双向 |
| 暂停/继续/取消 | `NOT_RUN` | 双侧状态机和 UI runtime 测试骨架 | 传输中的真实控制 |
| 断点续传 | `NOT_RUN` | durable offset/recovery 测试骨架 | 进程重启、网络中断、物理摘要闭环 |
| 冲突四策略 | `NOT_RUN` | resolver、reservation、typed Ask intent | 新鲜单测、双进程逐文件 Ask 和竞态 |
| 路径安全/原子提交 | `NOT_RUN` | PathPolicy、Win/Mac adapter 测试骨架 | 本轮真实 NTFS/macOS 文件系统定向测试 |
| 进度/速度/ETA/历史 | `NOT_RUN` | model/runtime/history store 测试骨架 | 真实传输 UI 与完成位置操作 |
| UI/托盘/menu bar | `NOT_RUN` | widget/offscreen、生命周期测试骨架 | Windows native、macOS menu bar 和退出运行 |
| 七语言/品牌 | `NOT_RUN` | catalog、QM、品牌校验测试骨架 | 本轮加载、切换、重启和包资源闭包 |
| macOS 权限 | `NOT_RUN` | 原生 probe 和 hosted build | TCC 授权/撤销、前台复检、真实界面 |
| Windows 包 | `NOT_RUN` | hosted MSI/portable/TEST-005 候选入口 | 重开发精确标签、当前 artifact 和本机交互 |
| macOS 包 | `NOT_RUN` | hosted App/DMG/lifecycle 候选入口 | 重开发精确标签、Release、Gatekeeper/TCC |
| 10 GiB/10k/输入优先 | `NOT_RUN` | benchmark 与 bounded-I/O 测试骨架 | 当前测量报告和真实负载 |

物理 Win↔Mac、macOS TCC/menu bar、SmartScreen/UAC/Gatekeeper 和正式签名在自动开发完成后进入
`FINAL_ACCEPTANCE_REQUIRED`，但不得阻塞其他 R0-R5 工作。

## 7. R0 问题与下一步

| ID | 级别 | 状态 | 内容 |
|---|---|---|---|
| R0-001 | P0 | `PASS` | 基线 `30593b53e` 已普通推送，新状态和任务板已清除旧 PASS 的当前效力。 |
| R0-002 | P1 | `IN_PROGRESS` | Windows 确定性测试 50/50 PASS；等待 macOS 对 `72008201e` 的最小目标 ACK。 |
| R0-003 | P1 | `PASS` | fresh build 的 conflict resolver 连续 50/50 PASS，旧目录卡住未复现。 |
| R0-004 | P1 | `NOT_RUN` | 建立两个真实应用进程的发现、配对、文件和 UI 控制纵向用例。 |
| R0-005 | P1 | `NOT_RUN` | 重新建立 Windows/macOS 同 SHA 的阶段标签和 Release 证据。 |
| CI-001 | P1 | `IN_PROGRESS` | `materials-diagnostic` job 可 soft-fail 且不阻塞包；需决定是否让资料错误使 run 失败。 |
| DOC-001 | P2 | `IN_PROGRESS` | `IPlatformFileSafety.h` 的 `FileReceiver NOT_WIRED` 注释与现有 runtime wiring 不一致。 |
| NET-001 | P2 | `PASS` | 普通 Git 已恢复并推送产品与 coordination 新提交；保留间歇连接风险但不使用手工提交对象流程。 |

## 8. 跨平台即时沟通

涉及共享接口、协议、构建、权限、打包或运行行为的问题，不等待阶段结束。A0 与平台 owner 必须在
`coord/platform-sync` 追加独立 Markdown，引用准确产品 SHA、平台代理 branch/commit、测试结果、
问题和单一请求动作；接收平台在自己的目录追加 ACK 或带证据的 `BLOCKED`。

R0 已发送：

- A0 消息：`product/working/platform-sync/a0/20260830-144554Z-R0-001-macos-redevelopment-ack.md`；
- coordination commit：`4a811d11b`，已普通 push 到 `origin/coord/platform-sync`；
- 目标：A5-macOS；
- 主题：自动重连历史 macOS 失败、R0 证据归零、确定性生命周期测试与后续精确 SHA 复验；
- 基线 ACK：`product/working/platform-sync/macos/20260830-144918Z-R0-001-macos-redevelopment-ack.md`，
  coordination commit `862688b63`；
- 测试复验请求：`product/working/platform-sync/a0/20260830-151256Z-R0-002-macos-reconnect-test.md`，
  coordination commit `d854d55cd`；
- 精确测试 ACK 状态：`NOT_RUN`，异步等待 A5 结果，不阻塞 A0 继续其他 R0 工作。

聊天、本地状态报告或未推送文件不能替代 GitHub 留言。A0 持续跟踪到 ACK 或明确阻塞。

下一最小切片按顺序为：

1. 提交并普通推送 R0 基线；
2. 建立确定性 `AutoReconnectRuntime` 生命周期失败测试；
3. 使用可证明 SHA 的新鲜 build 单独复现 conflict resolver；
4. 只在根因证实后修改最小生产切片；
5. 逐项把源码分类从候选升级为本轮可复用证据。
