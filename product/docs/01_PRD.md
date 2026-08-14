# 01 产品需求文档（PRD）

## 0. 文档执行级别

本 PRD 同时定义产品需求和 Codex 全自动执行流程。A0 不得只输出方案、教程或命令清单，必须实际执行源码获取、仓库初始化、环境准备、开发、提交、推送、构建、打包与产物下载。

**默认前提：当前目录和会话已经连接用户的 GitHub 仓库，`origin` 可用。用户只负责最终安装与验收。**

### 0.1 已完成的工程前置：RelayDesk v1 协议与共享接口冻结

在 Windows 与 macOS 并行实现文件传输 service 之前，A0 必须完成
`PROTO-FREEZE-001`。该任务是防止两个平台产生不兼容实现的工程依赖，不是人工审批、
PR 门禁或 required check。其完成证据以 `PROJECT_STATE.md` 和阶段报告为准。

冻结范围只包含本 PRD 的 P0/v1 能力，不扩展 Phase 5，不引入账号、云端、RBAC、复杂
PKI、病毒扫描、DLP 或其他额外安全体系。

A2/A6 是共享网络协议和接口的 owner，A0 是唯一集成负责人。A4/A5 在冻结前可以继续
不依赖未定义协议的平台构建、权限、打包和安装验证，但不得各自在平台目录补充消息、
字段、codec、service 或同名数据模型。冻结后，两端必须从同一个
`product/relaydesk-v1` 提交开始实现 service。

#### 0.1.1 协议冻结范围

`src/lib/relaydesk/transfer/Protocol.h` 中每个 v1 `MessageType` 必须满足以下二选一：

1. 具有唯一、规范的值类型、CBOR schema、codec、validator、错误分类、测试和冻结向量；
2. 被明确标为 reserved，且 v1 运行时不得发送。

不得保留“已注册且被运行时使用，但没有 schema/codec”的消息。每种可发送消息必须定义：

- CBOR 整数 key、字段类型、必填/可选字段及默认值；
- UUID、SHA-256、字符串、集合和整数范围；
- 合法 `streamId`、flags、metadata 和 payload 组合；
- 版本、未知字段、未知消息和畸形 CBOR 的处理；
- 重复、乱序、终态后消息及幂等规则；
- 稳定错误码与可安全本地化的诊断分类；
- Windows/macOS 共用的正负十六进制 test vector。

现有 `Heartbeat`、`HeartbeatAck`、`TransferPause`、`TransferResume`、
`TransferCancel`、`TransferComplete`、`TransferResult` 和 `Goodbye` 不得只保留编号；
A0/A6 必须为其补齐上述契约，或在 v1 中明确 reserved 并从运行时路径排除。

以下既有消息也必须统一审计，不因已有 codec 而跳过：

- `Hello` / `AuthResult` / `Capabilities`；
- `TransferOffer` / `TransferAccept` / `TransferReject` / `Error`；
- `ManifestPage` / `ManifestComplete`；
- `FileBegin` / `FileChunk` / `FileCheckpoint` / `FileEnd` / `FileResult`；
- `ResumeQuery` / `ResumeResponse`。

固定 32-byte RDFT header、big-endian 编码、独立文件 TLS 连接、认证前不交付业务帧、
默认 1 MiB 文件块和最终 SHA-256 保持现有规范，不另造第二套协议。

#### 0.1.2 共享接口冻结范围

协议冻结同时必须审核并固定以下跨平台接口：

- `DeviceId`、`DeviceInfo`、`DeviceSnapshot`、`DeviceCapabilities`；
- discovery、pairing、trust、pinning 和 reconnect 的公共输入输出；
- `IncomingOffer`、`TransferSnapshot`、`TransferHistoryRecord`；
- `SendOptions`、`ReceiveOptions`；
- `IFileTransferService` 的 send/accept/reject/pause/resume/cancel/retry、活动任务和 signals；
- `FileTransferRuntime` 的生命周期、线程所有权、连接路由和 service 组合职责；
- sender/receiver、frame sink、backpressure、resume、conflict 和 history 的组合边界；
- `PermissionSnapshot` 及平台 adapter 输入输出。

`IFileTransferService` 是 GUI 和平台组合层使用的唯一文件传输业务接口。
`FileTransferRuntime` 后续必须实现或组合该接口，不得长期形成两套互不相干的业务 API。
UI 只消费不可变 snapshot 并发送 typed intent；socket 回调不得读取文件、扫描目录或计算
SHA-256，磁盘和哈希工作必须留在 worker 边界。

#### 0.1.3 冻结完成条件

A0 只有在以下条件全部满足后，才可宣布 v1 协议冻结并启动双平台 service 并行实现：

1. `Protocol.h` 的全部消息已被机器可验证地归类为 implemented 或 reserved；
2. 所有 implemented 消息具有 struct、codec、validator、负例和冻结向量；
3. `product/docs/05_FILE_TRANSFER_PROTOCOL.md`、
   `product/docs/18_SHARED_CONTRACTS.md` 与代码一致；
