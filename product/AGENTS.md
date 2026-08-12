# RelayDesk Repository Instructions for Codex

本文件是本仓库所有 Codex 代理的最高级项目指令。子目录内的 `AGENTS.md` 只能补充，不得违背本文件。

## 0. 执行契约：用户只做最终验收

默认环境已经满足：

- 当前工作目录是项目 Git 仓库；
- `origin` 已连接到用户的 GitHub 仓库；
- 当前 Codex 会话具备该仓库的读取、提交和推送能力；
- Windows 与 macOS 的开发会话都使用同一个远程仓库协作。

A0 和子代理必须自行完成：

1. 自动定位并在需要时解压当前会话中的 RelayDesk 开发包，识别仓库根目录、当前分支、`origin` 和 GitHub 登录状态；
2. 保留现有 `origin`，添加或修正 `upstream=https://github.com/deskflow/deskflow.git`；
3. 获取 Deskflow v1.26.0 源码并导入当前仓库，不要求用户手动 Fork、Clone、下载或复制文件；
4. 建立产品集成分支、平台代理分支和必要 worktree；
5. 检查并安装/准备可自动安装的编译依赖；本机工具链不可用时自动使用 GitHub Actions 构建；
6. 完成开发、测试、文档、提交、推送和阶段标签；
7. 生成 Windows 安装包与 macOS App/DMG，并上传为 GitHub Actions artifact 或草稿 Release；
8. 持续更新 `product/PROJECT_STATE.md`、`product/TASK_BOARD.md` 和阶段报告。

禁止把以下工作转交给用户：

- 解压或搬运开发包；
- 克隆或下载源码；
- 添加 Git remote；
- 创建分支；
- 安装普通开发工具；
- 执行构建、测试、提交、推送；
- 在 Windows 与 Mac 之间手工传递源码或构建产物；
- 手工制作安装包。

只有操作系统无法自动代办的动作才放入最终验收清单，例如 macOS 辅助功能授权、首次打开未公证内部包、提供真实代码签名凭据。即使缺少这些条件，开发也不得停止：先生成 unsigned 内部包并完成其他可执行工作。

## 1. 项目目标

基于 Deskflow v1.26.0 产品化二开，交付 Windows/macOS 局域网协作工具：

- 多设备共享鼠标、键盘和滚轮；
- 文本和图片剪贴板；
- 局域网设备发现、简单配对和自动重连；
- 单文件、多文件、文件夹传输；
- 暂停、继续、取消、失败重试和断线续传；
- 传输进度、速度、预计剩余时间和历史；
- Windows/macOS 可安装的内部发布包。

临时产品代号为 RelayDesk。正式名称未确定前，名称、包标识和图标必须集中配置，不得全仓散落硬编码。

## 2. 上游基线与仓库初始化

- Upstream repository: `https://github.com/deskflow/deskflow.git`
- Tag: `v1.26.0`
- Baseline short commit: `760e3b9`
- License: `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception`
- Stack: C++20、Qt 6、CMake、OpenSSL、平台原生适配层。

A0 首轮必须执行当前可见的 `docs/01_PRD.md`；资料安装进入源码工作树后改读 `product/docs/01_PRD.md`。唯一入口是 `scripts/autonomous-init-repo.py`（安装后为 `product/scripts/autonomous-init-repo.py`），唯一双平台工作流是 `.github/workflows/relaydesk-build.yml`。不得另造第二套 bootstrap 或工作流命名。核心原则：

```text
origin   = 用户当前已连接的 GitHub 仓库，始终保留并用于推送
upstream = deskflow/deskflow，只用于 fetch/tag/上游同步
```

若当前仓库尚未包含 Deskflow 源码，A0 必须自动：

1. 备份当前开发包资料；
2. `git fetch upstream --tags --prune`；
3. 从 `v1.26.0` 创建或恢复 `product/relaydesk-v1`；
4. 把开发包资料安装到根 `AGENTS.md` 与 `product/`；
5. 提交 bootstrap；
6. `git push -u origin product/relaydesk-v1`。

不得删除或重命名用户的 `origin`，不得要求用户另建 Fork。不得使用 `--force`、`reset --hard` 或重写远程历史作为常规流程。

## 3. 开发阶段：顺序优先，不设置审批门禁

阶段用于排序和同步，不是审批流、人工放行或阻塞式门禁。A0 可以在不冲突的前提下并行推进共享核心、GUI、平台调查和测试骨架。

### Phase 0：仓库与原版基线

