# 20 Codex 全自动执行与双平台 Git 工作流

本文件是 `01_PRD.md` 的执行命令参考。所有命令由 Codex/A0/平台代理运行，用户只在项目最终交付后安装和验收。

## 1. 首轮自动入口

A0 的第一项实际操作是定位并执行自举脚本，而不是向用户复述命令：

```bash
python scripts/autonomous-init-repo.py \
  --repo "$(git rev-parse --show-toplevel)"
```

若开发包已经位于源码仓库 `product/`：

```bash
python product/scripts/autonomous-init-repo.py \
  --repo "$(git rev-parse --show-toplevel)"
```

脚本输出 `SOURCE_WORKTREE=<path>` 后，A0 自动切换该 worktree 继续开发。

## 2. 自举完成条件

- `origin` 保留且可推送；
- `upstream` 指向 Deskflow；
- v1.26.0 已验证为 `760e3b9`；
- `product/relaydesk-v1` 存在；
- 根 `AGENTS.md` 与 `product/` 已安装；
- `.github/workflows/relaydesk-build.yml` 已安装；
- bootstrap commit 已创建；
- 集成分支已推送。

## 3. 每次代理会话同步

```bash
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
```

代理创建自己的任务分支/worktree。Windows 和 macOS 不通过 ZIP、聊天附件或共享目录同步源码。

## 4. 小功能提交

```bash
git add <本功能文件>
git commit -m "feat(<area>): <task-id> <summary>"
```

满足任一条件立即推送：

- 任务完成；
- 共享接口可供另一平台代理使用；
- 需要 Windows/macOS 切换；
- 会话即将结束；
- 需要 Actions 构建当前 SHA。

```bash
git push -u origin HEAD
```

## 5. Windows 自动构建

```powershell
& .\product\scripts\package-windows.ps1 `
  -RepoRoot (git rev-parse --show-toplevel)
```

本地准备失败时 A0 自动转到 `relaydesk-build.yml` Windows job。

## 6. macOS 自动构建

```bash
./product/scripts/package-macos.sh \
  --repo "$(git rev-parse --show-toplevel)"
```

本地 Xcode/Homebrew/权限不可用时 A0 自动转到同一工作流的 macOS job。

## 7. 阶段完成

A0 在 `product/relaydesk-v1` 上运行：

```bash
python product/scripts/complete-stage.py \
  --stage phase2 \
  --summary "file transfer vertical slice"
```

脚本自动提交阶段资料、推送集成分支、创建并推送 `relaydesk-phase*` 标签。标签自动触发 Windows/macOS 构建。

## 8. Actions 监控和产物下载

普通分支构建：

```bash
python product/scripts/run-github-actions.py \
  --ref product/relaydesk-v1
```

阶段标签已自动触发：

```bash
python product/scripts/run-github-actions.py \
  --ref <relaydesk-phase-tag> \
  --no-trigger
```

A0/A7 读取失败日志并修复、提交、推送、重跑；成功后把 artifacts 下载至 `dist/actions/<run-id>/`，生成下载文件 SHA-256，并把运行报告自动提交到当前协作分支（若存在预先 staged 改动则由 A0在清理后补交）。

## 9. 平台交接

```text
共享协议 owner 提交并 push
→ Windows/macOS fetch 同一 SHA
→ 两端各自适配并 commit/push
→ A0 合并
→ 双平台 Actions
```

任何一端不得持有长期未推送的共享接口改动。

## 10. 最终交付

A0 创建 `release/<version>`，完成双平台 package/source package、SHA-256、安装说明、已知问题和最终验收文档，并推送 release tag。无签名凭据时交付 unsigned 内部包。

用户只负责：

- 安装 Windows/macOS 最终包；
- 完成 macOS 系统权限授权；
- 连接真实两台设备；
- 执行最终验收清单；
- 报告最终验收中发现的问题。

用户不负责源码下载、依赖安装、仓库切换、提交、推送、Actions、日志读取、artifact 下载或打包。