4. 新增 `product/docs/19_PROTOCOL_V1_FREEZE.md`，记录消息表、schema、状态机、错误和兼容规则；
5. 自动测试能发现 enum 已注册但缺 schema/codec 的回归；
6. 相同正负向量在 Windows x64 与 macOS arm64 全部通过；
7. A0 将小提交合入并推送 `product/relaydesk-v1`；
8. A0 创建并推送 `relaydesk-protocol-v1-<date>-01` 标签，监控唯一双平台 Actions；
9. 阶段报告记录 commit、tag、run、测试数、artifact 和 `NOT_RUN`。

冻结后如确需修改 v1 契约，必须由原 owner 创建独立兼容性提交、同步文档和向量，并由
A0 先合入；平台代理不得私自扩展 wire schema。

### 0.2 当前产品变更：紧凑界面、权限门控、临时品牌与托盘

2026-08-14 用户确认 `product/assets/design/relaydesk-compact-ui-approved-20260814.png`
作为本轮视觉与信息架构基线。它是实现输入，不表示对应代码、图标或平台适配已经完成。
本轮按 `UI-010`、`BRAND-002`、`TRAY-001`、`MAC-037` 跟踪，并优先把权限契约同步给
共享 Qt 与 macOS owner。

实现前必须先冻结以下行为契约：

- 权限按能力门控，而不是用单一总开关阻断全应用。macOS 的 Accessibility、Input
  Monitoring、Local Network 必须分别显示状态、用途和“打开系统设置”，应用回到前台后
  自动复检；缺少输入类权限时文件传输仍可使用，但缺少 Local Network 时相关发现/连接
  能力必须明确降级；
- 支持最小化到托盘，以及可设置的关闭窗口后继续后台运行。托盘至少提供“显示 RelayDesk”、
  “暂停/继续共享”和“退出”；首次关闭到托盘只提示一次，“退出”必须真正结束进程并清理
  输入状态、监听器、传输任务持久化点和托盘资源；

在以上行为契约下，视觉与品牌实现还必须满足：

- 主窗口默认 `560 × 420 logical px`，最小建议尺寸 `520 × 380 logical px`；在系统缩放、
  较长翻译或辅助字号下允许内容滚动或窗口扩展，不得裁切关键操作；
- 首页使用共享 Qt 单栏信息架构：`52 px` 顶栏、单行权限条、紧凑设备列表、`52 px`
  迷你传输条和本机摘要；Windows 与 macOS 只做系统能力适配，不分叉信息层级；
- 设备条目使用两行语义：首行是设备名和主操作，次行是连接、信任与可用能力；设计稿中
  “已连接”和“可信、点击连接”是两种状态示例，不是固定设备数量或硬编码文案；
- 临时 Logo 必须为原创的“双设备 + 中继点”图形，以一个 SVG 为几何单源；彩色变体用于
  App/安装包，单色变体用于 tray/menu bar，并在 `16 px` 下仍可识别；
- macOS 使用菜单栏单色 template 图标承载同一托盘语义，不得把彩色 App 图标直接缩小后
  当作菜单栏图标。

## 1. 产品定义

RelayDesk 是一款无需账号、无需云端、在本地局域网工作的跨设备协作工具。用户使用一套键盘鼠标在 Windows 与 macOS 设备间无缝切换，并把文件或文件夹可靠发送到任意已配对设备。

## 2. 交付责任

### 2.1 Codex 必须负责

- 自动定位并在需要时解压本开发包；
- 自动获取 Deskflow v1.26.0 源码；
- 自动将源码放入当前 GitHub 项目仓库；
- 自动配置 `origin/upstream`；
- 自动创建产品分支、代理分支和 worktree；
- 自动准备或安装 Windows/macOS 开发依赖；
- 自动开发、测试、提交、合并和推送；
- 自动配置双平台 GitHub Actions 回退；
- 自动生成 Windows 安装包与 macOS App/DMG；
- 自动安装并触发 `.github/workflows/relaydesk-build.yml`；
- 自动监控 Actions、读取失败日志、修复并重新运行；
- 自动下载构建 artifact 或生成草稿 Release；
- 自动维护状态、日志、SHA-256 和最终验收说明。

### 2.2 用户只负责

项目全部开发完成后：

- 在 Windows 安装生成的包；
- 在 macOS 安装/打开生成的 App 或 DMG；
- 按系统提示授予 Accessibility/Input Monitoring/Local Network；
- 按最终验收表验证键鼠、剪贴板和文件互传；
- 若需要正式签名，再提供 Apple/Windows 证书。

开发过程中不得要求用户执行开发包解压/移动、clone、download、remote、branch、build、test、commit、push、package 等命令。

### 2.3 自动执行授权边界

本 PRD 中的所有命令块都是 **Codex/A0/子代理的可执行规范**，不是用户教程。对于可回退、仅影响本项目仓库或本项目工作目录的操作，Codex直接执行；遇到错误自行查看日志、调整命令并重试。不得因为“需要下载源码”“需要安装 Qt”“需要在 Mac 打包”“需要推送 GitHub”等理由暂停并询问用户。

允许留到最终验收的仅是操作系统明确要求真人点击的权限确认，以及真实代码签名证书；它们不阻塞 unsigned 内部包。

