# RelayDesk 开发包校验报告

- Package version: 1.2.0
- Generated: 2026-08-12 18:18 (Asia/Singapore)
- Upstream target: Deskflow v1.26.0 / `760e3b9`
- Execution model: current authenticated GitHub repository, Codex autonomous execution, user final acceptance only

## 1. 静态完整性校验

| 项目 | 结果 | 说明 |
|---|---|---|
| 必需文件与目录 | PASS | PRD、A0～A7、自动自举、双平台脚本、单一 Actions 工作流均存在 |
| Python 语法 | PASS | 8 个 `.py` 文件通过内存 `compile()`，未生成 `__pycache__` |
| Bash 语法 | PASS | 5 个 `.sh` 文件通过 `bash -n` |
| JSON 语法 | PASS | 5 个 JSON 文件均可解析 |
| GitHub Actions YAML | PASS | 唯一工作流可由 PyYAML 解析 |
| Markdown 代码围栏 | PASS | 48 个 Markdown 文件围栏成对 |
| 协议测试向量 | PASS | 6 个向量；32-byte 固定头按网络字节序解析 |
| 包级自检 | PASS | `scripts/validate-package.py` 检查 48 个必需文件并通过 |
| 重复入口检查 | PASS | 不存在第二套 bootstrap 或第二套 Actions 模板目录 |
| 中间人工操作措辞 | PASS | 任务/PR/代理模板不再要求开发中途人工验证 |
| OS/缓存杂项 | PASS | 无 `__pycache__`、`.DS_Store`、`Thumbs.db` |

## 2. Git 仓库全自动流程模拟

在隔离的本地 Git 环境中创建了：模拟 Deskflow 上游仓库、带 tag 的固定基线、裸 `origin`、仅包含 README 的当前项目仓库，以及第二个跨平台开发会话。

| 场景 | 结果 | 已验证行为 |
|---|---|---|
| 当前仓库自动自举 | PASS | 保留 `origin`、添加 `upstream`、fetch/tag 校验、创建产品 worktree、安装资料和工作流、commit、push |
| 自举脚本重复执行 | PASS | 已安装脚本再次执行时无多余提交、工作区保持干净 |
| 仅开发包/空项目仓库 | PASS | 自动从固定 tag 建立完整源码产品分支，不要求用户 clone |
| 第二 Windows/macOS 会话接续 | PASS | 新会话从同一远程产品分支恢复并继续，不靠 ZIP/共享目录同步源码 |
| 阶段完成 | PASS | 阶段报告强制纳入 Git、提交、推送产品分支、创建并推送阶段 tag |
| 远程一致性 | PASS | 本地产品分支 HEAD 与裸远程分支 HEAD 一致 |

## 3. GitHub Actions 自动化模拟

使用受控的假 `gh` 命令模拟 GitHub Actions 生命周期，未访问真实仓库。

| 场景 | 结果 | 已验证行为 |
|---|---|---|
| 显式 workflow dispatch | PASS | 不复用同 SHA 的旧 run；等待新 run；监控到完成 |
| artifact 下载 | PASS | 自动下载 Windows/macOS 模拟产物 |
| 下载文件校验 | PASS | 自动生成 `DOWNLOAD_SHA256SUMS.txt`，报告逐文件 SHA-256 |
| Actions 报告提交 | PASS | 工作区安全时自动提交并推送 `product/working/actions/<run-id>.json` |
| annotated stage tag | PASS | 使用 `^{commit}` 解析 tag，匹配 GitHub `headSha`，不误用 tag object SHA |
| 失败 run | PASS | 非零返回、保存失败日志和 JSON 报告，供 A0 自动修复重跑 |

## 4. 构建产物收集模拟

| 场景 | 结果 | 已验证行为 |
|---|---|---|
| Windows 产物收集 | PASS | 收集 MSI/ZIP、生成 artifact manifest 与 SHA-256 |
| macOS 产物收集 | PASS | 收集 DMG/source package，并把 `.app` 打成明确的 unsigned ZIP |
| 非目标文件过滤 | PASS | 不相关文件不会进入交付目录 |

## 5. 自动执行职责复核

PRD 和根 `AGENTS.md` 已明确由 Codex/A0 实际完成：

- 定位并在需要时解压开发包；
- 获取并验证 Deskflow 源码；
- 初始化 `origin/upstream`、分支与 worktree；
- 安装或准备 Windows/macOS 依赖；
- 本机不可用时切换 GitHub Actions runner；
- 开发、测试、小功能提交、任务分支推送；
- 阶段合并、产品分支推送和阶段 tag；
- Windows/macOS 构建和打包；
- Actions 监控、失败日志读取、修复、重跑和 artifact 下载；
- 校验值、状态、已知问题和最终验收资料维护。

用户只在项目完成后安装最终包、授予操作系统要求的权限并执行最终验收。

## 6. 当前生成环境未执行

| 项目 | 状态 | 原因与后续执行者 |
|---|---|---|
| PowerShell 运行时语法/执行 | NOT_RUN | 当前生成环境没有 `pwsh`/Windows PowerShell；由 Windows Codex 会话或 Actions runner 执行 |
| Qt/C++ starter 真实编译 | NOT_RUN | 当前环境没有 Qt 6；由双平台构建流程执行 |
| Deskflow Windows 原版/产品构建 | NOT_RUN | 需要 Windows/MSVC/Qt；由 Codex 本机流程或 Windows Actions runner 执行 |
| Deskflow macOS 原版/产品构建 | NOT_RUN | 需要 macOS/Xcode；由 Codex 本机流程或 macOS Actions runner 执行 |
| 真实 GitHub Actions | NOT_RUN | 未对用户仓库执行写操作；A0 在目标仓库中自动安装并运行 |
| Windows↔macOS 真实键鼠/剪贴板/文件互传 | FINAL_ACCEPTANCE_REQUIRED | 需要两台真实设备、系统权限与物理操作；由用户最终验收 |
| 正式代码签名与公证 | OPTIONAL | 缺少真实证书不阻塞 unsigned 内部包 |

## 7. 结论

本包已形成“开发全过程由 Codex 完成、用户只做最终验收”的闭环。源码下载、开发包解压、仓库初始化、依赖准备、跨平台同步、提交、推送、构建、打包、Actions 监控和 artifact 下载均不再作为用户操作步骤。