- 自动导入固定上游源码；
- Windows 原版构建；
- macOS 原版构建；
- 条件允许时完成双向键鼠/滚轮/文本剪贴板联调；
- 记录工具链、命令、日志、生成物和上游问题。

本地平台暂不可用时，不停止整个项目：使用 GitHub Actions 先完成编译和打包，真机输入权限与联调留到最终验收，但必须标记 `NOT_RUN`，不得伪造 PASS。

### Phase 1：产品基础

品牌集中配置、中文 i18n、设备模型、发现、配对、信任记录、自动重连、设备卡片。

### Phase 2：文件传输内核

独立文件通道、协议编解码、offer/accept、流式单文件、多文件、文件夹、进度。

### Phase 3：可靠性与 UI

暂停/恢复/取消、断点续传、冲突策略、历史、传输中心、应用内拖放。

### Phase 4：双平台发布

Windows/macOS 平台细节、自动构建、安装包、App/DMG、unsigned 发布、回归测试。

### Phase 5：增强

跨设备文件复制粘贴、屏幕边缘投递、Explorer/Finder 集成。真正延续 Windows OLE 与 macOS 原生拖拽会话不作为第一版条件。

## 4. 架构硬约束

1. 单仓库、单产品、共享核心；Windows 和 macOS 只保留必要的平台适配。
2. 不重写 Deskflow 已成熟的输入捕获、注入、Server/Client 协议与屏幕切换。
3. 文件传输与键鼠事件使用不同连接、缓冲区和队列。
4. 文件传输是 P2P，对等设备角色不受 Deskflow Server/Client 角色限制。
5. 大文件必须流式读写，禁止整文件读入内存。
6. 接收先写 `.part`，完成后校验并原子移动。
7. 断点状态使用简单本地文件；不引入数据库服务器。
8. 不新增账号、云端后台、RBAC、租户、审批、遥测平台、公网中继和付费系统。
9. 不引入 Boost、gRPC、WebRTC、Electron 或额外语言运行时，除非现有 Qt/标准库无法完成且 A0 记录 ADR。
10. 不为了“企业级”“未来扩展”创建大量无用抽象、权限层、网关或安全组件。

## 5. Git、提交与推送规则

### 5.1 分支

```text
product/relaydesk-v1                  产品集成分支
agent/a1/<task>                       上游/构建调查
agent/a2/<task>                       网络与发现
agent/a3/<task>                       GUI
agent/a4/windows-<task>               Windows
agent/a5/macos-<task>                 macOS
agent/a6/<task>                       文件传输核心
agent/a7/<task>                       测试与发布
release/<version>                     发布候选
```

A0 是唯一集成负责人。共享接口先由 owner 提交并推送，平台代理随后同步，不允许 Windows 与 Mac 同时各自发明一套协议。

### 5.2 小功能完成必须提交

一个“小功能”指可以独立编译、测试或演示的最小纵向切片，例如：

- 一个协议消息及其测试；
- 单文件 offer/accept；
- 设备卡片显示在线状态；
- Windows 接收目录处理；
- macOS 权限检测页。

完成后立即执行：

```bash
git add <本任务相关文件>
git commit -m "<type>(<area>): <task-id> <summary>"
```

不得累计大量无关改动后一次提交。每次提交至少运行受影响的最小测试并在提交说明或阶段报告中记录。

### 5.3 阶段完成必须推送

每个阶段或可供另一平台消费的共享接口完成后，A0 必须：

1. 合并/挑选已验证的小提交到 `product/relaydesk-v1`；
2. 更新 `PROJECT_STATE.md`、`TASK_BOARD.md` 和阶段报告；
3. 运行当前可用测试；
4. `git push origin product/relaydesk-v1`；
5. 创建并推送阶段标签，例如 `relaydesk-phase2-20260812-01`；
6. 触发双平台 GitHub Actions 构建；
7. 把 artifact/Release 地址和 SHA-256 写入阶段报告。

PR 可用于查看 diff，但不是必需门禁。禁止自行开启 branch protection、required review、required checks、覆盖率阈值或发布审批。

## 6. Windows 与 macOS 协同开发

远程 GitHub 仓库是唯一源码真相。禁止通过聊天附件、网盘、共享目录或手工 ZIP 交换源码。

每个平台会话开始时自动执行：

```bash
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
```

平台代理在独立分支开发。阶段完成后提交并推送代理分支，A0 合入集成分支。另一平台随后 fetch/pull 后继续。

跨平台接口协作顺序：

```text
A2/A6 提交共享协议/接口
        ↓ push
Windows 与 macOS 同步同一 commit
        ↓
A4/A5 分别实现平台适配
        ↓ push
A0 合并并触发双平台构建
```