## 3. 当前 GitHub 仓库自动自举

### 3.0 开发包定位与自动解压

A0 先在当前会话可见目录、当前仓库和会话附件挂载目录中定位本开发包。若输入为 `RelayDesk-Codex-Development-Package-*.zip`，A0 使用系统解压工具或 Python `zipfile` 自动解压到项目临时目录，并从解压后的 `AGENTS.md`、`docs/01_PRD.md` 和 `scripts/autonomous-init-repo.py` 开始执行。不得要求用户先解压、移动或复制开发包。

若开发包文件已经直接位于当前目录，则直接使用；若资料已经安装到源码分支，则使用 `product/` 下的副本。A0 应自行解析实际路径，不得要求用户提供绝对路径。

### 3.1 固定仓库关系

```text
origin   = 当前已连接的用户 GitHub 仓库，负责协作和保存成果
upstream = https://github.com/deskflow/deskflow.git，负责获取上游源码和 tag
```

A0 禁止删除、重命名或覆盖 `origin`。不需要 GitHub Fork。

### 3.2 首轮检查

A0 进入当前目录后自行执行：

```bash
git rev-parse --show-toplevel
git status --short --branch
git remote -v
git ls-remote origin
```

若 `gh` 可用，同时执行：

```bash
gh auth status
gh repo view --json nameWithOwner,defaultBranchRef
```

上述命令用于记录，不需要用户确认。

### 3.3 添加并验证 upstream

```bash
if git remote get-url upstream >/dev/null 2>&1; then
  git remote set-url upstream https://github.com/deskflow/deskflow.git
else
  git remote add upstream https://github.com/deskflow/deskflow.git
fi

git fetch upstream --tags --prune
git rev-parse --short=7 'v1.26.0^{commit}'
# 必须为 760e3b9
```

### 3.4 自动导入源码决策

A0 检查当前仓库根目录是否存在：

```text
CMakeLists.txt
src/apps
src/lib
src/lib/platform
```

#### 情况 A：当前仓库已经是 RelayDesk/Deskflow 源码仓库

- 验证当前集成分支；
- fetch/pull 远程；
- 不重复导入；
- 继续开发。

#### 情况 B：当前仓库只有开发包、README 或空仓库

A0 自动执行：

1. 将当前开发包复制到临时目录，避免切换分支时丢失；
2. 若工作区有未提交资料，使用 stash 或安全临时分支保存，不删除；
3. 若远程已存在 `origin/product/relaydesk-v1`，直接跟踪该分支；
4. 否则从 `v1.26.0` 创建 `product/relaydesk-v1`；
5. 恢复根 `AGENTS.md`，并在 `product/AGENTS.md` 保留可重复执行副本；
6. 将其余开发资料安装到 `product/`；
7. 生成 bootstrap 报告；
8. 创建提交并推送。

为避免覆盖当前仅包含开发包的工作目录，`autonomous-init-repo.py` 可以在同一个 Git 仓库下自动创建相邻 worktree（例如 `../<repo>-relaydesk`），A0 随后切换到该 worktree 继续；用户不需要操作。

参考命令：

```bash
git switch --create product/relaydesk-v1 v1.26.0
python product/scripts/install-package.py \
  --package-root <临时开发包目录> \
  --repo "$(git rev-parse --show-toplevel)" \
  --force-agents

git add AGENTS.md product
git commit -m "chore(bootstrap): initialize RelayDesk from Deskflow v1.26.0"
git push -u origin product/relaydesk-v1
```

A0 的第一项实际操作必须是直接运行开发包提供的自举脚本，而不是把命令返回给用户：

```bash
python scripts/autonomous-init-repo.py \
  --repo "$(git rev-parse --show-toplevel)"
```

脚本输出 `SOURCE_WORKTREE=<path>` 后，A0 自动切换该 worktree 继续开发。脚本同时安装根 `AGENTS.md`、`product/` 资料和 `.github/workflows/relaydesk-build.yml`，创建 bootstrap commit 并默认推送。

PowerShell：

```powershell
python .\scripts\autonomous-init-repo.py --repo (git rev-parse --show-toplevel)
```

脚本默认自动创建 bootstrap 提交、推送 `product/relaydesk-v1`，并安装 `.github/workflows/relaydesk-build.yml`。脚本打印的 `SOURCE_WORKTREE` 是后续 Codex 会话工作目录；A0 必须自行切换进去继续执行。脚本失败时由 A0 读取错误并自行修正，不把命令交给用户。

### 3.5 不允许的 Git 操作

除非为恢复已确认的损坏仓库，否则禁止：

- `git push --force`；
- `git reset --hard` 覆盖未保存工作；
- 删除用户默认分支；
- 用上游地址替换 `origin`；
- 要求用户重新建仓库或 Fork。

## 4. Git 提交与远程同步

### 4.1 分支模型

```text
product/relaydesk-v1       唯一产品集成分支
agent/a1/*                  上游/构建
agent/a2/*                  网络/发现
agent/a3/*                  GUI
agent/a4/windows-*          Windows
agent/a5/macos-*            macOS
agent/a6/*                  文件传输
agent/a7/*                  测试/发布
release/*                   发布候选
```

