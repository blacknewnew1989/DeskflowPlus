# RelayDesk Codex 多代理开发包 v1.2.0

> 临时产品代号：RelayDesk。
>
> 上游基线：Deskflow v1.26.0 / `760e3b9`。
>
> 执行模式：当前 Codex 目录和会话已经连接 GitHub 仓库；Codex 负责源码获取、环境准备、开发、提交、推送、双平台构建与打包，用户只做最终验收。

## 项目目标

在一个 Git 仓库、一套 C++20 + Qt 6 代码中实现：

- Windows/macOS 共享鼠标、键盘、滚轮；
- 文本和图片剪贴板；
- 局域网发现、配对、自动重连；
- 单文件、多文件、文件夹传输；
- 暂停、恢复、取消、断点续传；
- Windows 安装包与 macOS App/DMG。

## 不需要用户执行的工作

```text
不需要手工解压或移动开发包
不需要 Fork
不需要 Clone/下载 Deskflow
不需要添加 remote
不需要创建分支
不需要安装 Qt/CMake/编译器
不需要执行构建或测试命令
不需要提交或推送
不需要在 Windows 与 Mac 之间复制源码
不需要手工打包
```

上述工作已写入 `AGENTS.md` 和 `docs/01_PRD.md`，由 A0 自动完成。

## 使用方法

本包进入已连接 GitHub 仓库的 Codex 会话后，发送 `CODEX_START_PROMPT.txt` 的唯一一行。A0 会自行定位/解压开发包并运行仓库自举脚本；此后不需要用户下载源码、安装工具或运行命令，直到远程仓库存在完整源码、阶段提交、双平台 artifact 和最终验收说明。

## Git 协作规则

```text
origin   用户当前 GitHub 仓库，负责保存和协作
upstream deskflow/deskflow，负责获取固定上游版本
```

- 每个小功能完成后创建独立提交；
- 每个阶段完成后合并到 `product/relaydesk-v1` 并推送；
- 阶段结束触发 Windows/macOS GitHub Actions；
- 构建产物通过 artifact/Release 共享，不提交二进制；
- PR 可选，不设置强制审核、required checks、覆盖率阈值和发布审批。

## 包内内容

```text
AGENTS.md                         Codex 仓库级最高指令
CODEX_START_PROMPT.txt            唯一首轮提示词
START_HERE.md                     用户只做最终验收的启动说明
PROJECT_STATE.md                  远程共享项目状态
TASK_BOARD.md                     可并行任务板

docs/01_PRD.md                   完整产品需求 + 自动仓库/构建/协作流程
docs/10_BUILD_CI_RELEASE.md       Windows/macOS 自动构建与打包
docs/20_AUTONOMOUS_EXECUTION_AND_GIT_WORKFLOW.md
                                  自动执行命令参考

prompts/                          A0～A7 多代理职责
scripts/autonomous-init-repo.py   当前仓库自动导入上游、安装 Actions、提交和推送
scripts/git-checkpoint.py          小功能提交与任务分支推送
scripts/complete-stage.py          阶段报告、提交、标签和推送
scripts/run-github-actions.py      监控双平台构建并下载 artifacts
scripts/package-windows.ps1       Windows 自动环境/构建/测试/打包入口
scripts/package-macos.sh          macOS 自动环境/构建/测试/打包入口
templates/FINAL_ACCEPTANCE.md   最终只交给用户执行的验收清单模板
```

## 开发边界

- 内部局域网使用，不建设账号、云后台、RBAC、零信任、审批和内容审查；
- 只保留 TLS、路径限制、临时文件和完整性校验等防止数据损坏的基础措施；
- 缺少签名凭据时生成明确标注的 unsigned 内部包；
- macOS 系统权限授权延后到最终验收，不阻塞代码与打包；
- 对外分发时仍需履行 Deskflow 的 GPL 许可证义务。

## 唯一自动入口

所有源码获取和仓库初始化只允许走 `scripts/autonomous-init-repo.py`。该入口默认提交并推送 `product/relaydesk-v1`，同时安装唯一的 `relaydesk-build.yml`。包内不保留第二套 bootstrap 或第二套 Actions 命名，避免 Windows/macOS 会话采用不同流程。