构建产物不得提交到 Git；使用 GitHub Actions artifact 或草稿 Release 共享。

## 7. 构建与打包职责

A0/A4/A5/A7 必须读取上游真实构建文档和 CI，自动准备环境并执行。首轮从 `docs/01_PRD.md`、`docs/10_BUILD_CI_RELEASE.md` 读取；资料安装进入源码工作树后从 `product/docs/01_PRD.md`、`product/docs/10_BUILD_CI_RELEASE.md` 读取。

- Windows：由 `product/scripts/package-windows.ps1` 自动准备环境、构建、测试和打包；生成便携包及可用时的 WiX 安装包；无签名证书时生成 `unsigned` 包。
- macOS：由 `product/scripts/package-macos.sh` 自动准备环境、构建、测试和打包；生成 `.app`/`.dmg`；无 Developer ID 时使用 ad-hoc 或明确的 unsigned 内部包，不阻塞开发。
- 本机依赖安装失败时，立即配置/触发 `.github/workflows/relaydesk-build.yml` 的 Windows 与 macOS runner 作为构建回退。
- A0/A7 使用 `product/scripts/run-github-actions.py` 或当前会话的 GitHub 工具自动监控 run、读取失败日志、下载 artifacts；不得把 Actions 操作交给用户。
- 最终构建记录必须包含 commit、工具链、命令、日志、Actions run、artifact 名称和 SHA-256。

## 8. 编码和测试

- 遵循上游 `.clang-format`、命名、CMake 和测试风格。
- C++20；优先 RAII、值语义和 Qt 明确生命周期。
- 网络回调不得执行长时间哈希、目录扫描或磁盘阻塞。
- 用户可见字符串走 Qt 翻译系统。
- 不吞错误，不用空 catch，不提交假实现。
- 每个提交运行最小测试；每个阶段运行当前可用的完整测试。
- 不能运行的真机项标记 `NOT_RUN`，不阻止其他工作继续。

必须覆盖协议帧、路径、0B/大文件、文件夹、中断、续传、冲突、输入优先级以及 Win↔Mac E2E。

## 9. 内部使用的最低安全边界

只保留防止数据损坏和明显误写所必需的措施：

- 复用 Deskflow 已有 TLS/指纹能力；
- 设备首次配对由用户最终确认，内部版本不建设账号、RBAC、复杂 PKI、零信任或云端认证；
- 接收路径必须限制在用户选定目录，拒绝绝对路径和 `..`；
- 收到的文件不自动执行；
- `.part` 成功完成后再原子改名；
- 摘要用于发现传输损坏，不引入逐块复杂签名体系；
- 私钥和真实凭据不入仓库。

不得新增病毒扫描、DLP、文件类型白名单、强制大小限制、审批、限速策略、内容审查或企业安全平台，除非用户后续明确要求。

## 10. 允许询问用户的唯一事项

开发期间默认不询问。只有以下无法自动完成的事项可以在最终验收清单中列出：

- macOS 系统设置中的 Accessibility/Input Monitoring/Local Network 授权；
- 首次打开未公证内部应用的系统确认；
- 真实 Apple Developer ID 或 Windows 代码签名证书；
- 正式产品名、商标和对外分发许可证决策；
- 会删除用户数据或重写远程历史的不可逆操作。

以上事项不得成为编码、构建 unsigned 包、推送远程或生成测试 artifact 的前置条件。

## 11. Definition of Done

任务 Done：

- 真实代码已实现；
- 最小测试完成；
- 独立小提交已创建；
- 文档/状态已更新；
- 可由另一平台代理从远程仓库继续。

阶段 Done：

- 集成分支包含阶段成果；
- 当前可用测试已运行；
- 已推送 `origin/product/relaydesk-v1` 和阶段标签；
- Windows/macOS 构建已触发；
- artifact、日志、校验值和 `NOT_RUN` 项已记录。

项目交付：

- Windows 安装包；
- macOS App/DMG；
- 源码已推送；
- 最终验收清单和使用说明完整；
- 用户只需安装、授权并按清单验收。

## 12. 默认决策

- 产品代号：RelayDesk；
- 默认接收目录：`Downloads/RelayDesk`；
- 默认冲突策略：自动重命名；
- 默认文件块：1 MiB；
- 默认文件通道端口：动态协商，固定回退 24801；
- 控制元数据：CBOR；文件数据：二进制流；
- 最终完整性校验：SHA-256；
- P0 平台：Windows x64 + Apple Silicon macOS；
- 开发产物默认 unsigned，签名不阻塞内部交付。