### 4.2 小功能完成立即提交

每个可独立验证的小功能完成后，必须在当前代理分支创建提交：

```bash
git add <本功能涉及文件>
git commit -m "feat(file): FILE-001 complete single-file offer"
```

提交前至少执行受影响的最小构建或测试。禁止把数十个无关功能堆进一个提交。Codex 可调用：

```bash
python product/scripts/git-checkpoint.py \
  --task FILE-001 --type feat --area file \
  --summary "complete single-file offer" \
  --paths src/lib/filetransfer product/TASK_BOARD.md
```

任务完成、平台切换或需要另一代理消费时，加 `--push-task`，由脚本推送当前代理分支。

### 4.3 何时推送

以下任一条件满足时必须推送代理分支：

- 共享接口已可供另一平台代理使用；
- 当前工作需要从 Windows 切换到 Mac，或反向切换；
- 一个任务已完成；
- 会话即将结束；
- 需要 GitHub Actions 构建当前提交。

```bash
git push -u origin HEAD
```

### 4.4 阶段完成

A0 自动：

1. fetch 最新远程；
2. 合并或 cherry-pick 已验证提交；
3. 解决冲突；
4. 更新状态文档；
5. 运行当前可用测试；
6. 推送集成分支；
7. 创建阶段标签；
8. 触发 GitHub Actions；
9. 保存 artifact 地址和 SHA-256。

```bash
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
# 合并已验证代理分支
git push origin product/relaydesk-v1
git tag -a relaydesk-phase2-20260812-01 -m "RelayDesk Phase 2"
git push origin relaydesk-phase2-20260812-01
```

可以调用：

```bash
python product/scripts/complete-stage.py \
  --stage phase2 \
  --summary "single/multi-file transfer core"
```

### 4.5 不设置开发门禁

- PR 可选；
- 不要求人工 review；
- 不启用 required checks；
- 不设置覆盖率阈值；
- 不设置发布审批；
- CI 失败用于修复，不用于让用户手工放行；
- A0 对可回退的内部变更自行决策并继续。

## 5. Windows 与 macOS 协同开发

### 5.1 唯一同步通道

源码只通过 GitHub 远程仓库同步。构建产物只通过 GitHub Actions artifact 或草稿 Release 同步。

禁止：

- 手工复制源码目录；
- 在两台机器分别长期维护未推送改动；
- 通过文件传输工具交换开发分支；
- 把 `build/`、Qt SDK 或安装包提交 Git。

### 5.2 每个平台会话开始

Windows PowerShell：

```powershell
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
git switch -c agent/a4/windows-<task>  # 不存在时创建
```

macOS：

```bash
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
git switch -c agent/a5/macos-<task>    # 不存在时创建
```

### 5.3 活跃开发期间定时同步

Windows A4 与 macOS A5 在活跃开发期间必须自动轮询 GitHub，不依赖用户提醒。默认同步点为：

- 会话启动并完成仓库初始化后；
- 开始每个可独立验证的小功能之前；
- 本地连续开发每满 15 分钟；
- 自己完成 commit/push 后；
- 准备消费共享接口、合并平台代理提交或运行跨平台测试之前；
- A0 推送产品提交、协议标签、阶段标签或紧急协作消息后。

定时同步至少执行 `git fetch origin --prune --tags`，读取：

- `origin/product/relaydesk-v1` 是否前进；
- 最新 `relaydesk-protocol-v1-*` 与 `relaydesk-phase*` 标签；
- `coord/platform-sync` 的新消息；
- 对方已在交流消息中明确列出的平台代理分支和 commit。

平台代理不得在工作树存在未提交改动时盲目切换或合并。发现产品分支前进后，先完成当前安全
小切片并提交，或使用独立 worktree；随后把 `origin/product/relaydesk-v1` 合入自己的平台代理
分支，运行受影响测试并推送。禁止 force push、`reset --hard` 或静默丢弃本地/对方改动。

“每 15 分钟”是活跃会话内的最长轮询间隔，不要求无人运行的 Codex 会话在后台常驻；会话恢复
后的第一项协作操作必须立即同步。

### 5.4 Windows/macOS 交流区

平台代理唯一的持久化交流区为：

```text
branch: coord/platform-sync
path:   product/working/platform-sync/
```

目录按写入方隔离：

```text
product/working/platform-sync/
├── a0/       # 仅 A0 写：广播、冻结点、集成结果
├── windows/  # 仅 A4 写：Windows 状态、接口需求、交接
├── macos/    # 仅 A5 写：macOS 状态、接口需求、交接
├── README.md
└── TEMPLATE.md
```

每条消息使用独立 Markdown 文件，文件名为
`YYYYMMDD-HHMMSSZ-<task-id>-<short-topic>.md`，UTC 时间只用于排序。消息必须包含 author、
target、base product SHA、platform branch、commit、status、affected contracts、tests、
`PASS/FAIL/NOT_RUN`、blocker 和 requested action。不得写入凭据、私钥、用户文件、完整敏感
路径或大型日志。

