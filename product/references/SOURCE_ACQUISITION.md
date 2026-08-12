# Source Acquisition — Codex Autonomous Workflow

用户不执行开发包解压或源码下载。本文件供 A0/A1 自动执行。若输入为 ZIP，A0 先自行定位并解压。

## 固定来源

```text
upstream: https://github.com/deskflow/deskflow.git
tag: v1.26.0
short commit: 760e3b9
origin: 当前已连接的用户 GitHub 仓库
```

## 自动入口

```bash
python scripts/autonomous-init-repo.py --repo "$(git rev-parse --show-toplevel)"
```

脚本会保留 `origin`、添加 `upstream`、fetch tag、验证 commit，并在需要时创建同一 Git 仓库的 sibling worktree，避免破坏当前开发包目录。随后安装资料、commit 并 push `product/relaydesk-v1`。

## A0 回退流程

若脚本因特殊仓库状态失败，A0 必须自行：

1. 备份未提交文件；
2. 添加/修正 upstream；
3. fetch v1.26.0；
4. 创建新的 product worktree；
5. 安装本包；
6. commit/push；
7. 记录实际工作目录。

不得要求用户手工 Fork、Clone、复制源码或添加 remote。