`coord/platform-sync` 只保存轻量协作消息，不保存或交换源码、补丁、二进制和构建产物，也不
取代 `product/relaydesk-v1`、平台代理分支、Actions artifact、`PROJECT_STATE.md` 或
`TASK_BOARD.md`。代码仍在对应平台代理分支提交，消息只引用可获取的 branch/commit/tag/run。

A4 只追加 `windows/`，A5 只追加 `macos/`，A0 只追加 `a0/`。推送被拒绝时，代理必须自动
fetch，并在无冲突基础上 rebase 尚未发布的纯消息提交后重试；不得 force push。接收方读到需要
确认的消息后，在自己的目录新增 ACK 文件并引用原消息路径及所消费的 commit，不修改对方文件。
A0 负责初始化并维护 `coord/platform-sync`，但 Windows/macOS 可直接在各自目录交流，不必等待
A0 转述。

### 5.5 接口协同

文件传输协议、公共数据结构和 CMake target 由 A2/A6 先提交并推送。Windows/macOS 适配必须基于同一个共享 commit。

在 `PROTO-FREEZE-001` 完成前，A4/A5 不得开始依赖未冻结消息的 service 实现。协议标签
推送后，Windows/macOS 会话先验证当前分支包含该标签指向的提交，再分别创建平台分支。
平台发现共享契约缺字段时，只提交最小需求和复现证据，由 A2/A6 owner 统一修改；不得在
平台 adapter 中复制或派生第二套协议类型。

平台代理完成后：

```bash
git add <platform files>
git commit -m "feat(windows): WIN-001 integrate transfer service"
git push -u origin HEAD
```

A0 合并两端分支后触发双平台构建。另一平台不等待用户传文件，按 5.3 定时 fetch，并通过
5.4 交流区读取对方的 commit 与验证结果。

### 5.6 共享状态

A0 每次阶段推送同时更新：

- `product/PROJECT_STATE.md`；
- `product/TASK_BOARD.md`；
- `product/working/stages/*.md`；
- 当前 Windows/macOS 可构建 commit；
- artifact 名称与校验值；
- `PASS/FAIL/NOT_RUN`。

## 6. 自动环境准备

本节命令全部由 Codex 执行。不得输出“请在 Windows/Mac 上安装”后停止；本地不可自动准备时直接走 GitHub Actions runner。

A0/A4/A5 必须先读取真实上游：

```text
doc/dev/build.md
CMakeLists.txt
.github/actions/install-dependencies/action.yml
.github/workflows/continuous-integration.yml
deploy/windows/deploy.cmake
deploy/mac/deploy.cmake
```

固定最低要求：CMake 3.24+、Qt 6.7+、OpenSSL 3+、C++20。第一版自动构建优先复用上游 CI 使用的 Qt 和 WiX 版本。

### 6.1 Windows 自动准备

优先复用已安装工具。缺少时由 Codex 使用 `winget`、Python/aqtinstall、vcpkg 和 dotnet tool 自动安装：

```powershell
winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements
winget install --id Microsoft.DotNet.SDK.8 -e --accept-package-agreements --accept-source-agreements
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
  --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" `
  --accept-package-agreements --accept-source-agreements

python -m pip install --user aqtinstall
# Qt 具体 arch 以当前 Qt 包和上游 CI 为准
python -m aqt install-qt windows desktop 6.10.1 win64_msvc2022_64 -O .tools/Qt

git clone https://github.com/microsoft/vcpkg.git .tools/vcpkg
.\.tools\vcpkg\bootstrap-vcpkg.bat

dotnet tool install --global wix --version 5.0.2
wix extension add --global WixToolset.UI.wixext/5.0.2
wix extension add --global WixToolset.Util.wixext/5.0.2
wix extension add --global WixToolset.Firewall.wixext/5.0.2
```

若某个包已存在，Codex 跳过安装。若本机无管理员权限或安装失败，不询问用户，改用 GitHub Actions Windows runner。

### 6.2 macOS 自动准备

优先复用 Xcode/Command Line Tools 和 Homebrew。缺少普通依赖时：

```bash
brew update
brew install cmake ninja qt openssl@3 googletest python
```

若没有 Homebrew，Codex可以安装 Homebrew或直接使用 GitHub Actions macOS runner。若缺失 Xcode 且无法非交互安装，本地构建标记 `NOT_RUN`，但必须继续通过 macOS Actions 构建 App/DMG。

## 7. 自动构建、测试与打包

### 7.1 Windows

Codex 自动定位 Visual Studio 开发环境、Qt、vcpkg，然后执行等价流程：

```powershell
cmake -S . -B build/windows/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSKIP_BUILD_TESTS=ON `
  -DBUILD_TESTS=ON `
  -DBUILD_INSTALLER=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-release `
  -DCMAKE_PREFIX_PATH="$env:RELAYDESK_QT_PREFIX"

cmake --build build/windows/release --config Release --parallel
ctest --test-dir build/windows/release -C Release --output-on-failure
cmake --build build/windows/release --config Release --target package package_source --parallel
```

上游 Windows 打包始终可生成 7Z；检测到 WiX 时生成安装包。Codex把结果复制到 `dist/windows/`，计算 SHA-256，并上传 artifact。

Windows 本地唯一入口由 A4 自动运行；脚本内部自动完成环境准备：

```powershell
& .\product\scripts\package-windows.ps1 `
  -RepoRoot (git rev-parse --show-toplevel)
```

### 7.2 macOS

```bash
QT_PREFIX="$(brew --prefix qt)"
OPENSSL_PREFIX="$(brew --prefix openssl@3)"

cmake -S . -B build/macos/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14 \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX;$OPENSSL_PREFIX" \
  -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX" \
  -DSKIP_BUILD_TESTS=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_OSX_BUNDLE=ON \
  -DBUILD_INSTALLER=ON

cmake --build build/macos/release --parallel
ctest --test-dir build/macos/release --output-on-failure
cmake --build build/macos/release --target package package_source --parallel
```

上游 CPack 生成 `.dmg`。无 Developer ID 时使用 ad-hoc/unsigned 内部包并明确命名，不等待用户提供证书。

macOS 本地唯一入口由 A5 自动运行；脚本内部自动完成环境准备：

```bash
./product/scripts/package-macos.sh \
  --repo "$(git rev-parse --show-toplevel)"
```

### 7.3 GitHub Actions 回退

A0 必须通过 `product/scripts/install-github-workflows.py` 安装并维护唯一的非门禁工作流；禁止并存第二套工作流文件名：

```text
.github/workflows/relaydesk-build.yml
```

触发条件：

- `workflow_dispatch`；
- `product/relaydesk-v1` 的阶段推送；
- `relaydesk-phase*` 标签。

矩阵至少包含：

- Windows 2022 x64；
- macOS 15 arm64 runner（可用时）；
- build、test、package；
- artifact upload。

工作流不得成为 required check。A0 自动触发并监控：

```bash
gh workflow run relaydesk-build.yml --ref product/relaydesk-v1
gh run list --workflow relaydesk-build.yml --limit 5
gh run watch <run-id> --exit-status
gh run download <run-id> --dir dist/actions
# 或统一调用：
python product/scripts/run-github-actions.py --ref product/relaydesk-v1
```

失败时 A0 读取日志、修复代码或构建配置、创建小功能提交并重新运行，不要求用户操作。成功后 A0/A7 自动下载两个平台的 artifact，为下载文件生成 SHA-256，写入 Actions run ID、commit、run URL 和校验值，并在工作区安全时自动提交该轻量运行报告。若 `gh` 不可用，A0 使用当前会话已连接的 GitHub 工具完成同样的 workflow 查询、日志读取和 artifact 下载。

## 8. 产品平台范围

### P0

- Windows 10 1809+ / Windows 11 x64；
- macOS 14+ Apple Silicon（与固定上游 arm64 CI 目标一致）；
- 同一局域网；
- IPv4；
- unsigned 内部包允许作为交付。

### P1

- Intel macOS；
- Windows ARM64；
- 跨子网手动地址；
- 文件复制粘贴、屏幕边缘投递、Explorer/Finder 集成。

## 9. 用户故事

### 键鼠

- 鼠标越过屏幕边缘后进入另一台设备；
- 键盘焦点随目标设备切换；
- Windows `Ctrl` 与 macOS `Command` 可语义映射；
- 断线、睡眠或切换不留下卡住的修饰键；
- 重启后自动恢复连接。

### 文件传输

- 单文件、多文件、整个文件夹；
- 拖到目标设备卡片发送；
- 接收、拒绝或信任设备自动接收；
- 显示进度、当前文件、速度和 ETA；
- 暂停、继续、取消；
- 断线/重启后续传；
- 同名默认自动重命名；
- 中文、空格、Emoji 文件名；
- 10 GB 以上文件流式传输；
- 近期传输历史与打开目录。

### 紧凑界面、权限与后台运行

- 用户打开应用后，在一个小窗口内看到当前状态、权限提醒、设备和当前传输，不需要在
  多个侧栏页面间切换才能完成高频操作；
- 用户能明确知道缺少哪一项系统权限、该权限用于什么，并直接进入对应系统设置；
- 输入权限未开启时，用户仍可向已可达设备发送文件或接收文件；
- 用户最小化或按设置关闭主窗口后，共享服务可继续在 tray/menu bar 运行；
- 用户可从 tray/menu bar 恢复窗口、暂停或继续共享，并明确退出应用。

## 10. 功能需求

### FR-BASE

| ID | 需求 | 优先级 |
|---|---|---|
| FR-BASE-001 | 保持 Deskflow 双向 Server/Client 能力 | P0 |
| FR-BASE-002 | 保持现有 TLS、剪贴板、布局和热键能力 | P0 |
| FR-BASE-003 | 固定 v1.26.0 基线并可同步上游 | P0 |
| FR-BASE-004 | 品牌集中配置 | P0 |
| FR-BASE-005 | 当前 GitHub 仓库自动导入、提交和推送 | P0 |
| FR-BASE-006 | Windows/macOS 自动构建和 artifact | P0 |
| FR-BASE-007 | Windows/macOS 活跃开发会话至少每 15 分钟及关键边界自动 fetch | P0 |
| FR-BASE-008 | `coord/platform-sync` 提供目录所有权隔离的追加式平台代理交流区 | P0 |

### FR-PROTOCOL

| ID | 需求 | 优先级 |
|---|---|---|
| FR-PROTO-001 | v1 `MessageType` 全量 implemented/reserved 分类，不允许活动占位消息 | P0 |
| FR-PROTO-002 | 每个 implemented 消息具有规范 schema、codec、validator、错误和负例 | P0 |
| FR-PROTO-003 | Windows/macOS 共用冻结正负 test vector，字节级结果一致 | P0 |
| FR-PROTO-004 | `IFileTransferService` 是 GUI/平台唯一文件业务接口 | P0 |
| FR-PROTO-005 | 共享接口由 A2/A6 owner 先提交，A4/A5 从同一冻结提交实现 | P0 |
| FR-PROTO-006 | 协议文档、代码注册表和自动审计保持一致 | P0 |

### FR-DISCOVERY

| ID | 需求 | 优先级 |
|---|---|---|
| FR-DISC-001 | 广播本机设备摘要和能力 | P0 |
| FR-DISC-002 | 同网段设备发现、去重、离线超时 | P0 |
| FR-DISC-003 | 手动 IP/主机名回退 | P0 |
| FR-DISC-004 | 多网卡选择 | P0 |

### FR-PAIR

| ID | 需求 | 优先级 |
|---|---|---|
| FR-PAIR-001 | 稳定 deviceId | P0 |
| FR-PAIR-002 | 复用/生成本地 TLS 身份 | P0 |
| FR-PAIR-003 | 简单六位确认或指纹确认 | P0 |
| FR-PAIR-004 | 本地信任保存与撤销 | P0 |
| FR-PAIR-005 | 指纹变化重新确认 | P0 |

内部版本不开发账号、组织、RBAC、复杂 PAKE、云端证书服务和审批流程。

### FR-FILE

| ID | 需求 | 优先级 |
|---|---|---|
| FR-FILE-001 | 独立文件连接 | P0 |
| FR-FILE-002 | 单文件/多文件/文件夹 | P0 |
| FR-FILE-003 | 流式分块，默认 1 MiB | P0 |
| FR-FILE-004 | `.part` 与原子完成 | P0 |
| FR-FILE-005 | SHA-256 完整性校验 | P0 |
| FR-FILE-006 | pause/resume/cancel | P0 |
| FR-FILE-007 | 断线/进程重启续传 | P0 |
| FR-FILE-008 | 自动重命名/覆盖/跳过/询问 | P0 |
| FR-FILE-009 | 本地历史和失败重试 | P0 |
| FR-FILE-010 | 最低路径越界防护 | P0 |
| FR-FILE-011 | 应用设备卡片拖放 | P0 |
| FR-FILE-012 | 跨设备文件复制粘贴 | P1 |
| FR-FILE-013 | Explorer/Finder 集成 | P1 |

### FR-UI

| ID | 需求 | 优先级 |
|---|---|---|
| FR-UI-001 | 当前设备和发现/配对设备首页 | P0 |
| FR-UI-002 | 拖动设备卡片配置位置 | P0 |
| FR-UI-003 | 配对向导 | P0 |
| FR-UI-004 | 传输中心 | P0 |
| FR-UI-005 | 中文/英文 i18n | P0 |
| FR-UI-006 | 托盘快速操作 | P0 |
| FR-UI-007 | 使用已确认设计稿的共享 Qt 单栏首页；默认 560×420、最小建议 520×380 logical px | P0 |
| FR-UI-008 | 首页采用 52 px 顶栏、单行权限条、两行语义设备条目和 52 px 迷你传输条 | P0 |
| FR-UI-009 | Windows/macOS 共用信息架构和语义状态，仅由平台 adapter 提供系统能力与外观差异 | P0 |
| FR-UI-010 | 最小化到托盘、可设置的关闭到托盘、首次提示一次，以及恢复/暂停共享/退出菜单 | P0 |
| FR-UI-011 | 原创临时 Logo 使用“双设备 + 中继点”语义、SVG 几何单源、16 px 可辨；App 彩色、tray 单色 | P0 |

### FR-PERMISSION

| ID | 需求 | 优先级 |
|---|---|---|
| FR-PERM-001 | 权限按对应能力分别门控；一个能力缺权不得无条件阻断不依赖该权限的其他能力 | P0 |
| FR-PERM-002 | macOS 分别展示 Accessibility、Input Monitoring、Local Network 的状态、用途和系统设置入口 | P0 |
| FR-PERM-003 | macOS 应用回到前台后异步复检三项权限，并更新共享 `PermissionSnapshot` 和界面摘要 | P0 |
| FR-PERM-004 | 缺少 Accessibility 或 Input Monitoring 时禁用受影响的输入方向并给出原因，但文件传输保持可用 | P0 |
| FR-PERM-005 | 缺少 Local Network 时明确降级设备发现/直连，并保留与平台实际可用性一致的修复入口 | P0 |
| FR-PERM-006 | 权限状态不得声称应用已经自动授予权限，也不得用重复弹窗强迫用户授权 | P0 |

## 11. 非功能需求

### 性能

- 键鼠事件优先于文件数据；
- GUI 不被扫描、哈希或磁盘 I/O 阻塞；
- 大文件传输内存有界；
- UI 进度刷新不高于 5 Hz；
- 默认总并发任务 2。

### 可靠性

- 临时文件不冒充完成文件；
- 任务可恢复或明确失败；
- 断线不留下卡住按键；
- 配置损坏使用简单备份/默认值。

### 内部使用最低安全

- 复用 TLS；
- 接收路径限制在目标目录；
- 拒绝绝对路径和父级遍历；
- 不自动执行收到文件；
- 密钥不入仓库。

不建设强制限流、病毒扫描、内容审查、DLP、WAF、零信任、复杂权限和安全审批。

### 可维护性

- 共享核心不包含平台头文件；
- 协议有版本与测试；
- 上游同步单独提交；
- Windows/macOS 只通过 GitHub 协同。

## 12. 默认行为

- 接收目录：`Downloads/RelayDesk`；
- 首次接收询问；
- 信任设备可自动接收；
- 同名自动重命名；
- 断线自动续传；
- 历史默认最近 1,000 个任务或 90 天；
- 收到文件不自动打开；
- “最小化后隐藏到托盘”和“关闭窗口后继续在托盘运行”默认开启，用户可分别关闭；
- 首次关闭到托盘显示一次性提示，后续不重复打扰；
- 托盘/menu bar 的“退出”始终执行真正退出，不受关闭到托盘设置影响；
- 开发包默认 unsigned。

## 13. 最终交付物

A0 必须在远程仓库和 GitHub artifact/Release 中交付：

1. `product/relaydesk-v1` 完整源码；
2. 阶段化 Git 历史；
3. Windows x64 安装包/便携包；
4. macOS arm64 App/DMG；
5. source package；
6. SHA-256；
7. Windows/macOS 构建日志；
8. 安装说明；
9. 由 `product/templates/FINAL_ACCEPTANCE.md` 生成并填写的最终验收清单；
10. 已知问题与 `NOT_RUN`。

## 14. 最终验收场景

### AC-001 双向控制

Windows→Mac、Mac→Windows 鼠标跨屏、键盘、滚轮、自动重连。

### AC-002 文件互传

两个方向完成单文件、多文件和文件夹传输。

### AC-003 大文件与续传

≥10 GB 文件断网后续传，最终摘要一致，内存不随文件大小线性增长。

### AC-004 文件夹

包含空目录、中文、Emoji、10,000 小文件的目录结构正确。

### AC-005 输入隔离

文件传输接近链路上限时，键鼠无明显卡顿。

### AC-006 安装包

干净 Windows 和 macOS 可安装 unsigned 内部包；用户在最终验收时完成系统授权后，键鼠和文件互传可用。

### AC-007 Git 协作

远程仓库存在小功能提交、阶段推送、阶段标签和双平台 artifact；两端不依赖手工复制源码。
Windows/macOS 在活跃开发期间按 5.3 自动同步，并能通过 `coord/platform-sync` 中各自拥有的
目录交换 commit、测试证据、接口需求和 ACK；交流消息不包含源码、二进制或凭据。

### AC-008 v1 协议与接口冻结

`Protocol.h` 中所有 v1 消息均被归类为 implemented 或 reserved；implemented 消息都有
schema、codec、validator、错误负例和冻结向量。Windows x64 与 macOS arm64 对相同向量
产生相同结果，`IFileTransferService` 和共享 snapshot 可由两个平台从同一冻结提交直接
消费，不需要平台私有协议补丁。

### AC-009 紧凑共享界面

Windows 与 macOS 从同一共享 Qt 信息架构呈现已确认的单栏首页；在 100% 缩放时默认
窗口为 560×420 logical px，520×380 logical px 仍可访问全部关键操作。顶栏和迷你传输条
均为 52 px，权限摘要保持单行；设备条目的设备名/主操作与状态/能力保持两行语义。

### AC-010 macOS 权限能力门控

分别覆盖 Accessibility、Input Monitoring、Local Network 映射到冻结 `PermissionState`
的 `Unknown`、`NotRequired`、`Granted`、`Denied`、`NeedsAction` 状态。每项均显示自身用途、
当前状态和可用的系统设置入口；从系统设置回到前台后无需重启即可复检。只撤销输入类权限时，
相关输入能力被禁用并给出原因，已可达设备的文件发送与接收仍可完成。

### AC-011 托盘与真正退出

验证最小化到托盘、开启/关闭“关闭窗口后继续运行”设置、首次提示只出现一次，以及从托盘
恢复窗口、暂停/继续共享。选择“退出”后进程结束，按键/修饰键状态被释放，监听器关闭，
可恢复传输状态安全落盘且 tray/menu bar 图标移除；macOS 菜单栏使用单色 template 图标。

### AC-012 临时品牌资产

App/安装包使用原创“双设备 + 中继点”彩色临时 Logo；tray/menu bar 使用由同一 SVG 几何
单源派生的单色图标。逐项检查 16 px、浅色/深色系统主题和高 DPI，不复用 Deskflow 图形，
且名称、路径和生成规则仍由集中品牌配置管理。
